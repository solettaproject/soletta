/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <alloca.h>
#include <errno.h>
#include <float.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SOL_LOG_DOMAIN &_lwm2m_domain

#include "sol-log-internal.h"
#include "sol-list.h"
#include "sol-lwm2m.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-monitors.h"
#include "sol-random.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_lwm2m_domain, "lwm2m");

#define LWM2M_UPDATE_QUERY_PARAMS (4)
#define LWM2M_REGISTER_QUERY_PARAMS (5)
#define NUMBER_OF_PATH_SEGMENTS (3)
#define DEFAULT_CLIENT_LIFETIME (86400)
#define DEFAULT_BINDING_MODE (SOL_LWM2M_BINDING_MODE_U)
#define DEFAULT_LOCATION_PATH_SIZE (10)

struct sol_lwm2m_server {
    struct sol_coap_server *coap;
    struct sol_ptr_vector clients;
    struct sol_ptr_vector clients_to_delete;
    struct sol_monitors registration;
    struct {
        struct sol_timeout *timeout;
        uint32_t lifetime;
    } lifetime_ctx;
};

struct sol_lwm2m_client_object {
    struct sol_vector instances;
    uint16_t id;
};

struct sol_lwm2m_client_info {
    struct sol_vector objects;
    char *name;
    char *location;
    char *sms;
    char *objects_path;
    uint32_t lifetime;
    time_t register_time;
    struct sol_lwm2m_server *server;
    struct sol_network_link_addr cliaddr;
    enum sol_lwm2m_binding_mode binding;
    struct sol_coap_resource resource;
};

static bool lifetime_timeout(void *data);

static void
dispatch_registration_event(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event)
{
    uint16_t i;
    struct sol_monitors_entry *m;

    SOL_MONITORS_WALK (&server->registration, m, i)
        ((sol_lwm2m_server_regisration_event_cb)m->cb)((void *)m->data, server,
            cinfo, event);
}

static void
client_objects_clear(struct sol_vector *objects)
{
    uint16_t i;
    struct sol_lwm2m_client_object *object;

    SOL_VECTOR_FOREACH_IDX (objects, object, i)
        sol_vector_clear(&object->instances);

    sol_vector_clear(objects);
}

static void
client_info_del(struct sol_lwm2m_client_info *cinfo)
{
    free(cinfo->sms);
    free(cinfo->location);
    free(cinfo->name);
    free(cinfo->objects_path);
    client_objects_clear(&cinfo->objects);
    free(cinfo);
}

static enum sol_lwm2m_binding_mode
get_binding_mode_from_str(const struct sol_str_slice binding)
{
    static const struct sol_str_table map[] = {
        SOL_STR_TABLE_PTR_ITEM("U", SOL_LWM2M_BINDING_MODE_U),
        //TODO: The modes below are not supported for now.
        SOL_STR_TABLE_PTR_ITEM("UQ", SOL_LWM2M_BINDING_MODE_UNKNOWN),
        SOL_STR_TABLE_PTR_ITEM("S", SOL_LWM2M_BINDING_MODE_UNKNOWN),
        SOL_STR_TABLE_PTR_ITEM("SQ", SOL_LWM2M_BINDING_MODE_UNKNOWN),
        SOL_STR_TABLE_PTR_ITEM("US", SOL_LWM2M_BINDING_MODE_UNKNOWN),
        SOL_STR_TABLE_PTR_ITEM("UQS", SOL_LWM2M_BINDING_MODE_UNKNOWN),
        { }
    };

    return sol_str_table_lookup_fallback(map, binding,
        SOL_LWM2M_BINDING_MODE_UNKNOWN);
}

static void
clients_to_delete_clear(struct sol_ptr_vector *to_delete)
{
    uint16_t i;
    struct sol_lwm2m_client_info *cinfo;

    SOL_PTR_VECTOR_FOREACH_IDX (to_delete, cinfo, i)
        client_info_del(cinfo);
    sol_ptr_vector_clear(to_delete);
}

