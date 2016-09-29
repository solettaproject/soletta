/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <errno.h>
#include <float.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <ctype.h>

#define SOL_LOG_DOMAIN &_lwm2m_server_domain

#include "sol-log-internal.h"
#include "sol-util-internal.h"
#include "sol-list.h"
#include "sol-socket.h"
#include "sol-lwm2m.h"
#include "sol-lwm2m-common.h"
#include "sol-lwm2m-server.h"
#include "sol-lwm2m-security.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-monitors.h"
#include "sol-random.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util.h"
#include "sol-http.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_lwm2m_server_domain, "lwm2m-server");

struct sol_lwm2m_client_info {
    struct sol_ptr_vector objects;
    bool secure;
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

enum management_type {
    MANAGEMENT_DELETE,
    MANAGEMENT_READ,
    MANAGEMENT_CREATE,
    MANAGEMENT_WRITE,
    MANAGEMENT_EXECUTE
};

struct management_ctx {
    enum management_type type;
    struct sol_lwm2m_server *server;
    struct sol_lwm2m_client_info *cinfo;
    char *path;
    void *cb;
    const void *data;
};

static bool lifetime_server_timeout(void *data);

static void
dispatch_registration_event(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event)
{
    uint16_t i;
    struct sol_monitors_entry *m;

    SOL_MONITORS_WALK (&server->registration, m, i)
        ((void (*)(void *, struct sol_lwm2m_server *,
        struct sol_lwm2m_client_info *,
        enum sol_lwm2m_registration_event))m->cb)((void *)m->data, server,
            cinfo, event);
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
remove_all_observer_entries_from_client(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo)
{
    uint16_t i;
    int64_t token;
    struct observer_entry *entry;

    SOL_PTR_VECTOR_FOREACH_IDX (&server->observers, entry, i) {
        if (entry->cinfo == cinfo) {
            token = entry->token;
            entry->removed = true;
            sol_coap_unobserve_by_token(
                cinfo->secure ? cinfo->server->dtls_server : cinfo->server->coap,
                &cinfo->cliaddr,
                (uint8_t *)&token, sizeof(token));
        }
    }
}

static void
remove_client(struct sol_lwm2m_client_info *cinfo, bool del)
{
    int r = 0;

    remove_all_observer_entries_from_client(cinfo->server, cinfo);

    r = sol_ptr_vector_remove(&cinfo->server->clients, cinfo);
    if (r < 0)
        SOL_WRN("Could not remove the client %s from the clients list",
            cinfo->name);
    r = sol_coap_server_unregister_resource(
        cinfo->secure ? cinfo->server->dtls_server : cinfo->server->coap,
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

static int
fill_client_objects(struct sol_lwm2m_client_info *cinfo,
    struct sol_coap_packet *req, bool update)
{
    struct sol_lwm2m_client_object *cobject;
    struct sol_str_slice content, *object;
    struct sol_vector objects;
    struct sol_buffer *buf;
    uint16_t *instance;
    bool has_content;
    size_t offset;
    uint16_t i;
    int r;

#define TO_INT(_data, _endptr, _len, _i, _label) \
    _i = sol_util_strtol_n(_data, &_endptr, _len, 10); \
    if (_endptr == _data || errno != 0 ) { \
        SOL_WRN("Could not convert object to int. (%.*s)", \
            SOL_STR_SLICE_PRINT(*object)); \
        r = -EINVAL; \
        goto _label; \
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

    r = sol_coap_packet_get_payload(req, &buf, &offset);
    SOL_INT_CHECK(r, < 0, r);
    content.data = sol_buffer_at(buf, offset);
    content.len = buf->used - offset;

    SOL_DBG("Register payload content: %.*s", (int)content.len, content.data);
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

        TO_INT(object->data, endptr, object->len, id, err_exit);

        cobject = find_client_object_by_id(&cinfo->objects, id);

        if (!cobject) {
            cobject = malloc(sizeof(struct sol_lwm2m_client_object));
            r = -ENOMEM;
            SOL_NULL_CHECK_GOTO(cobject, err_exit);
            r = sol_ptr_vector_append(&cinfo->objects, cobject);
            if (r < 0) {
                SOL_WRN("Could not append the object id:%" PRIu16
                    " to the object array", id);
                free(cobject);
                goto err_exit;
            }
            sol_ptr_vector_init(&cobject->instances);
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

        instance = malloc(sizeof(uint16_t));
        r = -ENOMEM;
        SOL_NULL_CHECK_GOTO(instance, err_exit);

        TO_INT(object->data, endptr, object->len, *instance,
            err_instance_to_int);

        if (*instance == UINT16_MAX) {
            SOL_WRN("The instance id value: %" PRIu16 " must not be used!",
                UINT16_MAX);
            r = -EPERM;
            goto err_instance_to_int;
        }

        r = sol_ptr_vector_append(&cobject->instances, instance);
        if (r < 0) {
            SOL_WRN("Could not append the instance /%" PRIu16 "/%" PRIu16
                " to the instance array", cobject->id, *instance);
            goto err_instance_to_int;
        }
    }

    sol_vector_clear(&objects);
    return 0;

err_instance_to_int:
    free(instance);
err_exit:
    sol_vector_clear(&objects);
    client_objects_clear(&cinfo->objects);
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
            cinfo->name = sol_str_slice_to_str(value);
            SOL_NULL_CHECK_GOTO(cinfo->name, err_cinfo_prop);
        } else if (sol_str_slice_str_eq(key, "lt")) {
            char *endptr;
            cinfo->lifetime = sol_util_strtoul_n(value.data, &endptr,
                value.len, 10);
            if (endptr == value.data || errno != 0) {
                SOL_WRN("Could not convert the lifetime to integer."
                    " Lifetime: %.*s", SOL_STR_SLICE_PRINT(value));
                r = -EINVAL;
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
    uint32_t smallest_remaining, remaining, lf = 0;
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

    smallest_remaining = UINT32_MAX;
    now = time(NULL);
    SOL_PTR_VECTOR_FOREACH_IDX (&server->clients, cinfo, i) {
        remaining = cinfo->lifetime - (now - cinfo->register_time);
        if (remaining < smallest_remaining) {
            smallest_remaining = remaining;
            lf = cinfo->lifetime;
        }
    }

    //Set to NULL in case we fail.
    server->lifetime_ctx.timeout = NULL;
    /*
       When a client is registered, it tells the server what is its lifetime.
       If the server's timeout is registered using the exactly same amount,
       there's a high chance that the server will end up removing a client from
       my list, because the message will take some time until it arrives
       from the network. In order to reduce the change from happening,
       the server will add 2 seconds to smallest_remaining.
     */
    r = sol_util_uint32_mul(smallest_remaining + 2, 1000, &smallest_remaining);
    SOL_INT_CHECK(r, < 0, r);
    server->lifetime_ctx.timeout = sol_timeout_add(smallest_remaining,
        lifetime_server_timeout, server);
    SOL_NULL_CHECK(server->lifetime_ctx.timeout, -ENOMEM);
    server->lifetime_ctx.lifetime = lf;
    return 0;
}

static bool
lifetime_server_timeout(void *data)
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
update_client(void *data, struct sol_coap_server *coap,
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
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

    r = sol_coap_header_set_code(response, SOL_COAP_RESPONSE_CODE_CHANGED);
    SOL_INT_CHECK_GOTO(r, < 0, err_update);
    return sol_coap_send_packet(coap, response, cliaddr);

err_update:
    sol_coap_header_set_code(response, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);
    sol_coap_send_packet(coap, response, cliaddr);
    return r;
}

static int
delete_client(void *data, struct sol_coap_server *coap,
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_lwm2m_client_info *cinfo = data;
    struct sol_coap_packet *response;
    int r;

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

    r = sol_coap_header_set_code(response, SOL_COAP_RESPONSE_CODE_DELETED);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    return sol_coap_send_packet(coap, response, cliaddr);

err:
    sol_coap_packet_unref(response);
    return r;
}

static int
generate_location(char **location)
{
    SOL_BUFFER_DECLARE_STATIC(uuid, 33);
    int r;

    r = sol_util_uuid_gen(false, false, &uuid);
    SOL_INT_CHECK(r, < 0, r);
    *location = strndup(uuid.data, DEFAULT_LOCATION_PATH_SIZE);
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
       Current spec says that the client update should be handled using
       the post method, however some old clients still uses put.
     */
    (*cinfo)->resource.post = update_client;
    (*cinfo)->resource.put = update_client;
    (*cinfo)->server = server;
    sol_ptr_vector_init(&(*cinfo)->objects);
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
registration_request(void *data, struct sol_coap_server *coap,
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_lwm2m_client_info *cinfo, *old_cinfo;
    struct sol_lwm2m_server *server = data;
    struct sol_coap_packet *response;
    int r;

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

    //Register CoAP Resource at the CoAP Server in which the Registration Request
    // was made; which can be either server->coap or server->dtls_server
    r = sol_coap_server_register_resource(coap, &cinfo->resource,
        cinfo);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_del_client);

    cinfo->secure = sol_coap_server_is_secure(coap);

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

    r = sol_coap_header_set_code(response, SOL_COAP_RESPONSE_CODE_CREATED);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_unregister);

    SOL_DBG("Client %s registered. Location: %s, SMS: %s, binding: %u,"
        " lifetime: %" PRIu32 " objects paths: %s%s",
        cinfo->name, cinfo->location, cinfo->sms,
        cinfo->binding, cinfo->lifetime, cinfo->objects_path,
        cinfo->secure ? " (secure)" : "");

    r = sol_coap_send_packet(coap, response, cliaddr);
    dispatch_registration_event(server, cinfo,
        SOL_LWM2M_REGISTRATION_EVENT_REGISTER);
    return r;

err_exit_unregister:
    if (sol_coap_server_unregister_resource(coap, &cinfo->resource) < 0)
        SOL_WRN("Could not unregister resource for client: %s", cinfo->name);
err_exit_del_client:
    client_info_del(cinfo);
err_exit:
    sol_coap_header_set_code(response, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);
    sol_coap_send_packet(coap, response, cliaddr);
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
observer_entry_free(struct observer_entry *entry)
{
    sol_monitors_clear(&entry->monitors);
    free(entry->path);
    free(entry);
}

static void
remove_observer_entry(struct sol_ptr_vector *entries,
    struct observer_entry *entry)
{
    int r;

    r = sol_ptr_vector_del_element(entries, entry);
    SOL_INT_CHECK(r, < 0);
    observer_entry_free(entry);
}

static struct observer_entry *
find_observer_entry(struct sol_ptr_vector *entries,
    struct sol_lwm2m_client_info *cinfo, const char *path)
{
    uint16_t i;
    struct observer_entry *entry;

    SOL_PTR_VECTOR_FOREACH_IDX (entries, entry, i) {
        if (entry->cinfo == cinfo && streq(path, entry->path))
            return entry;
    }

    return NULL;
}

static int
observer_entry_new(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo, const char *path,
    struct observer_entry **entry)
{
    int r = -ENOMEM;

    *entry = calloc(1, sizeof(struct observer_entry));
    SOL_NULL_CHECK(*entry, r);

    (*entry)->path = strdup(path);
    SOL_NULL_CHECK_GOTO((*entry)->path, err_exit);

    sol_monitors_init(&(*entry)->monitors, NULL);
    (*entry)->server = server;
    (*entry)->cinfo = cinfo;

    r = sol_ptr_vector_append(&server->observers, *entry);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    return 0;
err_exit:
    free((*entry)->path);
    free(*entry);
    return r;
}

static int
observer_entry_add_monitor(struct observer_entry *entry,
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content),
    const void *data)
{
    SOL_NULL_CHECK(entry, -EINVAL);

    return add_to_monitors(&entry->monitors, (sol_monitors_cb_t)cb, data);
}

static int
observer_entry_del_monitor(struct observer_entry *entry,
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content),
    const void *data)
{
    SOL_NULL_CHECK(entry, -EINVAL);

    return remove_from_monitors(&entry->monitors, (sol_monitors_cb_t)cb, data);
}

SOL_API struct sol_lwm2m_server *
sol_lwm2m_server_new(uint16_t coap_port, uint16_t num_sec_modes, ...)
{
    struct sol_lwm2m_server *server;
    struct sol_network_link_addr servaddr_coap = { .family = SOL_NETWORK_FAMILY_INET6,
                                                   .port = coap_port };
    struct sol_network_link_addr servaddr_dtls = { .family = SOL_NETWORK_FAMILY_INET6,
                                                   .port = SOL_LWM2M_DEFAULT_SERVER_PORT_DTLS };
    int r, dtls_port;
    struct sol_lwm2m_security_psk **known_psks = NULL, *cli_psk;
    struct sol_lwm2m_security_rpk *my_rpk = NULL;
    struct sol_blob **known_pub_keys = NULL, *cli_pub_key;
    enum sol_lwm2m_security_mode *sec_modes = NULL;
    enum sol_socket_dtls_cipher *cipher_suites = NULL;
    uint16_t i, j;
    va_list ap;

    SOL_LOG_INTERNAL_INIT_ONCE;

    va_start(ap, num_sec_modes);

    if (num_sec_modes > 0) {
        cipher_suites = calloc(num_sec_modes, sizeof(enum sol_socket_dtls_cipher));
        SOL_NULL_CHECK_GOTO(cipher_suites, err_va_list);

        sec_modes = calloc(num_sec_modes, sizeof(enum sol_lwm2m_security_mode));
        SOL_NULL_CHECK_GOTO(sec_modes, err_cipher_suites);

        dtls_port = va_arg(ap, int);
        SOL_INT_CHECK_GOTO(dtls_port, < 0, err_sec_modes);
        servaddr_dtls.port = dtls_port;

        for (i = 0; i < num_sec_modes; i++) {
            sec_modes[i] = va_arg(ap, enum sol_lwm2m_security_mode);
            SOL_EXP_CHECK_GOTO(sec_mode_is_repeated(sec_modes[i], sec_modes, i), err_sec_modes);

            switch (sec_modes[i]) {
            case SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY:
                known_psks = va_arg(ap, struct sol_lwm2m_security_psk **);
                SOL_NULL_CHECK_GOTO(known_psks, err_sec_modes);

                cipher_suites[i] = SOL_SOCKET_DTLS_CIPHER_PSK_AES128_CCM8;
                break;
            case SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY:
                my_rpk = va_arg(ap, struct sol_lwm2m_security_rpk *);
                SOL_NULL_CHECK_GOTO(my_rpk, err_sec_modes);
                known_pub_keys = va_arg(ap, struct sol_blob **);
                SOL_NULL_CHECK_GOTO(known_pub_keys, err_sec_modes);

                cipher_suites[i] = SOL_SOCKET_DTLS_CIPHER_ECDHE_ECDSA_AES128_CCM8;
                break;
            case SOL_LWM2M_SECURITY_MODE_CERTIFICATE:
                SOL_WRN("Certificate security mode is not supported yet.");
                goto err_sec_modes;
            case SOL_LWM2M_SECURITY_MODE_NO_SEC:
                SOL_WRN("NoSec Security Mode (No DTLS) was found."
                    "If DTLS should not be used, num_sec_modes should be 0");
                goto err_sec_modes;
            default:
                SOL_WRN("Unknown DTLS Security Mode: %d", sec_modes[i]);
                goto err_sec_modes;
            }
        }
    }

    server = calloc(1, sizeof(struct sol_lwm2m_server));
    SOL_NULL_CHECK_GOTO(server, err_sec_modes);

    server->coap = sol_coap_server_new(&servaddr_coap, false);
    SOL_NULL_CHECK_GOTO(server->coap, err_coap);

    if (num_sec_modes > 0) {
        for (i = 0; i < num_sec_modes; i++) {
            if (sec_modes[i] == SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY) {
                sol_vector_init(&server->known_psks, sizeof(sol_lwm2m_security_psk));

                for (j = 0; known_psks[j]; j++) {
                    cli_psk = sol_vector_append(&server->known_psks);
                    SOL_NULL_CHECK_GOTO(cli_psk, err_copy_keys);
                    cli_psk->id = sol_blob_ref(known_psks[j]->id);
                    cli_psk->key = sol_blob_ref(known_psks[j]->key);
                }
            } else if (sec_modes[i] == SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY) {
                sol_ptr_vector_init(&server->known_pub_keys);

                for (j = 0; known_pub_keys[j]; j++) {
                    r = sol_ptr_vector_append(&server->known_pub_keys,
                        sol_blob_ref(known_pub_keys[j]));
                    SOL_INT_CHECK_GOTO(r, < 0, err_copy_keys);
                }

                server->rpk_pair.private_key = sol_blob_ref(my_rpk->private_key);
                server->rpk_pair.public_key = sol_blob_ref(my_rpk->public_key);
            }
        }

        server->dtls_server = sol_coap_server_new_by_cipher_suites(&servaddr_dtls,
            cipher_suites, num_sec_modes);
        SOL_NULL_CHECK_GOTO(server->dtls_server, err_copy_keys);

        for (i = 0; i < num_sec_modes; i++) {
            server->security = sol_lwm2m_server_security_add(server, sec_modes[i]);
            if (!server->security) {
                SOL_ERR("Could not enable %s security mode for LWM2M Server",
                    get_security_mode_str(sec_modes[i]));
                goto err_security;
            } else {
                SOL_DBG("Using %s security mode", get_security_mode_str(sec_modes[i]));
            }
        }
    }

    sol_ptr_vector_init(&server->clients);
    sol_ptr_vector_init(&server->clients_to_delete);
    sol_ptr_vector_init(&server->observers);
    sol_monitors_init(&server->registration, NULL);

    r = sol_coap_server_register_resource(server->coap,
        &registration_interface, server);
    SOL_INT_CHECK_GOTO(r, < 0, err_security);

    if (server->security) {
        r = sol_coap_server_register_resource(server->dtls_server,
            &registration_interface, server);
        SOL_INT_CHECK_GOTO(r, < 0, err_register_dtls);
    }

    va_end(ap);

    free(sec_modes);
    free(cipher_suites);

    return server;

err_register_dtls:
    if (sol_coap_server_unregister_resource(server->coap, &registration_interface) < 0)
        SOL_WRN("Could not unregister resource for"
            " Registration Interface at insecure CoAP Server");
err_security:
    sol_coap_server_unref(server->dtls_server);
    sol_lwm2m_server_security_del(server->security);
err_copy_keys:
    sol_coap_server_unref(server->coap);

    for (i = 0; i < num_sec_modes; i++) {
        if (sec_modes[i] == SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY) {
            SOL_VECTOR_FOREACH_IDX (&server->known_psks, cli_psk, j) {
                sol_blob_unref(cli_psk->id);
                sol_blob_unref(cli_psk->key);
            }
            sol_vector_clear(&server->known_psks);
        } else if (sec_modes[i] == SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY) {
            SOL_PTR_VECTOR_FOREACH_IDX (&server->known_pub_keys, cli_pub_key, j)
                sol_blob_unref(cli_pub_key);
            sol_ptr_vector_clear(&server->known_pub_keys);

            sol_blob_unref(server->rpk_pair.private_key);
            sol_blob_unref(server->rpk_pair.public_key);
        }
    }
err_coap:
    free(server);
err_sec_modes:
    free(sec_modes);
err_cipher_suites:
    free(cipher_suites);
err_va_list:
    va_end(ap);
    return NULL;
}

SOL_API void
sol_lwm2m_server_del(struct sol_lwm2m_server *server)
{
    uint16_t i;
    struct sol_lwm2m_client_info *cinfo;
    struct observer_entry *entry;
    struct sol_lwm2m_security_psk *cli_psk;
    struct sol_blob *cli_pub_key;

    SOL_NULL_CHECK(server);

    SOL_PTR_VECTOR_FOREACH_IDX (&server->observers, entry, i)
        entry->removed = true;

    sol_coap_server_unref(server->coap);

    if (server->security) {
        sol_coap_server_unref(server->dtls_server);

        if (sol_lwm2m_security_supports_security_mode(server->security,
            SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY)) {
            SOL_VECTOR_FOREACH_IDX (&server->known_psks, cli_psk, i) {
                sol_blob_unref(cli_psk->id);
                sol_blob_unref(cli_psk->key);
            }
            sol_vector_clear(&server->known_psks);
        }
        if (sol_lwm2m_security_supports_security_mode(server->security,
            SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY)) {
            SOL_PTR_VECTOR_FOREACH_IDX (&server->known_pub_keys, cli_pub_key, i)
                sol_blob_unref(cli_pub_key);
            sol_ptr_vector_clear(&server->known_pub_keys);

            sol_blob_unref(server->rpk_pair.private_key);
            sol_blob_unref(server->rpk_pair.public_key);
        }

        sol_lwm2m_server_security_del(server->security);
    }

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
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event),
    const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);

    return add_to_monitors(&server->registration, (sol_monitors_cb_t)cb, data);
}

SOL_API int
sol_lwm2m_server_del_registration_monitor(struct sol_lwm2m_server *server,
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event),
    const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);

