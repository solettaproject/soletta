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

#define DEFAULT_CLIENT_LIFETIME (86400)
#define DEFAULT_BINDING_MODE (SOL_LWM2M_BINDING_MODE_U)
#define DEFAULT_LOCATION_PATH_SIZE (10)
#define TLV_TYPE_MASK (192)
#define TLV_ID_SIZE_MASK (32)
#define TLV_CONTENT_LENGTH_MASK (24)
#define TLV_CONTENT_LENGHT_CUSTOM_MASK (7)
#define REMOVE_SIGN_BIT_MASK (127)
#define SIGN_BIT_MASK (128)
#define ID_HAS_16BITS_MASK (32)
#define OBJ_LINK_LEN (4)
#define LEN_IS_8BITS_MASK (8)
#define LEN_IS_16BITS_MASK (16)
#define LEN_IS_24BITS_MASK (24)
#define UINT24_BITS (16777215)

#ifndef SOL_NO_API_VERSION
#define LWM2M_TLV_CHECK_API(_tlv, ...) \
    do { \
        if (unlikely((_tlv).api_version != \
            SOL_LWM2M_TLV_API_VERSION)) { \
            SOL_WRN("Couldn't handle tlv that has unsupported version " \
                "'%u', expected version is '%u'", \
                (_tlv).api_version, SOL_LWM2M_TLV_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0);
#define LWM2M_RESOURCE_CHECK_API(_resource, ...) \
    do { \
        if (unlikely((_resource).api_version != \
            SOL_LWM2M_RESOURCE_API_VERSION)) { \
            SOL_WRN("Couldn't handle resource that has unsupported version " \
                "'%u', expected version is '%u'", \
                (_resource).api_version, SOL_LWM2M_RESOURCE_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0);
#else
#define LWM2M_TLV_CHECK_API(_tlv, ...)
#define LWM2M_RESOURCE_CHECK_API(_resource, ...)
#endif

enum tlv_length_size_type {
    LENGTH_SIZE_CHECK_NEXT_TWO_BITS = 0,
    LENGTH_SIZE_8_BITS = 8,
    LENGTH_SIZE_16_BITS = 16,
    LENGTH_SIZE_24_BITS = 32
};

struct sol_lwm2m_server {
    struct sol_coap_server *coap;
    struct sol_ptr_vector clients;
    struct sol_ptr_vector clients_to_delete;
    struct sol_monitors registration;
    struct sol_vector observers;
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

struct observer_entry {
    struct sol_monitors monitors;
    struct sol_lwm2m_server *server;
    struct sol_lwm2m_client_info *cinfo;
    int64_t token;
    char *path;
    bool removed;
};

static bool lifetime_timeout(void *data);

static void
send_ack_if_needed(struct sol_coap_server *coap, struct sol_coap_packet *msg,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_coap_packet *ack;

    if (sol_coap_header_get_type(msg) == SOL_COAP_TYPE_CON) {
        ack = sol_coap_packet_new(msg);
        if (ack) {
            sol_coap_header_set_type(ack, SOL_COAP_TYPE_ACK);
            if (sol_coap_send_packet(coap, ack, cliaddr) < 0)
                SOL_WRN("Could not send the reponse ACK");
        } else
            SOL_WRN("Could not create the response ACK");
    }
}

static void
dispatch_registration_event(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event)
{
    uint16_t i;
    struct sol_monitors_entry *m;

    SOL_MONITORS_WALK (&server->registration, m, i)
        ((sol_lwm2m_server_regisration_event_cb)m->cb)(server, cinfo, event,
            (void *)m->data);
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
    if (_endptr == _data) { \
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
    uint16_t i, count, max_count;
    bool has_name = false;
    struct sol_str_slice query[5];
    int r;

    if (update)
        max_count = 4;
    else
        max_count = 5;

    r = sol_coap_find_options(req, SOL_COAP_OPTION_URI_QUERY, query, max_count);
    SOL_INT_CHECK(r, < 0, r);
    count = r;

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
            uint32_t lifetime;
            char *endptr;
            lifetime = sol_util_strtoul(value.data, &endptr, value.len, 10);
            if (endptr == value.data) {
                SOL_WRN("Could not convert the lifetime to integer."
                    " Lifetime: %.*s", SOL_STR_SLICE_PRINT(value));
                goto err_cinfo_prop;
            }

            //Add some spare time.
            lifetime += 2;
            //To milliseconds
            r = sol_util_uint32_mul(lifetime, 1000, &cinfo->lifetime);
            SOL_INT_CHECK_GOTO(r, < 0, err_cinfo_prop);
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
        (sizeof(struct sol_str_slice) * 2));
    SOL_NULL_CHECK(cinfo, -ENOMEM);

    (*cinfo)->lifetime = DEFAULT_CLIENT_LIFETIME;
    (*cinfo)->binding = DEFAULT_BINDING_MODE;
    r = generate_location(&(*cinfo)->location);
    SOL_INT_CHECK(r, < 0, r);

    (*cinfo)->resource.flags = SOL_COAP_FLAGS_NONE;
    (*cinfo)->resource.path[0] = sol_str_slice_from_str((*cinfo)->location);
    (*cinfo)->resource.path[1] = sol_str_slice_from_str("");
    (*cinfo)->resource.del = delete_client;
    /*
       FIXME: Current spec says that the client update should be handled using
       the post method, however some old clients still uses put.
     */
    (*cinfo)->resource.post = update_client;
    (*cinfo)->resource.put = update_client;
    (*cinfo)->server = server;
    (*cinfo)->register_time = time(NULL);
    sol_vector_init(&(*cinfo)->objects, sizeof(struct sol_lwm2m_client_object));
    memcpy(&(*cinfo)->cliaddr, cliaddr, sizeof(struct sol_network_link_addr));
    SOL_SET_API_VERSION((*cinfo)->resource.api_version = SOL_COAP_RESOURCE_API_VERSION; )
    return 0;
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
    struct sol_buffer path = SOL_BUFFER_INIT_EMPTY;
    int r;
    bool b;

    SOL_DBG("Client registration request");

    response = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response, -ENOMEM);

    r = new_client_info(&cinfo, cliaddr, server);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    r = fill_client_info(cinfo, req, false);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_del_client);

    r = sol_buffer_append_printf(&path, "/rd/%s", cinfo->location);
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

    r = sol_coap_add_option(response,
        SOL_COAP_OPTION_LOCATION_PATH, path.data, path.used);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_unregister);

    sol_coap_header_set_code(response, SOL_COAP_RSPCODE_CREATED);
    sol_buffer_fini(&path);

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
    sol_buffer_fini(&path);
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

static void
observer_entry_del(struct observer_entry *entry)
{
    sol_monitors_clear(&entry->monitors);
    free(entry->path);
}

static void
remove_observer_entry(struct sol_vector *entries, struct observer_entry *entry)
{
    int r;

    r = sol_vector_del_element(entries, entry);
    SOL_INT_CHECK(r, < 0);
    observer_entry_del(entry);
}

static struct observer_entry *
find_observer_entry(struct sol_vector *entries,
    struct sol_lwm2m_client_info *cinfo, const char *path)
{
    uint16_t i;
    struct observer_entry *entry;

    SOL_VECTOR_FOREACH_IDX (entries, entry, i) {
        if (entry->cinfo == cinfo && streq(path, entry->path))
            return entry;
    }

    return NULL;
}

static int
observer_entry_init(struct observer_entry *entry,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo, const char *path)
{
    sol_monitors_init(&entry->monitors, NULL);
    entry->server = server;
    entry->cinfo = cinfo;
    entry->path = strdup(path);
    SOL_NULL_CHECK(entry->path, -ENOMEM);
    return 0;
}

static int
observer_entry_new(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo, const char *path,
    struct observer_entry **entry)
{
    int r;

    *entry = sol_vector_append(&server->observers);
    SOL_NULL_CHECK(*entry, -ENOMEM);

    r = observer_entry_init(*entry, server, cinfo, path);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    return 0;

err_exit:
    sol_vector_del_element(&server->observers, *entry);
    return r;
}

static int
observer_entry_add_monitor(struct observer_entry *entry,
    sol_lwm2m_server_content_cb cb, void *data)
{
    struct sol_monitors_entry *e;

    e = sol_monitors_append(&entry->monitors, (sol_monitors_cb_t)cb, data);
    SOL_NULL_CHECK(e, -ENOMEM);
    return 0;
}

static int
observer_entry_del_monitor(struct observer_entry *entry,
    sol_lwm2m_server_content_cb cb, void *data)
{
    int r;

    r = sol_monitors_find(&entry->monitors, (sol_monitors_cb_t)cb, data);
    SOL_INT_CHECK(r, < 0, r);

    return sol_monitors_del(&entry->monitors, r);
}

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
    sol_vector_init(&server->observers, sizeof(struct observer_entry));

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
    struct observer_entry *entry;

    SOL_NULL_CHECK(server);

    sol_coap_server_unref(server->coap);

    SOL_PTR_VECTOR_FOREACH_IDX (&server->clients, cinfo, i)
        client_info_del(cinfo);

    SOL_VECTOR_FOREACH_IDX (&server->observers, entry, i)
        observer_entry_del(entry);

    if (server->lifetime_ctx.timeout)
        sol_timeout_del(server->lifetime_ctx.timeout);

    clients_to_delete_clear(&server->clients_to_delete);
    sol_monitors_clear(&server->registration);
    sol_vector_clear(&server->observers);
    sol_ptr_vector_clear(&server->clients);
    free(server);
}

SOL_API int
sol_lwm2m_server_add_registration_monitor(struct sol_lwm2m_server *server,
    sol_lwm2m_server_regisration_event_cb cb, void *data)
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
    sol_lwm2m_server_regisration_event_cb cb, void *data)
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

static size_t
get_int_size(int64_t i)
{
    if (i >= INT8_MIN && i <= INT8_MAX)
        return 1;
    if (i >= INT16_MIN && i <= INT16_MAX)
        return 2;
    if (i >= INT32_MIN && i <= INT32_MAX)
        return 4;
    return 8;
}

static size_t
get_double_size(double fp)
{
    if (fp >= FLT_MIN && fp <= FLT_MAX)
        return 4;
    return 8;
}

static int
get_resource_len(const struct sol_lwm2m_resource *resource, size_t *len)
{
    switch (resource->type) {
    case SOL_LWM2M_RESOURCE_TYPE_STRING:
    case SOL_LWM2M_RESOURCE_TYPE_OPAQUE:
        *len = resource->data.bytes.len;
        return 0;
    case SOL_LWM2M_RESOURCE_TYPE_INT:
    case SOL_LWM2M_RESOURCE_TYPE_TIME:
        *len = get_int_size(resource->data.integer);
        return 0;
    case SOL_LWM2M_RESOURCE_TYPE_BOOLEAN:
        *len = 1;
        return 0;
    case SOL_LWM2M_RESOURCE_TYPE_FLOAT:
        *len = get_double_size(resource->data.fp);
        return 0;
    case SOL_LWM2M_RESOURCE_TYPE_OBJ_LINK:
        *len = OBJ_LINK_LEN;
        return 0;
    default:
        return -EINVAL;
    }
}

static void
swap_bytes(uint8_t *to_swap, size_t len, bool machine)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return;
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t *original;
    size_t i, j;

    original = alloca(len);
    memcpy(original, to_swap, len);
    if (machine) {
        for (i = 0, j = len - 1; i < len; i++, j--)
            to_swap[i] = original[j];
    } else {
        for (i = len - 1, j = 0; i < len; i--, j++)
            to_swap[i] = original[j];
    }
#else
#error "Unknown byte order"
#endif
}