static void
remove_client(struct sol_lwm2m_client_info *cinfo, bool del)
{
    int r = 0;

    r = sol_ptr_vector_remove(&cinfo->server->clients, cinfo);
    if (r < 0)
        SOL_WRN("Could not remove the client %s from the clients list",
            cinfo->name);
    r = sol_coap_server_unregister_resource(cinfo->server->coap,
        &cinfo->resource);
    if (r < 0)
        SOL_WRN("Could not unregister coap resource for the client: %s",
            cinfo->name);
    if (del)
        client_info_del(cinfo);
    else {
        r = sol_ptr_vector_append(&cinfo->server->clients_to_delete, cinfo);
        if (r < 0)
            SOL_WRN("Could not add the client to pending clients list");
    }
}

static struct sol_lwm2m_client_object *
find_client_object_by_id(struct sol_vector *objects,
    uint16_t id)
{
    uint16_t i;
    struct sol_lwm2m_client_object *cobject;

    SOL_VECTOR_FOREACH_IDX (objects, cobject, i) {
        if (cobject->id == id)
            return cobject;
    }

    return NULL;
}

static int
fill_client_objects(struct sol_lwm2m_client_info *cinfo,
    struct sol_coap_packet *req, bool update)
{
    uint8_t *buf;
    uint16_t len, i;
    int r;
    bool has_content;
    struct sol_vector objects;
    struct sol_str_slice content, *object;
    struct sol_lwm2m_client_object *cobject;
    uint16_t *instance;

#define TO_INT(_data, _endptr, _len, _i) \
    _i = sol_util_strtol(_data, &_endptr, _len, 10); \
    if (_endptr == _data || errno != 0 ) { \
        SOL_WRN("Could not convert object to int. (%.*s)", \
            SOL_STR_SLICE_PRINT(*object)); \
        r = -EINVAL; \
        goto err_exit; \
    }
#define EXIT_IF_FAIL(_condition) \
    if (_condition) { \
        r = -EINVAL; \
        SOL_WRN("Malformed object: %.*s", \
            SOL_STR_SLICE_PRINT(*object)); \
        goto err_exit; \
    }

    has_content = sol_coap_packet_has_payload(req);

    if (!has_content && !update) {
        SOL_WRN("The registration request has no payload!");
        return -ENOENT;
    } else if (!has_content)
        return 0;

    client_objects_clear(&cinfo->objects);

    r = sol_coap_packet_get_payload(req, &buf, &len);
    SOL_INT_CHECK(r, < 0, r);
    content.data = (const char *)buf;
    content.len = len;

    SOL_DBG("Register payload content: %.*s", (int)len, buf);
    objects = sol_str_slice_split(content, ",", 0);

    if (!objects.len) {
        SOL_WRN("The objects list is empty!");
        return -EINVAL;
    }