    return remove_from_monitors(&server->registration, (sol_monitors_cb_t)cb, data);
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
sol_lwm2m_client_info_get_sms_number(const struct sol_lwm2m_client_info *client)
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

SOL_API const struct sol_ptr_vector *
sol_lwm2m_client_info_get_objects(const struct sol_lwm2m_client_info *client)
{
    SOL_NULL_CHECK(client, NULL);

    return &client->objects;
}

static void
extract_content(struct sol_coap_packet *req, uint8_t *code,
    enum sol_lwm2m_content_type *type, struct sol_str_slice *content)
{
    struct sol_buffer *buf;
    size_t offset;
    int r;

    r = sol_coap_header_get_code(req, code);
    SOL_INT_CHECK(r, < 0);

    if (!sol_coap_packet_has_payload(req))
        return;

    r = sol_coap_packet_get_payload(req, &buf, &offset);
    SOL_INT_CHECK(r, < 0);
    content->len = buf->used - offset;
    content->data = sol_buffer_at(buf, offset);
    r = get_coap_int_option(req, SOL_COAP_OPTION_CONTENT_FORMAT,
        (uint16_t *)type);
    if (r < 0)
        SOL_INF("Content format not specified");
}

static bool
observation_request_reply(void *data, struct sol_coap_server *coap_server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr)
{
    struct observer_entry *entry = data;
    struct sol_monitors_entry *m;
    struct sol_str_slice content = SOL_STR_SLICE_EMPTY;
    enum sol_lwm2m_content_type type = SOL_LWM2M_CONTENT_TYPE_TEXT;
    uint16_t i;
    uint8_t code = SOL_COAP_RESPONSE_CODE_GATEWAY_TIMEOUT;
    bool keep_alive = true;

    if (!cliaddr && !req) {
        //Cancel observation
        if (entry->removed) {
            remove_observer_entry(&entry->server->observers, entry);
            return false;
        }
        SOL_WRN("Could not complete the observation request on client:%s"
            " path:%s", entry->cinfo->name, entry->path);
        keep_alive = false;
    } else {
        extract_content(req, &code, &type, &content);
        send_ack_if_needed(coap_server, req, cliaddr);
    }

    SOL_MONITORS_WALK (&entry->monitors, m, i)
        ((void (*)(void *, struct sol_lwm2m_server *,
        struct sol_lwm2m_client_info *,
        const char *,
        enum sol_coap_response_code,
        enum sol_lwm2m_content_type,
        struct sol_str_slice))m->cb)((void *)m->data, entry->server,
            entry->cinfo, entry->path, code, type, content);

    return keep_alive;
}

SOL_API int
sol_lwm2m_server_add_observer(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content),
    const void *data)
{
    enum sol_lwm2m_path_props props;
    struct observer_entry *entry;
    struct sol_coap_packet *pkt;
    uint8_t obs = 0;
    int r;
    bool send_msg = false;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);

    props = sol_lwm2m_common_get_path_props(path);
    SOL_EXP_CHECK(props < PATH_HAS_OBJECT, -EINVAL);

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

    r = setup_coap_packet(SOL_COAP_METHOD_GET, SOL_COAP_MESSAGE_TYPE_CON,
        client->objects_path, path, &obs, &entry->token, NULL, NULL, NULL, NULL, 0, NULL, &pkt);
    SOL_INT_CHECK(r, < 0, r);

    return sol_coap_send_packet_with_reply(
        client->secure ? client->server->dtls_server : client->server->coap,
        pkt, &client->cliaddr,
        observation_request_reply, entry);
}