static void
to_machine_order(uint8_t *to_swap, size_t len)
{
    swap_bytes(to_swap, len, true);
}

static void
to_network_order(uint8_t *to_swap, size_t len)
{
    swap_bytes(to_swap, len, false);
}

static int
add_float_resource(struct sol_buffer *buf, double fp, size_t len)
{
    uint8_t *bytes = NULL;
    float f;
    double d;

    if (len == 4) {
        f = (float)fp;
        to_network_order((uint8_t *)&f, len);
        bytes = (uint8_t *)&f;
    } else {
        d = fp;
        to_network_order((uint8_t *)&d, len);
        bytes = (uint8_t *)&d;
    }

    return sol_buffer_append_bytes(buf, bytes, len);
}

static int
add_resource_bytes_to_buffer(struct sol_buffer *buf,
    const struct sol_lwm2m_resource *resource, size_t len)
{
    uint8_t b;
    uint8_t bytes[sizeof(int64_t)];
    size_t i, j;

    switch (resource->type) {
    case SOL_LWM2M_RESOURCE_TYPE_STRING:
    case SOL_LWM2M_RESOURCE_TYPE_OPAQUE:
        return sol_buffer_append_slice(buf, resource->data.bytes);
    case SOL_LWM2M_RESOURCE_TYPE_INT:
    case SOL_LWM2M_RESOURCE_TYPE_TIME:
    case SOL_LWM2M_RESOURCE_TYPE_OBJ_LINK:
        for (i = 0, j = len - 1; i < len; i++, j--)
            bytes[j] = (resource->data.integer >> (8 * i)) & 255;
        return sol_buffer_append_bytes(buf, bytes, len);
    case SOL_LWM2M_RESOURCE_TYPE_BOOLEAN:
        b = resource->data.integer != 0 ? 1 : 0;
        return sol_buffer_append_bytes(buf, (uint8_t *)&b, 1);
    case SOL_LWM2M_RESOURCE_TYPE_FLOAT:
        return add_float_resource(buf, resource->data.fp, len);
    default:
        return -EINVAL;
    }
}