    SOL_VECTOR_FOREACH_IDX (&objects, object, i) {
        char *endptr;
        uint16_t id;

        *object = sol_str_slice_trim(*object);

        EXIT_IF_FAIL(object->len < 4 || object->data[0] != '<');

        /*
           Object form: </ObjectId[/InstanceID]>
           Where ObjectId is an integer (must be present)
           InstanceId is an integer, may not be present and can not be UINT16_MAX
           Alternate path: </a/path>[;rt="oma.lwm2m"][;ct=1058]
         */
        if (sol_str_slice_str_contains(*object, "rt=\"oma.lwm2m\"")) {
            struct sol_str_slice path;
            endptr = memrchr(object->data, '>', object->len);
            EXIT_IF_FAIL(!endptr);
            path.data = object->data + 1;
            path.len = endptr - path.data;
            r = sol_util_replace_str_from_slice_if_changed(&cinfo->objects_path,
                path);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
            if (cinfo->objects_path && streq(cinfo->objects_path, "/")) {
                free(cinfo->objects_path);
                cinfo->objects_path = NULL;
            }
            continue;
        } else if (sol_str_slice_str_contains(*object, "ct=")) {
            //The content type value for json was not defined yet.
            //TODO: Support JSON formats.
            SOL_WRN("Only text format is supported for now");
            r = -EINVAL;
            goto err_exit;
        }

        EXIT_IF_FAIL(object->data[object->len - 1] != '>');

        //Removing '<', '>' and '/'
        object->data += 2;
        object->len -= 3;

        TO_INT(object->data, endptr, object->len, id);

        cobject = find_client_object_by_id(&cinfo->objects, id);

        if (!cobject) {
            cobject = sol_vector_append(&cinfo->objects);
            SOL_NULL_CHECK_GOTO(cobject, err_exit_nomem);
            sol_vector_init(&cobject->instances, sizeof(uint16_t));
            cobject->id = id;
        }

        //Advance to instance ID
        object->len -= endptr - object->data;

        //Instance ID not provided.
        if (!object->len)
            continue;

        //Skip '/'
        object->data = endptr + 1;
        object->len--;

        instance = sol_vector_append(&cobject->instances);
        SOL_NULL_CHECK_GOTO(instance, err_exit_nomem);

        TO_INT(object->data, endptr, object->len, *instance);

        if (*instance == UINT16_MAX) {
            SOL_WRN("The instance id value: %" PRIu16 " must not be used!",
                UINT16_MAX);
            r = -EPERM;
            goto err_exit;
        }
    }

    sol_vector_clear(&objects);
    return 0;

err_exit_nomem:
    r = -ENOMEM;
err_exit:
    sol_vector_clear(&objects);
    return r;

#undef EXIT_IF_FAIL
#undef TO_INT
}

static int
fill_client_info(struct sol_lwm2m_client_info *cinfo,
    struct sol_coap_packet *req, bool update)
{
    uint16_t i, count;
    bool has_name = false;
    struct sol_str_slice query[5];
    int r;

    r = sol_coap_find_options(req, SOL_COAP_OPTION_URI_QUERY, query,
        update ? LWM2M_UPDATE_QUERY_PARAMS : LWM2M_REGISTER_QUERY_PARAMS);
    SOL_INT_CHECK(r, < 0, r);
    count = r;
    cinfo->register_time = time(NULL);

    for (i = 0; i < count; i++) {
        struct sol_str_slice key, value;
        const char *sep;

        SOL_DBG("Query:%.*s", SOL_STR_SLICE_PRINT(query[i]));
        sep = memchr(query[i].data, '=', query[i].len);

        if (!sep) {
            SOL_WRN("Could not find the separator '=' at: %.*s",
                SOL_STR_SLICE_PRINT(query[i]));
            break;
        }

        key.data = query[i].data;
        key.len = sep - query[i].data;
        value.data = sep + 1;
        value.len = query[i].len - key.len - 1;

        if (sol_str_slice_str_eq(key, "ep")) {
            if (update) {
                SOL_WRN("The lwm2m client can not update it's name"
                    " during the update");
                r = -EPERM;
                goto err_cinfo_prop;
            }
            //Required info
            has_name = true;
            cinfo->name = sol_str_slice_to_string(value);
            SOL_NULL_CHECK_GOTO(cinfo->name, err_cinfo_prop);
        } else if (sol_str_slice_str_eq(key, "lt")) {
            char *endptr;
            cinfo->lifetime = sol_util_strtoul(value.data, &endptr,
                value.len, 10);
            if (endptr == value.data || errno != 0) {
                SOL_WRN("Could not convert the lifetime to integer."
                    " Lifetime: %.*s", SOL_STR_SLICE_PRINT(value));
                goto err_cinfo_prop;
            }
        } else if (sol_str_slice_str_eq(key, "sms")) {
            r = sol_util_replace_str_from_slice_if_changed(&cinfo->sms, value);
            SOL_INT_CHECK_GOTO(r, < 0, err_cinfo_prop);
        } else if (sol_str_slice_str_eq(key, "lwm2m") &&
            !sol_str_slice_str_eq(value, "1.0")) {
            r = -EINVAL;
            SOL_WRN("LWM2M version not supported:%.*s",
                SOL_STR_SLICE_PRINT(value));
            goto err_cinfo_prop;
        } else if (sol_str_slice_str_eq(key, "b")) {
            cinfo->binding = get_binding_mode_from_str(value);
            r = -EINVAL;
            SOL_INT_CHECK_GOTO(cinfo->binding,
                == SOL_LWM2M_BINDING_MODE_UNKNOWN, err_cinfo_prop);
        }
    }