SOL_API int
sol_lwm2m_server_del_observer(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content),
    const void *data)
{
    struct observer_entry *entry;
    enum sol_lwm2m_path_props props;
    int r;
    int64_t token;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);

    props = sol_lwm2m_common_get_path_props(path);
    SOL_EXP_CHECK(props < PATH_HAS_OBJECT, -EINVAL);

    entry = find_observer_entry(&server->observers, client, path);
    SOL_NULL_CHECK(entry, -ENOENT);

    r = observer_entry_del_monitor(entry, cb, data);
    SOL_INT_CHECK(r, < 0, r);

    if (entry->monitors.entries.len)
        return 0;

    entry->removed = true;
    token = entry->token;

    return sol_coap_unobserve_by_token(
        client->secure ? client->server->dtls_server : client->server->coap,
        &entry->cinfo->cliaddr,
        (uint8_t *)&token, sizeof(token));
}

static bool
management_reply(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr)
{
    struct management_ctx *ctx = data;
    uint8_t code = 0;
    enum sol_lwm2m_content_type content_type = SOL_LWM2M_CONTENT_TYPE_TEXT;
    struct sol_str_slice content = SOL_STR_SLICE_EMPTY;

    if (!cliaddr && !req)
        code = SOL_COAP_RESPONSE_CODE_GATEWAY_TIMEOUT;

    switch (ctx->type) {
    case MANAGEMENT_DELETE:
    case MANAGEMENT_CREATE:
    case MANAGEMENT_WRITE:
    case MANAGEMENT_EXECUTE:
        if (!code)
            sol_coap_header_get_code(req, &code);
        ((void (*)(void *,
        struct sol_lwm2m_server *,
        struct sol_lwm2m_client_info *, const char *,
        enum sol_coap_response_code))ctx->cb)
            ((void *)ctx->data, ctx->server, ctx->cinfo, ctx->path, code);
        break;
    default: //Read
        if (!code)
            extract_content(req, &code, &content_type, &content);
        ((void (*)(void *, struct sol_lwm2m_server *,
        struct sol_lwm2m_client_info *,
        const char *,
        enum sol_coap_response_code,
        enum sol_lwm2m_content_type,
        struct sol_str_slice))ctx->cb)((void *)ctx->data, ctx->server,
            ctx->cinfo, ctx->path, code, content_type, content);
        break;
    }

    if (code != SOL_COAP_RESPONSE_CODE_GATEWAY_TIMEOUT)
        send_ack_if_needed(server, req, cliaddr);
    free(ctx->path);
    free(ctx);
    return false;
}