static int
set_packet_payload(struct sol_coap_packet *pkt,
    const uint8_t *data, uint16_t len)
{
    int r;
    uint16_t payload_len;
    uint8_t *payload;

    r = sol_coap_packet_get_payload(pkt, &payload, &payload_len);
    SOL_INT_CHECK(r, < 0, r);
    SOL_INT_CHECK(len, > payload_len, -ENOMEM);

    memcpy(payload, data, len);
    return sol_coap_packet_set_payload_used(pkt, len);
}

static int
setup_tlv_header(struct sol_lwm2m_resource *resource, struct sol_buffer *buf,
    bool append_header_to_buffer)
{
    int r;
    uint8_t tlv_data[6];
    size_t tlv_data_len, data_len;

    LWM2M_RESOURCE_CHECK_API(*resource, -EINVAL);
    tlv_data_len = 2;

    r = get_resource_len(resource, &data_len);
    SOL_INT_CHECK(r, < 0, r);

    tlv_data[0] = SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE;

    if (resource->id > UINT8_MAX) {
        tlv_data[0] |= ID_HAS_16BITS_MASK;
        tlv_data[1] = (resource->id >> 8) & 255;
        tlv_data[2] = resource->id & 255;
        tlv_data_len++;
    } else
        tlv_data[1] = resource->id;

    if (data_len <= 7)
        tlv_data[0] |= data_len;
    else if (data_len <= UINT8_MAX) {
        tlv_data[tlv_data_len++] = data_len;
        tlv_data[0] |= LEN_IS_8BITS_MASK;
    } else if (data_len <= UINT16_MAX) {
        tlv_data[tlv_data_len++] = (data_len >> 8) & 255;
        tlv_data[tlv_data_len++] = data_len & 255;
        tlv_data[0] |= LEN_IS_16BITS_MASK;
    } else if (data_len <= UINT24_BITS) {
        tlv_data[tlv_data_len++] = (data_len >> 16) & 255;
        tlv_data[tlv_data_len++] = (data_len >> 8) & 255;
        tlv_data[tlv_data_len++] = data_len & 255;
        tlv_data[0] |= LEN_IS_24BITS_MASK;
    }

    if (append_header_to_buffer) {
        r = sol_buffer_append_bytes(buf, tlv_data, tlv_data_len);
        SOL_INT_CHECK(r, < 0, r);
    }

    r = add_resource_bytes_to_buffer(buf, resource, data_len);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API int
sol_lwm2m_resource_to_tlv(struct sol_lwm2m_resource *resource,
    struct sol_lwm2m_tlv *tlv)
{
    int r;

    SOL_NULL_CHECK(resource, -EINVAL);
    SOL_NULL_CHECK(tlv, -EINVAL);

    tlv->type = SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE;
    tlv->id = resource->id;
    sol_buffer_init(&tlv->content);

    r = setup_tlv_header(resource, &tlv->content, false);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    SOL_SET_API_VERSION(tlv->api_version = SOL_LWM2M_TLV_API_VERSION; )

    return 0;

err_exit:
    sol_buffer_fini(&tlv->content);
    return r;
}

static int
resources_to_tlv(struct sol_lwm2m_resource *resources,
    size_t len, struct sol_buffer *tlvs)
{
    int r;
    size_t i;

    for (i = 0; i < len; i++) {
        r = setup_tlv_header(&resources[i], tlvs, true);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    return 0;

exit:
    return r;
}

static int
setup_coap_packet(sol_coap_method_t method,
    sol_coap_msgtype_t type, const char *objects_path, const char *path,
    uint8_t *obs, int64_t *token, struct sol_lwm2m_resource *resources,
    size_t len,
    const char *execute_args,
    struct sol_coap_packet **pkt)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    struct sol_buffer tlvs =
        SOL_BUFFER_INIT_FLAGS(NULL, 0, SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    struct sol_random *random;
    uint16_t content_type, content_len = 0;
    const uint8_t *content_data;
    int64_t t;
    int r;

    random = sol_random_new(SOL_RANDOM_DEFAULT, 0);
    SOL_NULL_CHECK(random, -ENOMEM);

    *pkt = sol_coap_packet_request_new(method, type);
    r = -ENOMEM;
    SOL_NULL_CHECK_GOTO(*pkt, exit);

    if (!sol_random_get_int64(random, &t)) {
        SOL_WRN("Could not generate a random number");
        r = -ECANCELED;
        goto exit;
    }

    if (!sol_coap_header_set_token(*pkt, (uint8_t *)&t,
        (uint8_t)sizeof(int64_t))) {
        SOL_WRN("Could not set the token");
        r = -ECANCELED;
        goto exit;
    }

    if (token)
        *token = t;

    if (obs) {
        r = sol_coap_add_option(*pkt, SOL_COAP_OPTION_OBSERVE,
            obs, sizeof(uint8_t));
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    if (objects_path) {
        r = sol_buffer_append_slice(&buf,
            sol_str_slice_from_str(objects_path));
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(path));
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = sol_coap_packet_add_uri_path_option(*pkt, buf.data);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    if (execute_args) {
        size_t str_len;
        content_type = SOL_LWM2M_CONTENT_TYPE_TEXT;
        content_data = (const uint8_t *)execute_args;
        str_len = strlen(execute_args);
        r = -ENOMEM;
        SOL_INT_CHECK_GOTO(str_len, >= UINT16_MAX, exit);
        content_len = str_len;
    } else if (resources) {
        content_type = SOL_LWM2M_CONTENT_TYPE_TLV;
        r = resources_to_tlv(resources, len, &tlvs);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
        r = -ENOMEM;
        SOL_INT_CHECK_GOTO(tlvs.used, >= UINT16_MAX, exit);
        content_data = tlvs.data;
        content_len = tlvs.used;
    }

    if (content_len > 0) {
        r = sol_coap_add_option(*pkt, SOL_COAP_OPTION_CONTENT_FORMAT,
            &content_type, sizeof(content_type));
        SOL_INT_CHECK_GOTO(r, < 0, exit);

        r = set_packet_payload(*pkt, content_data, content_len);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = 0;

exit:
    if (r < 0)
        sol_coap_packet_unref(*pkt);
    sol_buffer_fini(&tlvs);
    sol_buffer_fini(&buf);
    sol_random_del(random);
    return r;
}

static void
extract_content(struct sol_coap_packet *req, uint8_t *code,
    enum sol_lwm2m_content_type *type, struct sol_str_slice *content)
{
    uint16_t len;
    uint8_t *buf;
    int r;

    *code = sol_coap_header_get_code(req);

    if (*code == SOL_COAP_RSPCODE_CONTENT) {
        const void *format;
        format = sol_coap_find_first_option(req,
            SOL_COAP_OPTION_CONTENT_FORMAT, &len);
        if (!format) {
            SOL_WRN("Could not get the response content type");
        } else
            memcpy(type, format, sizeof(len));
        if (sol_coap_packet_has_payload(req)) {
            r = sol_coap_packet_get_payload(req, &buf, &len);
            if (r < 0) {
                SOL_WRN("Could not get the reposnse content");
            } else {
                content->len = len;
                content->data = (const char *)buf;
            }
        }
    }
}

static bool
observation_request_reply(struct sol_coap_server *coap_server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr,
    void *data)
{
    struct observer_entry *entry = data;
    struct sol_monitors_entry *m;
    struct sol_str_slice content = SOL_STR_SLICE_EMPTY;
    enum sol_lwm2m_content_type type = SOL_LWM2M_CONTENT_TYPE_TEXT;
    uint16_t i;
    uint8_t code = SOL_COAP_RSPCODE_GATEWAY_TIMEOUT;
    bool keep_alive = true;

    if (!cliaddr && !req) {
        //Cancel observation
        if (entry->removed) {
            remove_observer_entry(&entry->server->observers, entry);
            return false;
        }
        SOL_WRN("Could not complete the observation request on client:%s"
            " path:%s", entry->path, entry->cinfo->name);
        keep_alive = false;
    } else {
        extract_content(req, &code, &type, &content);
        send_ack_if_needed(coap_server, req, cliaddr);
    }

    SOL_MONITORS_WALK (&entry->monitors, m, i)
        ((sol_lwm2m_server_content_cb)m->cb)(entry->server, entry->cinfo,
            entry->path, code, type, content, (void *)m->data);

    return keep_alive;
}

SOL_API int
sol_lwm2m_server_add_observer(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path, sol_lwm2m_server_content_cb cb, void *data)
{
    struct observer_entry *entry;
    struct sol_coap_packet *pkt;
    uint8_t obs = 0;
    int r;
    bool send_msg = false;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);

    entry = find_observer_entry(&server->observers, client, path);

    if (!entry) {
        send_msg = true;
        r = observer_entry_new(server, client, path, &entry);
        SOL_INT_CHECK(r, < 0, r);
    }

    r = observer_entry_add_monitor(entry, cb, data);
    SOL_INT_CHECK(r, < 0, r);

    if (!send_msg)
        return 0;

    r = setup_coap_packet(SOL_COAP_METHOD_GET, SOL_COAP_TYPE_CON,
        client->objects_path, path, &obs, &entry->token, NULL, 0, NULL, &pkt);
    SOL_INT_CHECK(r, < 0, r);

    return sol_coap_send_packet_with_reply(server->coap, pkt, &client->cliaddr,
        observation_request_reply, entry);
}

SOL_API int
sol_lwm2m_server_del_observer(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    sol_lwm2m_server_content_cb cb, void *data)
{
    struct observer_entry *entry;
    int r;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);

    entry = find_observer_entry(&server->observers, client, path);
    SOL_NULL_CHECK(entry, -ENOENT);

    r = observer_entry_del_monitor(entry, cb, data);
    SOL_INT_CHECK(r, < 0, r);

    if (entry->monitors.entries.len)
        return 0;

    entry->removed = true;

    return sol_coap_unobserve_server(server->coap, &entry->cinfo->cliaddr,
        (uint8_t *)&entry->token, sizeof(entry->token));
}


static void
tlv_clear(struct sol_lwm2m_tlv *tlv)
{
    LWM2M_TLV_CHECK_API(*tlv);
    sol_buffer_fini(&tlv->content);
}

SOL_API void
sol_lwm2m_tlv_clear(struct sol_lwm2m_tlv *tlv)
{
    SOL_NULL_CHECK(tlv);
    tlv_clear(tlv);
}

SOL_API void
sol_lwm2m_tlv_array_clear(struct sol_vector *tlvs)
{
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;

    SOL_NULL_CHECK(tlvs);

    SOL_VECTOR_FOREACH_IDX (tlvs, tlv, i)
        tlv_clear(tlv);
    sol_vector_clear(tlvs);
}

SOL_API int
sol_lwm2m_parse_tlv(const struct sol_str_slice content, struct sol_vector *out)
{
    size_t i, offset;
    struct sol_lwm2m_tlv *tlv;
    int r;

    SOL_NULL_CHECK(out, -EINVAL);

    sol_vector_init(out, sizeof(struct sol_lwm2m_tlv));

    for (i = 0; i < content.len;) {
        struct sol_str_slice tlv_content;
        tlv = sol_vector_append(out);
        r = -ENOMEM;
        SOL_NULL_CHECK_GOTO(tlv, err_exit);

        sol_buffer_init(&tlv->content);

        SOL_SET_API_VERSION(tlv->api_version = SOL_LWM2M_TLV_API_VERSION; )

        tlv->type = content.data[i] & TLV_TYPE_MASK;

        if ((content.data[i] & TLV_ID_SIZE_MASK) != TLV_ID_SIZE_MASK) {
            tlv->id = content.data[i + 1];
            offset = i + 2;
        } else {
            tlv->id = (content.data[i + 1] << 8) | content.data[i + 2];
            offset = i + 3;
        }

        SOL_INT_CHECK_GOTO(offset, >= content.len, err_would_overflow);

        switch (content.data[i] & TLV_CONTENT_LENGTH_MASK) {
        case LENGTH_SIZE_24_BITS:
            tlv_content.len = (content.data[offset] << 16) |
                (content.data[offset + 1] << 8) | content.data[offset + 2];
            offset += 3;
            break;
        case LENGTH_SIZE_16_BITS:
            tlv_content.len |= (content.data[offset] << 8) |
                content.data[offset + 1];
            offset += 2;
            break;
        case LENGTH_SIZE_8_BITS:
            tlv_content.len = content.data[offset];
            offset++;
            break;
        default:
            tlv_content.len = content.data[i] & TLV_CONTENT_LENGHT_CUSTOM_MASK;
        }

        SOL_INT_CHECK_GOTO(offset, >= content.len, err_would_overflow);

        tlv_content.data = content.data + offset;

        r = sol_buffer_append_slice(&tlv->content, tlv_content);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        SOL_DBG("tlv type: %u, ID: %" PRIu16 ", Size: %zu, Content: %.*s",
            tlv->type, tlv->id, tlv_content.len,
            SOL_STR_SLICE_PRINT(tlv_content));

        if (tlv->type != SOL_LWM2M_TLV_TYPE_MULTIPLE_RESOURCES &&
            tlv->type != SOL_LWM2M_TLV_TYPE_OBJECT_INSTANCE)
            i += ((offset - i) + tlv_content.len);
        else
            i += (offset - i);
    }

    return 0;

err_would_overflow:
    r = -EOVERFLOW;
err_exit:
    sol_lwm2m_tlv_array_clear(out);
    return r;
}

static int
is_resource(struct sol_lwm2m_tlv *tlv)
{
    if (tlv->type != SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE &&
        tlv->type != SOL_LWM2M_TLV_TYPE_RESOURCE_INSTANCE)
        return -EINVAL;
    return 0;
}

SOL_API int
sol_lwl2m_tlv_to_int(struct sol_lwm2m_tlv *tlv, int64_t *value)
{
    size_t i;
    const char *buf;

    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(*tlv, -EINVAL);

    if (tlv->content.used != 1 &&
        tlv->content.used != 2 &&
        tlv->content.used != 4 &&
        tlv->content.used != 8)
        return -EINVAL;

    buf = ((char *)tlv->content.data);

    //Remove the sign bit.
    for (i = 1, *value = buf[0] & REMOVE_SIGN_BIT_MASK;
        i < tlv->content.used; i++)
        *value = ((*value) << 8) | (buf[i] & 255);

    //Apply sign bit
    if ((buf[0] & SIGN_BIT_MASK) == SIGN_BIT_MASK)
        (*value) *= -1;

    SOL_DBG("TLV has integer data. Value: %" PRId64 "", *value);
    return 0;
}

SOL_API int
sol_lwl2m_tlv_to_bool(struct sol_lwm2m_tlv *tlv, bool *value)
{
    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(*tlv, -EINVAL);
    SOL_INT_CHECK(tlv->content.used, != 1, -EINVAL);

    *value = (((char *)tlv->content.data))[0] == 1;
    SOL_DBG("TLV data as bool: %d", (int)*value);
    return 0;
}

SOL_API int
sol_lwl2m_tlv_to_float(struct sol_lwm2m_tlv *tlv, double *value)
{
    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(*tlv, -EINVAL);

    if (tlv->content.used == 4) {
        float f;
        memcpy(&f, tlv->content.data, sizeof(float));
        to_machine_order((uint8_t *)&f, sizeof(float));
        *value = f;
    } else if (tlv->content.used == 8) {
        memcpy(value, tlv->content.data, sizeof(double));
        to_machine_order((uint8_t *)value, sizeof(double));
    } else
        return -EINVAL;

    SOL_DBG("TLV has float data. Value: %g", *value);
    return 0;
}

SOL_API int
sol_lwm2m_tlv_to_obj_link(struct sol_lwm2m_tlv *tlv,
    uint16_t *object_id, uint16_t *instance_id)
{
    int32_t i = 0;

    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(object_id, -EINVAL);
    SOL_NULL_CHECK(instance_id, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(*tlv, -EINVAL);
    SOL_INT_CHECK(tlv->content.used, != OBJ_LINK_LEN, -EINVAL);


    memcpy(&i, tlv->content.data, OBJ_LINK_LEN);
    to_machine_order((uint8_t *)&i, OBJ_LINK_LEN);
    *object_id = (i >> 16) & 0xFFFF;
    *instance_id = i & 0xFFFF;

    SOL_DBG("TLV has object link value. Object id:%" PRIu16
        "  Instance id:%" PRIu16 "", *object_id, *instance_id);
    return 0;
}

SOL_API int
sol_lwm2m_tlv_get_bytes(struct sol_lwm2m_tlv *tlv, uint8_t **bytes, uint16_t *len)
{
    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(bytes, -EINVAL);
    SOL_NULL_CHECK(len, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(*tlv, -EINVAL);

    *bytes = (uint8_t *)tlv->content.data;
    *len = tlv->content.used;

    return 0;
}

SOL_API int
sol_lwm2m_resource_init(struct sol_lwm2m_resource *resource,
    uint16_t id, enum sol_lwm2m_resource_type type, ...)
{
    va_list ap;
    int r = 0;

    SOL_NULL_CHECK(resource, -EINVAL);
    SOL_INT_CHECK(type, == SOL_LWM2M_RESOURCE_TYPE_NONE, -EINVAL);

    va_start(ap, type);

    resource->id = id;
    resource->type = type;

    switch (resource->type) {
    case SOL_LWM2M_RESOURCE_TYPE_STRING:
        resource->data.bytes = sol_str_slice_from_str(va_arg(ap, const char *));
        break;
    case SOL_LWM2M_RESOURCE_TYPE_OPAQUE:
        resource->data.bytes = va_arg(ap, struct sol_str_slice);
        break;
    case SOL_LWM2M_RESOURCE_TYPE_FLOAT:
        resource->data.fp = va_arg(ap, double);
        break;
    case SOL_LWM2M_RESOURCE_TYPE_INT:
    case SOL_LWM2M_RESOURCE_TYPE_TIME:
        resource->data.integer = va_arg(ap, int64_t);
        break;
    case SOL_LWM2M_RESOURCE_TYPE_BOOLEAN:
        resource->data.integer = va_arg(ap, int);
        break;
    case SOL_LWM2M_RESOURCE_TYPE_OBJ_LINK:
        resource->data.integer = (uint16_t)va_arg(ap, int);
        resource->data.integer = (resource->data.integer << 16) |
            (uint16_t)va_arg(ap, int);
        break;
    default:
        r = -EINVAL;
        SOL_WRN("Unknown resource type '%d'", resource->type);
        break;
    }

    SOL_SET_API_VERSION(resource->api_version = SOL_LWM2M_RESOURCE_API_VERSION; )
    va_end(ap);
    return r;
}