    if (has_name || update)
        return fill_client_objects(cinfo, req, update);
    else {
        SOL_WRN("The client did not provide its name!");
        return -EINVAL;
    }

err_cinfo_prop:
    return r;
}

static int
reschedule_timeout(struct sol_lwm2m_server *server)
{
    struct sol_lwm2m_client_info *cinfo;
    uint32_t smallest_remeaning, remeaning, lf;
    time_t now;
    uint16_t i;
    int r;

    clients_to_delete_clear(&server->clients_to_delete);

    if (server->lifetime_ctx.timeout)
        sol_timeout_del(server->lifetime_ctx.timeout);

    if (!sol_ptr_vector_get_len(&server->clients)) {
        server->lifetime_ctx.timeout = NULL;
        server->lifetime_ctx.lifetime = 0;
        return 0;
    }

    smallest_remeaning = UINT32_MAX;
    now = time(NULL);
    SOL_PTR_VECTOR_FOREACH_IDX (&server->clients, cinfo, i) {
        remeaning = cinfo->lifetime - (now - cinfo->register_time);
        if (remeaning < smallest_remeaning) {
            smallest_remeaning = remeaning;
            lf = cinfo->lifetime;
        }
    }

    //Add some spare seconds.
    r = sol_util_uint32_mul(smallest_remeaning + 2, 1000, &smallest_remeaning);
    SOL_INT_CHECK(r, < 0, r);
    server->lifetime_ctx.timeout = sol_timeout_add(smallest_remeaning,
        lifetime_timeout, server);
    SOL_NULL_CHECK(server->lifetime_ctx.timeout, -ENOMEM);
    server->lifetime_ctx.lifetime = lf;
    return 0;
}

static bool
lifetime_timeout(void *data)
{
    struct sol_ptr_vector to_delete = SOL_PTR_VECTOR_INIT;
    struct sol_lwm2m_server *server = data;
    struct sol_lwm2m_client_info *cinfo;
    uint16_t i;
    int r;

    SOL_DBG("Lifetime timeout! (%" PRIu32 ")", server->lifetime_ctx.lifetime);

    SOL_PTR_VECTOR_FOREACH_IDX (&server->clients, cinfo, i) {
        if (server->lifetime_ctx.lifetime != cinfo->lifetime)
            continue;
        SOL_DBG("Deleting client %s for inactivity", cinfo->name);
        r = sol_ptr_vector_append(&to_delete, cinfo);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&to_delete, cinfo, i) {
        dispatch_registration_event(server, cinfo,
            SOL_LWM2M_REGISTRATION_EVENT_TIMEOUT);
        remove_client(cinfo, true);
    }

    sol_ptr_vector_clear(&to_delete);

    r = reschedule_timeout(server);
    if (r < 0)
        SOL_WRN("Could not reschedule the lifetime timeout");
    return false;

err_exit:
    sol_ptr_vector_clear(&to_delete);
    return true;
}

static int
update_client(struct sol_coap_server *coap,
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data)
{
    struct sol_lwm2m_client_info *cinfo = data;
    struct sol_coap_packet *response;
    int r;

    SOL_DBG("Client update request (name: %s)", cinfo->name);

    response = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response, -ENOMEM);

    r = fill_client_info(cinfo, req, true);
    SOL_INT_CHECK_GOTO(r, < 0, err_update);

    r = reschedule_timeout(cinfo->server);
    SOL_INT_CHECK_GOTO(r, < 0, err_update);

    dispatch_registration_event(cinfo->server, cinfo,
        SOL_LWM2M_REGISTRATION_EVENT_UPDATE);

    sol_coap_header_set_code(response, SOL_COAP_RSPCODE_CHANGED);
    return sol_coap_send_packet(coap, response, cliaddr);

