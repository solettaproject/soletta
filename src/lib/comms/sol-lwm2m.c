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
#include <errno.h>
#include <float.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>


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

#define SECURITY_SERVER_OBJECT_ID (0)
#define SERVER_OBJECT_ID (1)

#ifndef SOL_NO_API_VERSION
#define LWM2M_TLV_CHECK_API(_tlv, ...) \
    do { \
        if (SOL_UNLIKELY((_tlv).api_version != \
            SOL_LWM2M_TLV_API_VERSION)) { \
            SOL_WRN("Couldn't handle tlv that has unsupported version " \
                "'%u', expected version is '%u'", \
                (_tlv).api_version, SOL_LWM2M_TLV_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0);
#define LWM2M_RESOURCE_CHECK_API(_resource, ...) \
    do { \
        if (SOL_UNLIKELY((_resource).api_version != \
            SOL_LWM2M_RESOURCE_API_VERSION)) { \
            SOL_WRN("Couldn't handle resource that has unsupported version " \
                "'%u', expected version is '%u'", \
                (_resource).api_version, SOL_LWM2M_RESOURCE_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0);
#define LWM2M_RESOURCE_CHECK_API_GOTO(_resource, _label) \
    do { \
        if (SOL_UNLIKELY((_resource).api_version != \
            SOL_LWM2M_RESOURCE_API_VERSION)) { \
            SOL_WRN("Couldn't handle resource that has unsupported version " \
                "'%u', expected version is '%u'", \
                (_resource).api_version, SOL_LWM2M_RESOURCE_API_VERSION); \
            goto _label; \
        } \
    } while (0);
#define LWM2M_OBJECT_CHECK_API(_obj, ...) \
    do { \
        if (SOL_UNLIKELY((_obj).api_version != \
            SOL_LWM2M_OBJECT_API_VERSION)) { \
            SOL_WRN("Couldn't handle object that has unsupported version " \
                "'%u', expected version is '%u'", \
                (_obj).api_version, SOL_LWM2M_OBJECT_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0);
#define LWM2M_OBJECT_CHECK_API_GOTO(_obj, _label) \
    do { \
        if (SOL_UNLIKELY((_obj).api_version != \
            SOL_LWM2M_OBJECT_API_VERSION)) { \
            SOL_WRN("Couldn't handle object that has unsupported version " \
                "'%u', expected version is '%u'", \
                (_obj).api_version, SOL_LWM2M_OBJECT_API_VERSION); \
            goto _label; \
        } \
    } while (0);
#else
#define LWM2M_TLV_CHECK_API(_tlv, ...)
#define LWM2M_RESOURCE_CHECK_API(_resource, ...)
#define LWM2M_OBJECT_CHECK_API(_obj, ...)
#define LWM2M_RESOURCE_CHECK_API_GOTO(_resource, _label)
#define LWM2M_OBJECT_CHECK_API_GOTO(_obj, _label)
#endif

enum tlv_length_size_type {
    LENGTH_SIZE_CHECK_NEXT_TWO_BITS = 0,
    LENGTH_SIZE_8_BITS = 8,
    LENGTH_SIZE_16_BITS = 16,
    LENGTH_SIZE_24_BITS = 32
};

enum lwm2m_parser_args_state {
    STATE_NEEDS_DIGIT = 0,
    STATE_NEEDS_COMMA_OR_EQUAL = (1 << 1),
    STATE_NEEDS_COMMA = (1 << 2),
    STATE_NEEDS_APOSTROPHE = (1 << 3),
    STATE_NEEDS_CHAR_OR_DIGIT = (1 << 4),
};

struct lifetime_ctx {
    struct sol_timeout *timeout;
    uint32_t lifetime;
};

struct sol_lwm2m_server {
    struct sol_coap_server *coap;
    struct sol_ptr_vector clients;
    struct sol_ptr_vector clients_to_delete;
    struct sol_monitors registration;
    struct sol_vector observers;
    struct lifetime_ctx lifetime_ctx;
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

//Data structs used by LWM2M Client.
struct obj_instance {
    uint16_t id;
    const void *data;
};

struct obj_ctx {
    const struct sol_lwm2m_object *obj;
    struct sol_vector instances;
};

struct sol_lwm2m_client {
    struct sol_coap_server *coap_server;
    struct lifetime_ctx lifetime_ctx;
    struct sol_vector connections;
    struct sol_vector objects;
    char *name;
    char *path;
    char *sms;
    bool running;
};

struct server_conn_ctx {
    struct sol_lwm2m_client *client;
    struct sol_network_link_addr server_addr;
    struct sol_coap_packet *pending_pkt; //Pending registration reply
    int64_t server_id;
    int64_t lifetime;
    time_t registration_time;
    char *location;
};

static bool lifetime_server_timeout(void *data);
static bool lifetime_client_timeout(void *data);

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
            if (endptr == value.data) {
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
        (sizeof(struct sol_str_slice) * 3));
    SOL_NULL_CHECK(cinfo, -ENOMEM);

    (*cinfo)->lifetime = DEFAULT_CLIENT_LIFETIME;
    (*cinfo)->binding = DEFAULT_BINDING_MODE;
    r = generate_location(&(*cinfo)->location);
    SOL_INT_CHECK(r, < 0, r);

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
    sol_lwm2m_server_content_cb cb, const void *data)
{
    struct sol_monitors_entry *e;

    e = sol_monitors_append(&entry->monitors, (sol_monitors_cb_t)cb, data);
    SOL_NULL_CHECK(e, -ENOMEM);
    return 0;
}

static int
observer_entry_del_monitor(struct observer_entry *entry,
    sol_lwm2m_server_content_cb cb, const void *data)
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

static size_t
get_int_size(int64_t i)
{
    //FIXME: Find a batter way to handle negative numbers.
    if (i < 0)
        return 8;
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
get_resource_len(const struct sol_lwm2m_resource *resource, uint16_t index,
    size_t *len)
{
    switch (resource->data_type) {
    case SOL_LWM2M_RESOURCE_DATA_TYPE_STRING:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE:
        *len = resource->data[index].bytes.len;
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_INT:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_TIME:
        *len = get_int_size(resource->data[index].integer);
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_BOOLEAN:
        *len = 1;
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT:
        *len = get_double_size(resource->data[index].fp);
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK:
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
    uint8_t original[sizeof(int64_t)] = { };
    size_t i, j;

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
add_int_resource(struct sol_buffer *buf, int64_t i, size_t len)
{
    to_network_order((uint8_t *)&i, len);
    return sol_buffer_append_bytes(buf, (uint8_t *)&i, len);
}

static int
add_resource_bytes_to_buffer(const struct sol_lwm2m_resource *resource,
    struct sol_buffer *buf, uint16_t idx)
{
    int r;
    uint8_t b;
    size_t len;

    r = get_resource_len(resource, idx, &len);
    SOL_INT_CHECK(r, < 0, r);

    switch (resource->data_type) {
    case SOL_LWM2M_RESOURCE_DATA_TYPE_STRING:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE:
        return sol_buffer_append_slice(buf, resource->data[idx].bytes);
    case SOL_LWM2M_RESOURCE_DATA_TYPE_INT:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_TIME:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK:
        return add_int_resource(buf, resource->data[idx].integer, len);
    case SOL_LWM2M_RESOURCE_DATA_TYPE_BOOLEAN:
        b = resource->data[idx].integer != 0 ? 1 : 0;
        return sol_buffer_append_bytes(buf, (uint8_t *)&b, 1);
    case SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT:
        return add_float_resource(buf, resource->data[idx].fp, len);
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
setup_tlv_header(enum sol_lwm2m_tlv_type tlv_type, uint16_t res_id,
    struct sol_buffer *buf, size_t data_len)
{
    int r;
    uint8_t tlv_data[6];
    size_t tlv_data_len;

    tlv_data_len = 2;

    tlv_data[0] = tlv_type;

    if (res_id > UINT8_MAX) {
        tlv_data[0] |= ID_HAS_16BITS_MASK;
        tlv_data[1] = (res_id >> 8) & 255;
        tlv_data[2] = res_id & 255;
        tlv_data_len++;
    } else
        tlv_data[1] = res_id;

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

    r = sol_buffer_append_bytes(buf, tlv_data, tlv_data_len);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
setup_tlv(struct sol_lwm2m_resource *resource, struct sol_buffer *buf)
{
    int r;
    size_t data_len, len;
    uint16_t i;
    enum sol_lwm2m_tlv_type type;

    LWM2M_RESOURCE_CHECK_API(*resource, -EINVAL);

    for (i = 0, data_len = 0; i < resource->data_len; i++) {
        r = get_resource_len(resource, i, &len);
        SOL_INT_CHECK(r, < 0, r);
        data_len += len;
    }

    switch (resource->type) {
    case SOL_LWM2M_RESOURCE_TYPE_SINGLE:
        type = SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE;
        break;
    case SOL_LWM2M_RESOURCE_TYPE_MULTIPLE:
        type = SOL_LWM2M_TLV_TYPE_MULTIPLE_RESOURCES;
        data_len += (resource->data_len * 2);
        break;
    default:
        SOL_WRN("Unknown resource type '%d'", (int)resource->type);
        return -EINVAL;
    }

    r = setup_tlv_header(type, resource->id, buf, data_len);
    SOL_INT_CHECK(r, < 0, r);

    if (type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE)
        return add_resource_bytes_to_buffer(resource, buf, 0);

    for (i = 0; i < resource->data_len; i++) {
        r = get_resource_len(resource, i, &data_len);
        SOL_INT_CHECK(r, < 0, r);
        r = setup_tlv_header(SOL_LWM2M_TLV_TYPE_RESOURCE_INSTANCE, i,
            buf, data_len);
        SOL_INT_CHECK(r, < 0, r);
        r = add_resource_bytes_to_buffer(resource, buf, i);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

static int
resources_to_tlv(struct sol_lwm2m_resource *resources,
    size_t len, struct sol_buffer *tlvs)
{
    int r;
    size_t i;

    for (i = 0; i < len; i++) {
        r = setup_tlv(&resources[i], tlvs);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    return 0;

exit:
    return r;
}

static int
add_coap_int_option(struct sol_coap_packet *pkt,
    sol_coap_option_num_t opt, const void *data, uint16_t len)
{
    uint8_t buf[sizeof(int64_t)] = { };

    memcpy(buf, data, len);
    to_network_order(buf, len);
    return sol_coap_add_option(pkt, opt, buf, len);
}

static int
get_coap_int_option(struct sol_coap_packet *pkt,
    sol_coap_option_num_t opt, uint16_t *value)
{
    const void *v;
    uint16_t len;

    v = sol_coap_find_first_option(pkt, opt, &len);

    if (!v)
        return -ENOENT;

    memcpy(value, v, len);
    to_machine_order((uint8_t *)value, len);
    return 0;
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
        r = add_coap_int_option(*pkt, SOL_COAP_OPTION_OBSERVE, &obs,
            sizeof(uint8_t));
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
        r = add_coap_int_option(*pkt, SOL_COAP_OPTION_CONTENT_FORMAT,
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

    if (*code == SOL_COAP_RSPCODE_CONTENT && sol_coap_packet_has_payload(req)) {
        r = sol_coap_packet_get_payload(req, &buf, &len);
        SOL_INT_CHECK(r, < 0);
        content->len = len;
        content->data = (const char *)buf;
        r = get_coap_int_option(req, SOL_COAP_OPTION_CONTENT_FORMAT,
            (uint16_t *)type);
        if (r < 0)
            SOL_INF("Content format not specified");
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
        ((sol_lwm2m_server_content_cb)m->cb)((void *)m->data, entry->server,
            entry->cinfo, entry->path, code, type, content);

    return keep_alive;
}

SOL_API int
sol_lwm2m_server_add_observer(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path, sol_lwm2m_server_content_cb cb, const void *data)
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
    sol_lwm2m_server_content_cb cb, const void *data)
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

static bool
management_reply(struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr,
    void *data)
{
    struct management_ctx *ctx = data;
    uint8_t code = 0;
    enum sol_lwm2m_content_type content_type = SOL_LWM2M_CONTENT_TYPE_TEXT;
    struct sol_str_slice content = SOL_STR_SLICE_EMPTY;

    if (!cliaddr && !req)
        code = SOL_COAP_RSPCODE_GATEWAY_TIMEOUT;

    switch (ctx->type) {
    case MANAGEMENT_DELETE:
    case MANAGEMENT_CREATE:
    case MANAGEMENT_WRITE:
    case MANAGEMENT_EXECUTE:
        if (!code)
            code = sol_coap_header_get_code(req);
        ((sol_lwm2m_server_management_status_response_cb)ctx->cb)
            ((void *)ctx->data, ctx->server, ctx->cinfo, ctx->path, code);
        break;
    default: //Read
        if (!code)
            extract_content(req, &code, &content_type, &content);
        ((sol_lwm2m_server_content_cb)ctx->cb)((void *)ctx->data, ctx->server,
            ctx->cinfo, ctx->path, code, content_type, content);
        break;
    }

    if (code != SOL_COAP_RSPCODE_GATEWAY_TIMEOUT)
        send_ack_if_needed(server, req, cliaddr);
    free(ctx->path);
    free(ctx);
    return false;
}

static int
send_management_packet(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum management_type type, void *cb, const void *data,
    sol_coap_method_t method,
    struct sol_lwm2m_resource *resources, size_t len, const char *execute_args)
{
    int r;
    struct sol_coap_packet *pkt;
    struct management_ctx *ctx;

    r = setup_coap_packet(method, SOL_COAP_TYPE_CON,
        client->objects_path, path, NULL, NULL, resources, len,
        execute_args, &pkt);
    SOL_INT_CHECK(r, < 0, r);

    if (!cb)
        return sol_coap_send_packet(server->coap, pkt, &client->cliaddr);

    ctx = malloc(sizeof(struct management_ctx));
    SOL_NULL_CHECK_GOTO(ctx, err_exit);

    ctx->path = strdup(path);
    SOL_NULL_CHECK_GOTO(ctx->path, err_exit);
    ctx->type = type;
    ctx->server = server;
    ctx->cinfo = client;
    ctx->data = data;
    ctx->cb = cb;

    return sol_coap_send_packet_with_reply(server->coap, pkt, &client->cliaddr,
        management_reply, ctx);

err_exit:
    free(ctx);
    sol_coap_packet_unref(pkt);
    return -ENOMEM;
}

static bool
is_resource_set(const char *path)
{
    size_t i;
    uint8_t slashes;
    const char *last_slash;

    for (i = 0, slashes = 0; path[i]; i++) {
        if (path[i] == '/') {
            last_slash = path + i;
            slashes++;
        }
    }

    if (slashes < 3 || *(last_slash + 1) == '\0')
        return false;
    return true;
}

SOL_API int
sol_lwm2m_server_management_write(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    sol_lwm2m_server_management_status_response_cb cb, const void *data)
{
    sol_coap_method_t method = SOL_COAP_METHOD_PUT;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(resources, -EINVAL);

    if (!is_resource_set(path))
        method = SOL_COAP_METHOD_POST;

    return send_management_packet(server, client, path,
        MANAGEMENT_WRITE, cb, data, method, resources, len, NULL);
}

SOL_API int
sol_lwm2m_server_management_execute(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path, const char *args,
    sol_lwm2m_server_management_status_response_cb cb, const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    return send_management_packet(server, client, path,
        MANAGEMENT_EXECUTE, cb, data, SOL_COAP_METHOD_POST, NULL, 0, args);
}

SOL_API int
sol_lwm2m_server_management_delete(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    sol_lwm2m_server_management_status_response_cb cb, const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    return send_management_packet(server, client, path,
        MANAGEMENT_DELETE, cb, data, SOL_COAP_METHOD_DELETE, NULL, 0, NULL);
}

SOL_API int
sol_lwm2m_server_management_create(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    sol_lwm2m_server_management_status_response_cb cb, const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    return send_management_packet(server, client, path,
        MANAGEMENT_CREATE, cb, data, SOL_COAP_METHOD_POST, resources,
        len, NULL);
}

SOL_API int
sol_lwm2m_server_management_read(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    sol_lwm2m_server_content_cb cb, const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(cb, -EINVAL);

    return send_management_packet(server, client, path,
        MANAGEMENT_READ, cb, data, SOL_COAP_METHOD_GET, NULL, 0, NULL);
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
sol_lwm2m_tlv_to_int(struct sol_lwm2m_tlv *tlv, int64_t *value)
{
    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(*tlv, -EINVAL);

    if (tlv->content.used != 1 &&
        tlv->content.used != 2 &&
        tlv->content.used != 4 &&
        tlv->content.used != 8)
        return -EINVAL;

    *value = 0;
    memcpy(value, tlv->content.data, tlv->content.used);
    to_machine_order((uint8_t *)value, tlv->content.used);

    SOL_DBG("TLV has integer data. Value: %" PRId64 "", *value);
    return 0;
}

SOL_API int
sol_lwm2m_tlv_to_bool(struct sol_lwm2m_tlv *tlv, bool *value)
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
sol_lwm2m_tlv_to_float(struct sol_lwm2m_tlv *tlv, double *value)
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
sol_lwm2m_tlv_get_bytes(struct sol_lwm2m_tlv *tlv, uint8_t **bytes,
    uint16_t *len)
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

SOL_API void
sol_lwm2m_resource_clear(struct sol_lwm2m_resource *resource)
{
    SOL_NULL_CHECK(resource);
    LWM2M_RESOURCE_CHECK_API(*resource);

    free(resource->data);
}

static int
extract_path(struct sol_lwm2m_client *client, struct sol_coap_packet *req,
    uint16_t *path_id, uint16_t *path_size)
{
    struct sol_str_slice path[4] = { };
    int i, j, r, count;

    r = sol_coap_find_options(req, SOL_COAP_OPTION_URI_PATH, path,
        ARRAY_SIZE(path));
    count = r;
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    r = -ENOENT;
    SOL_INT_CHECK_GOTO(r, == 0, err_exit);

    if (client->path && !sol_str_slice_str_eq(path[0], client->path)) {
        SOL_WRN("Wrong object path. Client: %s, Received: %.*s", client->path,
            SOL_STR_SLICE_PRINT(path[0]));
        r = -EINVAL;
        goto err_exit;
    }

    for (i = client->path ? 1 : 0, j = 0; i < count; i++, j++) {
        char *end;
        //Only numbers are allowed.
        path_id[j] = sol_util_strtoul(path[i].data, &end, path[i].len, 10);
        if (end == path[i].data || end != path[i].data + path[i].len) {
            SOL_WRN("Could not convert %.*s to integer",
                SOL_STR_SLICE_PRINT(path[i]));
            r = -EINVAL;
            goto err_exit;
        }
        SOL_DBG("Path ID at request: %" PRIu16 "", path_id[j]);
    }

    *path_size = j;
    return 0;

err_exit:
    return r;
}

static struct obj_ctx *
find_object_ctx_by_id(struct sol_lwm2m_client *client, uint16_t id)
{
    uint16_t i;
    struct obj_ctx *ctx;

    SOL_VECTOR_FOREACH_IDX (&client->objects, ctx, i) {
        if (ctx->obj->id == id)
            return ctx;
    }

    return NULL;
}

static struct obj_instance *
find_object_instance_by_instance_id(struct obj_ctx *ctx, uint16_t instance_id)
{
    uint16_t i;
    struct obj_instance *instance;

    SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, i) {
        if (instance->id == instance_id)
            return instance;
    }

    return NULL;
}

static uint8_t
handle_delete(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance)
{
    int r;

    if (!obj_instance) {
        SOL_WRN("Object instance was not provided to delete! (object id: %"
            PRIu16 "", obj_ctx->obj->id);
        return SOL_COAP_RSPCODE_BAD_REQUEST;
    }

    if (!obj_ctx->obj->del) {
        SOL_WRN("The object %" PRIu16 " does not implement the delete method",
            obj_ctx->obj->id);
        return SOL_COAP_RSPCODE_NOT_ALLOWED;
    }

    r = obj_ctx->obj->del((void *)obj_instance->data,
        (void *)obj_ctx->obj->data, client, obj_instance->id);
    if (r < 0) {
        SOL_WRN("Could not properly delete object id %"
            PRIu16 " instance id: %" PRIu16 " reason:%d",
            obj_ctx->obj->id, obj_instance->id, r);
        return SOL_COAP_RSPCODE_NOT_ALLOWED;
    }

    (void)sol_vector_del_element(&obj_ctx->instances, obj_instance);
    return SOL_COAP_RSPCODE_DELETED;
}

static bool
is_valid_char(char c)
{
    if (c == '!' ||
        (c >= '#' && c <= '&') ||
        (c >= '(' && c <= '[') ||
        (c >= ']' && c <= '~'))
        return true;
    return false;
}

static bool
is_valid_args(const struct sol_str_slice args)
{
    size_t i;
    enum lwm2m_parser_args_state state = STATE_NEEDS_DIGIT;

    if (!args.len)
        return true;

    for (i = 0; i < args.len; i++) {
        if (state == STATE_NEEDS_DIGIT) {
            if (isdigit(args.data[i]))
                state = STATE_NEEDS_COMMA_OR_EQUAL;
            else {
                SOL_WRN("Expecting a digit, but found '%c'", args.data[i]);
                return false;
            }
        } else if (state == STATE_NEEDS_COMMA_OR_EQUAL) {
            if (args.data[i] == ',')
                state = STATE_NEEDS_DIGIT;
            else if (args.data[i] == '=')
                state = STATE_NEEDS_APOSTROPHE;
            else {
                SOL_WRN("Expecting ',' or '=' but found '%c'", args.data[i]);
                return false;
            }
        } else if (state == STATE_NEEDS_APOSTROPHE) {
            if (args.data[i] == '\'')
                state = STATE_NEEDS_CHAR_OR_DIGIT;
            else {
                SOL_WRN("Expecting '\'' but found '%c'", args.data[i]);
                return false;
            }
        } else if (state == STATE_NEEDS_CHAR_OR_DIGIT) {
            if (args.data[i] == '\'')
                state = STATE_NEEDS_COMMA;
            else if (!is_valid_char(args.data[i])) {
                SOL_WRN("Invalid characterc '%c'", args.data[i]);
                return false;
            }
        } else if (state == STATE_NEEDS_COMMA) {
            if (args.data[i] == ',')
                state = STATE_NEEDS_DIGIT;
            else {
                SOL_WRN("Expecting ',' found '%c'", args.data[i]);
                return false;
            }
        }
    }
    if ((state & (STATE_NEEDS_COMMA | STATE_NEEDS_COMMA_OR_EQUAL)))
        return true;
    return false;
}

static uint8_t
handle_execute(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance,
    uint16_t resource, const struct sol_str_slice args)
{
    int r;

    if (!obj_instance) {
        SOL_WRN("Object instance was not provided to execute the path"
            "/%" PRIu16 "/?/%" PRIu16 "", obj_ctx->obj->id, resource);
        return SOL_COAP_RSPCODE_BAD_REQUEST;
    }

    if (!obj_ctx->obj->execute) {
        SOL_WRN("Obj id %" PRIu16 " does not implemet the execute",
            obj_ctx->obj->id);
        return SOL_COAP_RSPCODE_NOT_ALLOWED;
    }

    if (!is_valid_args(args)) {
        SOL_WRN("Invalid arguments. Args: %.*s", SOL_STR_SLICE_PRINT(args));
        return SOL_COAP_RSPCODE_BAD_REQUEST;
    }

    r = obj_ctx->obj->execute((void *)obj_instance->data,
        (void *)obj_ctx->obj->data, client, obj_instance->id, resource, args);

    if (r < 0) {
        SOL_WRN("Could not execute the path /%" PRIu16
            "/%" PRIu16 "/%" PRIu16 " with args: %.*s", obj_ctx->obj->id,
            obj_instance->id, resource, SOL_STR_SLICE_PRINT(args));
        return SOL_COAP_RSPCODE_NOT_ALLOWED;
    }

    return SOL_COAP_RSPCODE_CHANGED;
}

static uint8_t
handle_write(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance,
    int32_t resource, uint16_t content_format,
    const struct sol_str_slice payload)
{
    int r;

    //If write_resource is not NULL then write_tlv is guaramteed to ve valid as well.
    if (!obj_ctx->obj->write_resource) {
        SOL_WRN("Object %" PRIu16 " does not support the write method",
            obj_ctx->obj->id);
        return SOL_COAP_RSPCODE_NOT_ALLOWED;
    }

    if (!content_format) {
        SOL_WRN("Content format was not set."
            " Impossible to create object instance");
        return SOL_COAP_RSPCODE_BAD_REQUEST;
    }

    if (!payload.len) {
        SOL_WRN("Payload to write on object instance /%"
            PRIu16 "/%" PRIu16 " is empty", obj_ctx->obj->id, obj_instance->id);
        return SOL_COAP_RSPCODE_BAD_REQUEST;
    }

    if (!obj_instance) {
        SOL_WRN("Object instance was not provided."
            " Can not complete the write operation");
        return SOL_COAP_RSPCODE_BAD_REQUEST;
    }

    if (content_format == SOL_LWM2M_CONTENT_TYPE_TLV) {
        struct sol_vector tlvs;
        r = sol_lwm2m_parse_tlv(payload, &tlvs);
        SOL_INT_CHECK(r, < 0, SOL_COAP_RSPCODE_BAD_REQUEST);
        r = obj_ctx->obj->write_tlv((void *)obj_instance->data,
            (void *)obj_ctx->obj->data, client, obj_instance->id, &tlvs);
        sol_lwm2m_tlv_array_clear(&tlvs);
        SOL_INT_CHECK(r, < 0, SOL_COAP_RSPCODE_BAD_REQUEST);
    } else if (content_format == SOL_LWM2M_CONTENT_TYPE_TEXT ||
        content_format == SOL_LWM2M_CONTENT_TYPE_OPAQUE) {
        struct sol_lwm2m_resource res;

        if (resource < 0) {
            SOL_WRN("Unexpected content format (%" PRIu16
                "). It must be TLV", content_format);
            return SOL_COAP_RSPCODE_BAD_REQUEST;
        }

        r = sol_lwm2m_resource_init(&res, resource, 1,
            content_format == SOL_LWM2M_CONTENT_TYPE_TEXT ?
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING :
            SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE,
            payload);
        SOL_INT_CHECK(r, < 0, SOL_COAP_RSPCODE_BAD_REQUEST);
        r = obj_ctx->obj->write_resource((void *)obj_instance->data,
            (void *)obj_ctx->obj->data, client, obj_instance->id, res.id, &res);
        sol_lwm2m_resource_clear(&res);
        SOL_INT_CHECK(r, < 0, SOL_COAP_RSPCODE_BAD_REQUEST);
    } else {
        SOL_WRN("Only TLV, string or opaque is supported for writing."
            " Received: %" PRIu16 "", content_format);
        return SOL_COAP_RSPCODE_BAD_REQUEST;
    }

    return SOL_COAP_RSPCODE_CHANGED;
}

static uint8_t
handle_create(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, int32_t instance_id,
    uint16_t content_format, const struct sol_str_slice payload)
{
    int r;
    uint8_t r_code;
    struct obj_instance *obj_instance;

    if (!obj_ctx->obj->create) {
        SOL_WRN("Object %" PRIu16 " does not support the create method",
            obj_ctx->obj->id);
        return SOL_COAP_RSPCODE_NOT_ALLOWED;
    }

    obj_instance = sol_vector_append(&obj_ctx->instances);
    SOL_NULL_CHECK(obj_instance, SOL_COAP_RSPCODE_BAD_REQUEST);

    if (instance_id < 0)
        obj_instance->id = obj_ctx->instances.len - 1;
    else
        obj_instance->id = instance_id;

    r = obj_ctx->obj->create((void *)obj_ctx->obj->data, client,
        obj_instance->id, (void *)&obj_instance->data);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    if (!payload.len)
        return SOL_COAP_RSPCODE_CREATED;

    r_code = handle_write(client, obj_ctx, obj_instance, -1,
        content_format, payload);
    SOL_INT_CHECK_GOTO(r_code, != SOL_COAP_RSPCODE_CHANGED, err_exit);

    return SOL_COAP_RSPCODE_CREATED;

err_exit:
    (void)sol_vector_del_element(&obj_ctx->instances, obj_instance);
    return SOL_COAP_RSPCODE_BAD_REQUEST;
}

static int
read_object_instance(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance,
    struct sol_vector *resources)
{
    struct sol_lwm2m_resource *res;
    uint16_t i;
    int r;

    for (i = 0;; i++) {

        res = sol_vector_append(resources);
        SOL_NULL_CHECK(res, -ENOMEM);

        r = obj_ctx->obj->read((void *)obj_instance->data,
            (void *)obj_ctx->obj->data, client, obj_instance->id, i, res);

        if (r == -ENOENT) {
            (void)sol_vector_del_element(resources, res);
            continue;
        }
        if (r == -EBADRQC) {
            (void)sol_vector_del_element(resources, res);
            break;
        }
        LWM2M_RESOURCE_CHECK_API_GOTO(*res, err_api);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    return 0;

err_api:
    r = -EINVAL;
err_exit:
    (void)sol_vector_del_element(resources, res);
    return r;
}

static uint8_t
handle_read(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance,
    int32_t resource_id, struct sol_coap_packet *resp)
{
    struct sol_vector resources = SOL_VECTOR_INIT(struct sol_lwm2m_resource);
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    struct sol_lwm2m_resource *res;
    uint16_t format = SOL_LWM2M_CONTENT_TYPE_TLV;
    uint16_t i;
    int r;

    if (!obj_ctx->obj->read) {
        SOL_WRN("Object %" PRIu16 " does not support the read method",
            obj_ctx->obj->id);
        return SOL_COAP_RSPCODE_NOT_ALLOWED;
    }

    if (obj_instance && resource_id >= 0) {
        res = sol_vector_append(&resources);
        SOL_NULL_CHECK(res, SOL_COAP_RSPCODE_BAD_REQUEST);

        r = obj_ctx->obj->read((void *)obj_instance->data,
            (void *)obj_ctx->obj->data, client,
            obj_instance->id, resource_id, res);

        if (r == -ENOENT || r == -EBADRQC) {
            sol_vector_clear(&resources);
            return SOL_COAP_RSPCODE_NOT_FOUND;
        }

        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        LWM2M_RESOURCE_CHECK_API_GOTO(*res, err_exit);
    } else if (obj_instance) {
        r = read_object_instance(client, obj_ctx, obj_instance, &resources);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    } else {
        struct obj_instance *instance;

        SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, instance, i) {
            r = read_object_instance(client, obj_ctx, instance, &resources);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }
    }

    SOL_VECTOR_FOREACH_IDX (&resources, res, i) {
        r = setup_tlv(res, &buf);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        sol_lwm2m_resource_clear(res);
    }

    r = add_coap_int_option(resp, SOL_COAP_OPTION_CONTENT_FORMAT,
        &format, sizeof(format));
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    r = set_packet_payload(resp, (const uint8_t *)buf.data, buf.used);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    sol_buffer_fini(&buf);
    sol_vector_clear(&resources);
    return SOL_COAP_RSPCODE_CONTENT;

err_exit:
    SOL_VECTOR_FOREACH_IDX (&resources, res, i)
        sol_lwm2m_resource_clear(res);
    sol_buffer_fini(&buf);
    sol_vector_clear(&resources);
    return SOL_COAP_RSPCODE_BAD_REQUEST;
}

static int
handle_resource(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    int r;
    uint8_t method;
    struct sol_coap_packet *resp;
    struct sol_lwm2m_client *client = data;
    struct obj_ctx *obj_ctx;
    struct obj_instance *obj_instance = NULL;
    uint16_t path[3], path_size, content_format;
    uint8_t header_code;
    struct sol_str_slice payload = SOL_STR_SLICE_EMPTY;

    resp = sol_coap_packet_new(req);
    SOL_NULL_CHECK(resp, -ENOMEM);

    r = get_coap_int_option(req, SOL_COAP_OPTION_CONTENT_FORMAT,
        &content_format);

    if (r < 0)
        content_format = SOL_LWM2M_CONTENT_TYPE_TEXT;

    r = extract_path(client, req, path, &path_size);
    header_code = SOL_COAP_RSPCODE_BAD_REQUEST;
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    obj_ctx = find_object_ctx_by_id(client, path[0]);
    header_code = SOL_COAP_RSPCODE_NOT_FOUND;
    SOL_NULL_CHECK_GOTO(obj_ctx, exit);

    if (path_size >= 2)
        obj_instance = find_object_instance_by_instance_id(obj_ctx, path[1]);

    if (sol_coap_packet_has_payload(req)) {
        uint8_t *args;
        uint16_t args_len;
        r = sol_coap_packet_get_payload(req, &args, &args_len);
        header_code = SOL_COAP_RSPCODE_BAD_REQUEST;
        SOL_INT_CHECK_GOTO(r, < 0, exit);
        payload.len = args_len;
        payload.data = (char *)args;
    }

    method = sol_coap_header_get_code(req);

    switch (method) {
    case SOL_COAP_METHOD_GET:
        header_code = handle_read(client, obj_ctx, obj_instance,
            path_size > 2 ? path[2] : -1, resp);
        break;
    case SOL_COAP_METHOD_POST:
        if (path_size == 1)
            //This is a create op
            header_code = handle_create(client, obj_ctx, -1,
                content_format, payload);
        else if (path_size == 2 && !obj_instance)
            //This is a create with chosen by the LWM2M server.
            header_code = handle_create(client, obj_ctx, path[1],
                content_format, payload);
        else if (path_size == 2)
            //Write on object instance
            header_code = handle_write(client, obj_ctx, obj_instance, -1,
                content_format, payload);
        else {
            //Execute.
            header_code = handle_execute(client, obj_ctx, obj_instance, path[2],
                payload);
        }
        break;
    case SOL_COAP_METHOD_PUT:
        if (path_size == 3) {
            //Write op on a resource.
            header_code = handle_write(client, obj_ctx, obj_instance,
                path[2], content_format, payload);
        } else {
            header_code = SOL_COAP_RSPCODE_BAD_REQUEST;
            SOL_WRN("Write request without full path specified!");
        }
        break;
    case SOL_COAP_METHOD_DELETE:
        header_code = handle_delete(client, obj_ctx, obj_instance);
        break;
    default:
        header_code = SOL_COAP_RSPCODE_BAD_REQUEST;
        SOL_WRN("Unknown COAP method: %" PRIu8 "", method);
    }

exit:
    sol_coap_header_set_code(resp, header_code);
    return sol_coap_send_packet(server, resp, cliaddr);
}

SOL_API struct sol_lwm2m_client *
sol_lwm2m_client_new(const char *name, const char *path, const char *sms,
    const struct sol_lwm2m_object **objects)
{
    struct sol_lwm2m_client *client;
    size_t i;

    SOL_NULL_CHECK(name, NULL);
    SOL_NULL_CHECK(objects, NULL);
    SOL_NULL_CHECK(objects[0], NULL);

    SOL_LOG_INTERNAL_INIT_ONCE;

    client = calloc(1, sizeof(struct sol_lwm2m_client));
    SOL_NULL_CHECK(client, NULL);

    sol_vector_init(&client->objects, sizeof(struct obj_ctx));
    sol_vector_init(&client->connections, sizeof(struct server_conn_ctx));

    for (i = 0; objects[i]; i++) {
        struct obj_ctx *obj_ctx = sol_vector_append(&client->objects);
        LWM2M_OBJECT_CHECK_API_GOTO(*objects[i], err_obj);
        SOL_NULL_CHECK_GOTO(obj_ctx, err_obj);
        if ((objects[i]->write_resource && !objects[i]->write_tlv) ||
            (!objects[i]->write_resource && objects[i]->write_tlv)) {
            SOL_WRN("write_resource and write_tlv must be provided!");
            goto err_obj;
        }
        obj_ctx->obj = objects[i];
        sol_vector_init(&obj_ctx->instances, sizeof(struct obj_instance));
    }

    client->name = strdup(name);
    SOL_NULL_CHECK_GOTO(client->name, err_obj);

    if (path) {
        client->path = strdup(path);
        SOL_NULL_CHECK_GOTO(client->path, err_path);
    }

    if (sms) {
        client->sms = strdup(sms);
        SOL_NULL_CHECK_GOTO(client->sms, err_sms);
    }

    client->coap_server = sol_coap_server_new(0);
    SOL_NULL_CHECK_GOTO(client->coap_server, err_coap);

    return client;

err_coap:
    free(client->sms);
err_sms:
    free(client->path);
err_path:
    free(client->name);
err_obj:
    sol_vector_clear(&client->objects);
    free(client);
    return NULL;
}

static void
obj_ctx_clear(struct sol_lwm2m_client *client, struct obj_ctx *ctx)
{
    uint16_t i;
    struct obj_instance *instance;

    if (ctx->obj->del) {
        SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, i) {
            ctx->obj->del((void *)instance->data,
                (void *)ctx->obj->data, client, instance->id);
        }
    }
    sol_vector_clear(&ctx->instances);
}

static void
server_connection_ctx_clear(struct server_conn_ctx *conn_ctx)
{
    if (conn_ctx->pending_pkt)
        sol_coap_packet_unref(conn_ctx->pending_pkt);
    free(conn_ctx->location);
}

static void
server_connection_ctx_remove(struct sol_vector *conns,
    struct server_conn_ctx *conn_ctx)
{
    server_connection_ctx_clear(conn_ctx);
    (void)sol_vector_del_element(conns, conn_ctx);
}

static void
server_connection_ctx_list_clear(struct sol_vector *conns)
{
    uint16_t i;
    struct server_conn_ctx *conn_ctx;

    SOL_VECTOR_FOREACH_IDX (conns, conn_ctx, i)
        server_connection_ctx_clear(conn_ctx);
    sol_vector_clear(conns);
}

SOL_API void
sol_lwm2m_client_del(struct sol_lwm2m_client *client)
{
    uint16_t i;
    struct obj_ctx *ctx;

    SOL_NULL_CHECK(client);

    sol_coap_server_unref(client->coap_server);

    SOL_VECTOR_FOREACH_IDX (&client->objects, ctx, i)
        obj_ctx_clear(client, ctx);

    server_connection_ctx_list_clear(&client->connections);
    sol_vector_clear(&client->objects);
    free(client->name);
    free(client->path);
    free(client->sms);
    free(client);
}

SOL_API int
sol_lwm2m_add_object_instance(struct sol_lwm2m_client *client,
    const struct sol_lwm2m_object *obj, const void *data)
{
    struct obj_ctx *ctx;
    struct obj_instance *instance;

    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(obj, -EINVAL);
    LWM2M_OBJECT_CHECK_API(*obj, -EINVAL);

    ctx = find_object_ctx_by_id(client, obj->id);
    SOL_NULL_CHECK(ctx, -ENOENT);

    instance = sol_vector_append(&ctx->instances);
    SOL_NULL_CHECK(instance, -ENOMEM);
    instance->id = ctx->instances.len - 1;
    instance->data = data;

    return 0;
}

static void
clear_resource_array(struct sol_lwm2m_resource *array, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++)
        sol_lwm2m_resource_clear(&array[i]);
}

static int
read_resources(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *instance,
    struct sol_lwm2m_resource *res, size_t res_len, ...)
{
    size_t i;
    int r = 0;
    va_list ap;

    SOL_NULL_CHECK(obj_ctx->obj->read, -ENOTSUP);

    va_start(ap, res_len);

    // The va_list contains the resources IDs that we should be read.
    for (i = 0; i < res_len; i++) {
        r = obj_ctx->obj->read((void *)instance->data,
            (void *)obj_ctx->obj->data, client, instance->id,
            (uint16_t)va_arg(ap, int), &res[i]);
        LWM2M_RESOURCE_CHECK_API_GOTO(res[i], err_exit_api);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    va_end(ap);
    return 0;

err_exit_api:
    r = -EINVAL;
err_exit:
    clear_resource_array(res, i);
    va_end(ap);
    return r;
}

static int
get_binding_and_lifetime(struct sol_lwm2m_client *client, int64_t server_id,
    int64_t *lifetime, struct sol_str_slice *binding)
{
    struct obj_ctx *ctx;
    struct obj_instance *instance;
    uint16_t i;
    int r;
    struct sol_lwm2m_resource res[3];

    ctx = find_object_ctx_by_id(client, SERVER_OBJECT_ID);

    if (!ctx) {
        SOL_WRN("LWM2M Server object not provided");
        return -ENOENT;
    }

    SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, i) {
        r = read_resources(client, ctx, instance, res, ARRAY_SIZE(res),
            0, 1, 7);
        SOL_INT_CHECK(r, < 0, r);

        if (res[0].data[0].integer == server_id) {
            r = -EINVAL;
            SOL_INT_CHECK_GOTO(get_binding_mode_from_str(res[2].data[0].bytes),
                == SOL_LWM2M_BINDING_MODE_UNKNOWN, exit);
            *lifetime = res[1].data[0].integer;
            *binding = res[2].data[0].bytes;
            r = 0;
            goto exit;
        }
        clear_resource_array(res, ARRAY_SIZE(res));
    }

    return -ENOENT;

exit:
    clear_resource_array(res, ARRAY_SIZE(res));
    return r;
}

static struct server_conn_ctx *
server_connection_ctx_new(struct sol_lwm2m_client *client,
    const struct sol_str_slice str_addr, int64_t server_id, int64_t lifetime)
{
    struct server_conn_ctx *conn_ctx;

    conn_ctx = sol_vector_append(&client->connections);
    SOL_NULL_CHECK(conn_ctx, NULL);
    conn_ctx->client = client;
    conn_ctx->server_id = server_id;
    conn_ctx->lifetime = lifetime;

    //FIXME: Create getaddrinfo() like func.
    conn_ctx->server_addr.port = 5683;
    conn_ctx->server_addr.family = AF_INET;
    if (!sol_network_addr_from_str(&conn_ctx->server_addr, "127.0.0.1")) {
        SOL_WRN("Could not parse the LWM2M Server addres '%*s'",
            SOL_STR_SLICE_PRINT(str_addr));
        (void)sol_vector_del_element(&client->connections, conn_ctx);
        return NULL;
    }
    //Location will be filled in register_reply()

    return conn_ctx;
}

static int
setup_objects_payload(struct sol_lwm2m_client *client, struct sol_buffer *objs)
{
    uint16_t i, j;
    struct obj_ctx *ctx;
    struct obj_instance *instance;
    int r;

    sol_buffer_init(objs);

    if (client->path) {
        r = sol_buffer_append_printf(objs, "</%s>;rt=\"oma.lwm2m\",",
            client->path);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    SOL_VECTOR_FOREACH_IDX (&client->objects, ctx, i) {
        if (!ctx->instances.len) {
            r = sol_buffer_append_printf(objs, "</%" PRIu16 ">,", ctx->obj->id);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
            continue;
        }

        SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, j) {
            r = sol_buffer_append_printf(objs, "</%" PRIu16 "/%" PRIu16 ">,",
                ctx->obj->id, instance->id);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }
    }

    //Remove last ','
    objs->used--;

    SOL_DBG("Objs payload: %.*s",
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(objs)));
    return 0;

err_exit:
    sol_buffer_fini(objs);
    return r;
}

static int
reschedule_client_timeout(struct sol_lwm2m_client *client)
{
    uint16_t i;
    uint32_t smallest, remeaning, lf;
    struct server_conn_ctx *conn_ctx;
    time_t now;
    int r;

    now = time(NULL);
    smallest = UINT32_MAX;

    SOL_VECTOR_FOREACH_IDX (&client->connections, conn_ctx, i) {
        remeaning = conn_ctx->lifetime - (now - conn_ctx->registration_time);
        if (remeaning < smallest) {
            smallest = remeaning;
            lf = conn_ctx->lifetime;
        }
    }

    if (client->lifetime_ctx.timeout)
        sol_timeout_del(client->lifetime_ctx.timeout);

    //To milliseconds.
    r = sol_util_uint32_mul(smallest, 1000, &smallest);
    SOL_INT_CHECK(r, < 0, r);
    client->lifetime_ctx.timeout = sol_timeout_add(smallest,
        lifetime_client_timeout, client);
    SOL_NULL_CHECK(client->lifetime_ctx.timeout, -ENOMEM);
    client->lifetime_ctx.lifetime = lf;

    return 0;
}

static bool
register_reply(struct sol_coap_server *server,
    struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *server_addr, void *data)
{
    struct server_conn_ctx *conn_ctx = data;
    struct sol_str_slice path[2];
    uint16_t code;
    char addr[SOL_INET_ADDR_STRLEN] = { };
    int r;

    if (!pkt && !server_addr) {
        SOL_WRN("Registration request timeout!");
        return false;
    }

    sol_coap_packet_unref(conn_ctx->pending_pkt);
    conn_ctx->pending_pkt = NULL;

    if (!sol_network_addr_to_str(server_addr, addr, sizeof(addr)))
        SOL_WRN("Could not convert the server address to string");

    code = sol_coap_header_get_code(pkt);
    SOL_INT_CHECK_GOTO(code, != SOL_COAP_RSPCODE_CREATED, err_exit);

    r = sol_coap_find_options(pkt, SOL_COAP_OPTION_LOCATION_PATH, path,
        ARRAY_SIZE(path));
    SOL_INT_CHECK_GOTO(r, != 2, err_exit);

    conn_ctx->location = sol_str_slice_to_string(path[1]);
    SOL_NULL_CHECK_GOTO(conn_ctx->location, err_exit);

    SOL_DBG("Registered with server %s at location %s", addr,
        conn_ctx->location);

    r = reschedule_client_timeout(conn_ctx->client);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    return false;

err_exit:
    server_connection_ctx_remove(&conn_ctx->client->connections, conn_ctx);
    return false;
}

static bool
update_reply(struct sol_coap_server *server,
    struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *server_addr, void *data)
{
    uint8_t code;
    struct server_conn_ctx *conn_ctx = data;

    if (!pkt && !server_addr)
        return false;

    code = sol_coap_header_get_code(pkt);
    SOL_INT_CHECK_GOTO(code, != SOL_COAP_RSPCODE_CHANGED, err_exit);
    return false;

err_exit:
    server_connection_ctx_remove(&conn_ctx->client->connections, conn_ctx);
    return false;
}

static int
register_with_server(struct sol_lwm2m_client *client,
    struct server_conn_ctx *conn_ctx,
    int64_t lifetime, const struct sol_str_slice binding,
    const struct sol_buffer *objects_payload, bool is_update)
{
    struct sol_coap_packet *pkt;
    struct sol_buffer query = SOL_BUFFER_INIT_EMPTY;
    uint8_t format = SOL_COAP_CONTENTTYPE_APPLICATION_LINKFORMAT;
    int r;
    uint16_t len;
    uint8_t *buf;

#define ADD_QUERY(_key, _format, _value) \
    do { \
        query.used = 0; \
        r = sol_buffer_append_printf(&query, "%s=" _format "", _key, _value); \
        SOL_INT_CHECK_GOTO(r, < 0, err_coap); \
        r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_QUERY, \
            query.data, query.used); \
        SOL_INT_CHECK_GOTO(r, < 0, err_coap); \
    } while (0);

    pkt = sol_coap_packet_request_new(SOL_COAP_METHOD_POST, SOL_COAP_TYPE_CON);
    SOL_NULL_CHECK(pkt, -ENOMEM);

    r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH, "rd", strlen("rd"));
    SOL_INT_CHECK_GOTO(r, < 0, err_coap);

    if (is_update) {
        r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH,
            conn_ctx->location, strlen(conn_ctx->location));
        SOL_INT_CHECK_GOTO(r, < 0, err_coap);
    } else
        conn_ctx->pending_pkt = sol_coap_packet_ref(pkt);

    r = add_coap_int_option(pkt, SOL_COAP_OPTION_CONTENT_FORMAT,
        &format, sizeof(format));
    SOL_INT_CHECK_GOTO(r, < 0, err_coap);

    if (!is_update)
        ADD_QUERY("ep", "%s", client->name);
    ADD_QUERY("lt", "%" PRId64, lifetime);
    ADD_QUERY("binding", "%.*s", SOL_STR_SLICE_PRINT(binding));
    if (client->sms)
        ADD_QUERY("sms", "%s", client->sms);

    r = sol_coap_packet_get_payload(pkt, &buf, &len);
    SOL_INT_CHECK_GOTO(r, < 0, err_coap);
    SOL_INT_CHECK_GOTO(len, < objects_payload->used, err_coap);

    memcpy(buf, objects_payload->data, objects_payload->used);
    r = sol_coap_packet_set_payload_used(pkt, objects_payload->used);
    SOL_INT_CHECK_GOTO(r, < 0, err_coap);

    conn_ctx->registration_time = time(NULL);

    SOL_DBG("Connecting with LWM2M server - binding '%.*s' -"
        "lifetime '%" PRId64 "'", SOL_STR_SLICE_PRINT(binding),
        lifetime);
    r = sol_coap_send_packet_with_reply(client->coap_server,
        pkt, &conn_ctx->server_addr,
        is_update ? update_reply : register_reply, conn_ctx);
    sol_buffer_fini(&query);
    return r;

err_coap:
    sol_coap_packet_unref(pkt);
    sol_buffer_fini(&query);
    return r;

#undef ADD_QUERY
}

static int
spam_update(struct sol_lwm2m_client *client, bool consider_lifetime)
{
    int r;
    uint16_t i;
    struct server_conn_ctx *conn_ctx;
    struct sol_buffer objs_payload;

    r = setup_objects_payload(client, &objs_payload);
    SOL_INT_CHECK(r, < 0, r);

    SOL_VECTOR_FOREACH_IDX (&client->connections, conn_ctx, i) {
        int64_t lifetime;
        struct sol_str_slice binding;

        if (consider_lifetime &&
            conn_ctx->lifetime != client->lifetime_ctx.lifetime)
            continue;

        //Read it again, in case it changed.
        r = get_binding_and_lifetime(client, conn_ctx->server_id,
            &lifetime, &binding);
        SOL_INT_CHECK_GOTO(r, < 0, exit);

        //Update its lifetime.
        conn_ctx->lifetime = lifetime;
        r = register_with_server(client, conn_ctx, lifetime, binding,
            &objs_payload, true);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = reschedule_client_timeout(client);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

exit:
    sol_buffer_fini(&objs_payload);
    return r;
}

static bool
lifetime_client_timeout(void *data)
{
    if (spam_update(data, true) < 0)
        SOL_WRN("Could not spam the update");
    return false;
}

SOL_API int
sol_lwm2m_client_start(struct sol_lwm2m_client *client)
{
    uint16_t i;
    struct obj_ctx *ctx;
    bool has_server = false;
    struct obj_instance *instance;
    struct sol_buffer objs_payload;
    struct server_conn_ctx *conn_ctx;
    struct sol_lwm2m_resource res[3];
    int r;

    SOL_NULL_CHECK(client, -EINVAL);

    ctx = find_object_ctx_by_id(client, SECURITY_SERVER_OBJECT_ID);
    if (!ctx) {
        SOL_WRN("LWM2M Security object not provided!");
        return -ENOENT;
    }

    r = setup_objects_payload(client, &objs_payload);
    SOL_INT_CHECK(r, < 0, r);

    SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, i) {
        struct sol_str_slice binding;
        int64_t lifetime;

        r = read_resources(client, ctx, instance, res, ARRAY_SIZE(res),
            0, 1, 10);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        //Is it a bootstap?
        if (!res[1].data[0].b) {
            r = get_binding_and_lifetime(client, res[2].data[0].integer,
                &lifetime, &binding);
            SOL_INT_CHECK_GOTO(r, < 0, err_clear);

            conn_ctx = server_connection_ctx_new(client, res[0].data[0].bytes,
                res[2].data[0].integer, lifetime);
            r = -ENOMEM;
            SOL_NULL_CHECK_GOTO(conn_ctx, err_clear);

            r = register_with_server(client, conn_ctx, lifetime, binding,
                &objs_payload, false);
            if (r < 0) {
                SOL_WRN("Could not register with the server");
                (void)sol_vector_del(&client->connections,
                    client->connections.len - 1);
                goto err_clear;
            }

            has_server = true;
        }
        clear_resource_array(res, ARRAY_SIZE(res));
    }

    if (!has_server) {
        SOL_WRN("The client did not specify a LWM2M server to connect");
        r = -ENOENT;
        goto err_exit;
    }

    client->running = true;
    sol_coap_server_set_unknown_resource_handler(client->coap_server,
        handle_resource, client);

    sol_buffer_fini(&objs_payload);
    return 0;

err_clear:
    clear_resource_array(res, ARRAY_SIZE(res));
err_exit:
    return r;
}

static int
send_client_delete_request(struct sol_lwm2m_client *client,
    struct server_conn_ctx *conn_ctx)
{
    struct sol_coap_packet *pkt;
    int r;

    //Did not receive reply yet.
    if (!conn_ctx->location) {
        r = sol_coap_cancel_send_packet(client->coap_server,
            conn_ctx->pending_pkt, &conn_ctx->server_addr);
        sol_coap_packet_unref(conn_ctx->pending_pkt);
        conn_ctx->pending_pkt = NULL;
        return r;
    }

    pkt = sol_coap_packet_request_new(SOL_COAP_METHOD_DELETE,
        SOL_COAP_TYPE_NONCON);
    SOL_NULL_CHECK(pkt, -ENOMEM);

    r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH, "rd", strlen("rd"));
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH,
        conn_ctx->location, strlen(conn_ctx->location));
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    return sol_coap_send_packet(client->coap_server, pkt,
        &conn_ctx->server_addr);

err_exit:
    sol_coap_packet_unref(pkt);
    return r;
}

SOL_API int
sol_lwm2m_client_stop(struct sol_lwm2m_client *client)
{
    struct server_conn_ctx *conn_ctx;
    uint16_t i;
    int r;

    SOL_NULL_CHECK(client, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&client->connections, conn_ctx, i) {
        r = send_client_delete_request(client, conn_ctx);
        SOL_INT_CHECK(r, < 0, r);
    }

    sol_coap_server_set_unknown_resource_handler(client->coap_server,
        NULL, NULL);
    client->running = false;
    server_connection_ctx_list_clear(&client->connections);
    return 0;
}

SOL_API int
sol_lwm2m_send_update(struct sol_lwm2m_client *client)
{
    SOL_NULL_CHECK(client, -EINVAL);

    return spam_update(client, false);
}