static int
send_management_packet(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum management_type type, void *cb, const void *data,
    enum sol_coap_method method,
    struct sol_lwm2m_resource *resources, size_t len, const char *execute_args)
{
    int r;
    struct sol_coap_packet *pkt;
    struct management_ctx *ctx;

    r = setup_coap_packet(method, SOL_COAP_MESSAGE_TYPE_CON,
        client->objects_path, path, NULL, NULL, resources, NULL, NULL, NULL,
        len, execute_args, &pkt);
    SOL_INT_CHECK(r, < 0, r);

    if (!cb)
        return sol_coap_send_packet(
            client->secure ? client->server->dtls_server : client->server->coap,
            pkt, &client->cliaddr);

    ctx = malloc(sizeof(struct management_ctx));
    SOL_NULL_CHECK_GOTO(ctx, err_exit);

    ctx->path = strdup(path);
    SOL_NULL_CHECK_GOTO(ctx->path, err_exit);
    ctx->type = type;
    ctx->server = server;
    ctx->cinfo = client;
    ctx->data = data;
    ctx->cb = cb;

    return sol_coap_send_packet_with_reply(
        client->secure ? client->server->dtls_server : client->server->coap,
        pkt, &client->cliaddr,
        management_reply, ctx);

err_exit:
    free(ctx);
    sol_coap_packet_unref(pkt);
    return -ENOMEM;
}