err_update:
    sol_coap_header_set_code(response, SOL_COAP_RSPCODE_BAD_REQUEST);
    (void)sol_coap_send_packet(coap, response, cliaddr);
    return r;
}

static int
delete_client(struct sol_coap_server *coap,
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data)
{
    struct sol_lwm2m_client_info *cinfo = data;
    struct sol_coap_packet *response;

    SOL_DBG("Client delete request (name: %s)", cinfo->name);

    response = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response, -ENOMEM);

    remove_client(cinfo, false);

    if (!sol_ptr_vector_get_len(&cinfo->server->clients) &&
        cinfo->server->lifetime_ctx.timeout) {
        sol_timeout_del(cinfo->server->lifetime_ctx.timeout);
        cinfo->server->lifetime_ctx.timeout = NULL;
        cinfo->server->lifetime_ctx.lifetime = 0;
        SOL_DBG("Client list is empty");
    }

    dispatch_registration_event(cinfo->server, cinfo,
        SOL_LWM2M_REGISTRATION_EVENT_UNREGISTER);

    sol_coap_header_set_code(response, SOL_COAP_RSPCODE_DELETED);
    return sol_coap_send_packet(coap, response, cliaddr);
}

static int
generate_location(char **location)
{
    int r;
    char uuid[37];

    r = sol_util_uuid_gen(false, false, uuid);
    SOL_INT_CHECK(r, < 0, r);
    *location = strndup(uuid, DEFAULT_LOCATION_PATH_SIZE);
    SOL_NULL_CHECK(*location, -ENOMEM);
    return 0;
}

static int
new_client_info(struct sol_lwm2m_client_info **cinfo,
    const struct sol_network_link_addr *cliaddr,
    struct sol_lwm2m_server *server)
{
    int r;

    *cinfo = calloc(1, sizeof(struct sol_lwm2m_client_info) +
        (sizeof(struct sol_str_slice) * NUMBER_OF_PATH_SEGMENTS));
    SOL_NULL_CHECK(cinfo, -ENOMEM);

    (*cinfo)->lifetime = DEFAULT_CLIENT_LIFETIME;
    (*cinfo)->binding = DEFAULT_BINDING_MODE;
    r = generate_location(&(*cinfo)->location);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    (*cinfo)->resource.flags = SOL_COAP_FLAGS_NONE;
    (*cinfo)->resource.path[0] = sol_str_slice_from_str("rd");
    (*cinfo)->resource.path[1] = sol_str_slice_from_str((*cinfo)->location);
    (*cinfo)->resource.path[2] = sol_str_slice_from_str("");
    (*cinfo)->resource.del = delete_client;
    /*
       FIXME: Current spec says that the client update should be handled using
       the post method, however some old clients still uses put.
     */
    (*cinfo)->resource.post = update_client;
    (*cinfo)->resource.put = update_client;
    (*cinfo)->server = server;
    sol_vector_init(&(*cinfo)->objects, sizeof(struct sol_lwm2m_client_object));
    memcpy(&(*cinfo)->cliaddr, cliaddr, sizeof(struct sol_network_link_addr));
    SOL_SET_API_VERSION((*cinfo)->resource.api_version = SOL_COAP_RESOURCE_API_VERSION; )
    return 0;
err_exit:
    free(*cinfo);
    return r;
}

static struct sol_lwm2m_client_info *
get_client_info_by_name(struct sol_ptr_vector *clients,
    const char *name)
{
    struct sol_lwm2m_client_info *cinfo;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (clients, cinfo, i) {
        if (streq(name, cinfo->name))
            return cinfo;
    }

    return NULL;
}

static int
registration_request(struct sol_coap_server *coap,
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data)
{
    struct sol_lwm2m_client_info *cinfo, *old_cinfo;
    struct sol_lwm2m_server *server = data;
    struct sol_coap_packet *response;
    int r;
    bool b;

    SOL_DBG("Client registration request");

    response = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response, -ENOMEM);

    r = new_client_info(&cinfo, cliaddr, server);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    r = fill_client_info(cinfo, req, false);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_del_client);

    old_cinfo = get_client_info_by_name(&server->clients,
        cinfo->name);
    if (old_cinfo) {
        SOL_DBG("Client %s already exists, replacing it.", old_cinfo->name);
        remove_client(old_cinfo, true);
    }

    b = sol_coap_server_register_resource(server->coap, &cinfo->resource,
        cinfo);
    if (!b) {
        SOL_WRN("Could not register the coap resource for client: %s",
            cinfo->name);
        goto err_exit_del_client;
    }

    r = sol_ptr_vector_append(&server->clients, cinfo);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_unregister);

    r = reschedule_timeout(server);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_unregister);

    r = sol_coap_add_option(response, SOL_COAP_OPTION_LOCATION_PATH,
        "rd", strlen("rd"));
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_unregister);
    r = sol_coap_add_option(response,
        SOL_COAP_OPTION_LOCATION_PATH, cinfo->location, strlen(cinfo->location));
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_unregister);

    sol_coap_header_set_code(response, SOL_COAP_RSPCODE_CREATED);

    SOL_DBG("Client %s registered. Location: %s, SMS: %s, binding: %u,"
        " lifetime: %" PRIu32 " objects paths: %s",
        cinfo->name, cinfo->location, cinfo->sms,
        cinfo->binding, cinfo->lifetime, cinfo->objects_path);
    dispatch_registration_event(server, cinfo,
        SOL_LWM2M_REGISTRATION_EVENT_REGISTER);

    return sol_coap_send_packet(coap, response, cliaddr);

err_exit_unregister:
    if (sol_coap_server_unregister_resource(server->coap, &cinfo->resource) < 0)
        SOL_WRN("Could not unregister resource for client: %s", cinfo->name);
err_exit_del_client:
    client_info_del(cinfo);
err_exit:
    sol_coap_header_set_code(response, SOL_COAP_RSPCODE_BAD_REQUEST);
    (void)sol_coap_send_packet(coap, response, cliaddr);
    return r;
}

static const struct sol_coap_resource registration_interface = {
    SOL_SET_API_VERSION(.api_version = SOL_COAP_RESOURCE_API_VERSION, )
    .post = registration_request,
    .flags = SOL_COAP_FLAGS_NONE,
    .path = {
        SOL_STR_SLICE_LITERAL("rd"),
        SOL_STR_SLICE_EMPTY
    }
};
SOL_API struct sol_lwm2m_server *
sol_lwm2m_server_new(uint16_t port)
{
    struct sol_lwm2m_server *server;
    bool b;

    SOL_LOG_INTERNAL_INIT_ONCE;

    server = calloc(1, sizeof(struct sol_lwm2m_server));
    SOL_NULL_CHECK(server, NULL);

    server->coap = sol_coap_server_new(port);
    SOL_NULL_CHECK_GOTO(server->coap, err_coap);

    sol_ptr_vector_init(&server->clients);
    sol_ptr_vector_init(&server->clients_to_delete);
    sol_monitors_init(&server->registration, NULL);
    b = sol_coap_server_register_resource(server->coap,
        &registration_interface, server);
    if (!b) {
        SOL_WRN("Could not register the server resources");
        goto err_register;
    }

    return server;

err_register:
    sol_coap_server_unref(server->coap);
err_coap:
    free(server);
    return NULL;
}