SOL_API int
sol_lwm2m_server_write(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data)
{
    enum sol_coap_method method = SOL_COAP_METHOD_PUT;
    enum sol_lwm2m_path_props props;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(resources, -EINVAL);

    props = sol_lwm2m_common_get_path_props(path);
    SOL_EXP_CHECK(props < PATH_HAS_INSTANCE, -EINVAL);

    if (props == PATH_HAS_INSTANCE)
        method = SOL_COAP_METHOD_POST;

    return send_management_packet(server, client, path,
        MANAGEMENT_WRITE, cb, data, method, resources, len, NULL);
}

SOL_API int
sol_lwm2m_server_execute_resource(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path, const char *args,
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data)
{
    enum sol_lwm2m_path_props props;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    props = sol_lwm2m_common_get_path_props(path);
    SOL_EXP_CHECK(props != PATH_HAS_RESOURCE, -EINVAL);

    return send_management_packet(server, client, path,
        MANAGEMENT_EXECUTE, cb, data, SOL_COAP_METHOD_POST, NULL, 0, args);
}

SOL_API int
sol_lwm2m_server_delete_object_instance(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data)
{
    enum sol_lwm2m_path_props props;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    props = sol_lwm2m_common_get_path_props(path);
    SOL_EXP_CHECK(props != PATH_HAS_INSTANCE, -EINVAL);

    return send_management_packet(server, client, path,
        MANAGEMENT_DELETE, cb, data, SOL_COAP_METHOD_DELETE, NULL, 0, NULL);
}

SOL_API int
sol_lwm2m_server_create_object_instance(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data)
{
    enum sol_lwm2m_path_props props;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    props = sol_lwm2m_common_get_path_props(path);
    SOL_EXP_CHECK(props < PATH_HAS_OBJECT || props > PATH_HAS_INSTANCE, -EINVAL);

    return send_management_packet(server, client, path,
        MANAGEMENT_CREATE, cb, data, SOL_COAP_METHOD_POST, resources,
        len, NULL);
}

SOL_API int
sol_lwm2m_server_read(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content),
    const void *data)
{
    enum sol_lwm2m_path_props props;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(cb, -EINVAL);

    props = sol_lwm2m_common_get_path_props(path);
    SOL_EXP_CHECK(props < PATH_HAS_OBJECT, -EINVAL);

    return send_management_packet(server, client, path,
        MANAGEMENT_READ, cb, data, SOL_COAP_METHOD_GET, NULL, 0, NULL);
}