SOL_API void
sol_lwm2m_server_del(struct sol_lwm2m_server *server)
{
    uint16_t i;
    struct sol_lwm2m_client_info *cinfo;

    SOL_NULL_CHECK(server);

    sol_coap_server_unref(server->coap);

    SOL_PTR_VECTOR_FOREACH_IDX (&server->clients, cinfo, i)
        client_info_del(cinfo);
    if (server->lifetime_ctx.timeout)
        sol_timeout_del(server->lifetime_ctx.timeout);

    clients_to_delete_clear(&server->clients_to_delete);
    sol_monitors_clear(&server->registration);
    sol_ptr_vector_clear(&server->clients);
    free(server);
}

SOL_API int
sol_lwm2m_server_add_registration_monitor(struct sol_lwm2m_server *server,
    sol_lwm2m_server_regisration_event_cb cb, const void *data)
{
    struct sol_monitors_entry *m;

    SOL_NULL_CHECK(cb, -EINVAL);
    SOL_NULL_CHECK(server, -EINVAL);

    m = sol_monitors_append(&server->registration,
        (sol_monitors_cb_t)cb, data);
    SOL_NULL_CHECK(m, -ENOMEM);
    return 0;
}

SOL_API int
sol_lwm2m_server_del_registration_monitor(struct sol_lwm2m_server *server,
    sol_lwm2m_server_regisration_event_cb cb, const void *data)
{
    int i;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(cb, -EINVAL);

    i = sol_monitors_find(&server->registration, (sol_monitors_cb_t)cb, data);
    if (i < 0)
        return i;

    return sol_monitors_del(&server->registration, i);
}

SOL_API const struct sol_ptr_vector *
sol_lwm2m_server_get_clients(const struct sol_lwm2m_server *server)
{
    SOL_NULL_CHECK(server, NULL);

    return &server->clients;
}

SOL_API const char *
sol_lwm2m_client_info_get_name(const struct sol_lwm2m_client_info *client)
{
    SOL_NULL_CHECK(client, NULL);

    return client->name;
}

SOL_API const char *
sol_lwm2m_client_info_get_location(const struct sol_lwm2m_client_info *client)
{
    SOL_NULL_CHECK(client, NULL);

    return client->location;
}

SOL_API const char *
sol_lwm2m_client_info_get_sms(const struct sol_lwm2m_client_info *client)
{
    SOL_NULL_CHECK(client, NULL);

    return client->sms;
}

SOL_API const char *
sol_lwm2m_client_info_get_objects_path(
    const struct sol_lwm2m_client_info *client)
{
    SOL_NULL_CHECK(client, NULL);

    return client->objects_path;
}

SOL_API int
sol_lwm2m_client_info_get_lifetime(const struct sol_lwm2m_client_info *client,
    uint32_t *lifetime)
{
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(lifetime, -EINVAL);

    *lifetime = client->lifetime;
    return 0;
}

SOL_API enum sol_lwm2m_binding_mode
sol_lwm2m_client_info_get_binding_mode(
    const struct sol_lwm2m_client_info *client)
{
    SOL_NULL_CHECK(client, SOL_LWM2M_BINDING_MODE_UNKNOWN);

    return client->binding;
}

SOL_API const struct sol_network_link_addr *
sol_lwm2m_client_info_get_address(const struct sol_lwm2m_client_info *client)
{
    SOL_NULL_CHECK(client, NULL);

    return &client->cliaddr;
}

SOL_API const struct sol_vector *
sol_lwm2m_client_info_get_objects(const struct sol_lwm2m_client_info *client)
{
    SOL_NULL_CHECK(client, NULL);

    return &client->objects;
}

SOL_API int
sol_lwm2m_client_object_get_id(const struct sol_lwm2m_client_object *object,
    uint16_t *id)
{
    SOL_NULL_CHECK(object, -EINVAL);
    SOL_NULL_CHECK(id, -EINVAL);

    *id = object->id;
    return 0;
}

SOL_API const struct sol_vector *
sol_lwm2m_client_object_get_instances(
    const struct sol_lwm2m_client_object *object)
{
    SOL_NULL_CHECK(object, NULL);

    return &object->instances;
}

