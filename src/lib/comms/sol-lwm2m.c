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

#define SOL_LOG_DOMAIN &_lwm2m_domain

#include "sol-log-internal.h"
#include "sol-util-internal.h"
#include "sol-list.h"
#include "sol-lwm2m.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-monitors.h"
#include "sol-random.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util.h"
#include "sol-http.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_lwm2m_domain, "lwm2m");

#define LWM2M_BOOTSTRAP_QUERY_PARAMS (1)
#define LWM2M_UPDATE_QUERY_PARAMS (4)
#define LWM2M_REGISTER_QUERY_PARAMS (5)
#define NUMBER_OF_PATH_SEGMENTS (3)
#define DEFAULT_SHORT_SERVER_ID (0)
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
#define UINT24_MAX (16777215)
#define ONE_SECOND (1000)

#define SECURITY_SERVER_OBJECT_ID (0)
#define SECURITY_SERVER_URI (0)
#define SECURITY_SERVER_IS_BOOTSTRAP (1)
#define SECURITY_SERVER_ID (10)
#define SECURITY_SERVER_CLIENT_HOLD_OFF_TIME (11)
#define SECURITY_SERVER_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT (12)

#define SERVER_OBJECT_ID (1)
#define SERVER_OBJECT_SERVER_ID (0)
#define SERVER_OBJECT_LIFETIME (1)
#define SERVER_OBJECT_BINDING (7)

#ifndef SOL_NO_API_VERSION
#define LWM2M_TLV_CHECK_API(_tlv, ...) \
    do { \
        if (SOL_UNLIKELY((_tlv)->api_version != \
            SOL_LWM2M_TLV_API_VERSION)) { \
            SOL_WRN("Couldn't handle tlv that has unsupported version " \
                "'%u', expected version is '%u'", \
                (_tlv)->api_version, SOL_LWM2M_TLV_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0);
#define LWM2M_RESOURCE_CHECK_API(_resource, ...) \
    do { \
        if (SOL_UNLIKELY((_resource)->api_version != \
            SOL_LWM2M_RESOURCE_API_VERSION)) { \
            SOL_WRN("Couldn't handle resource that has unsupported version " \
                "'%u', expected version is '%u'", \
                (_resource)->api_version, SOL_LWM2M_RESOURCE_API_VERSION); \
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

#define ADD_QUERY(_key, _format, _value) \
    do { \
        query.used = 0; \
        r = sol_buffer_append_printf(&query, "%s=" _format "", _key, _value); \
        SOL_INT_CHECK_GOTO(r, < 0, err_coap); \
        r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_QUERY, \
            query.data, query.used); \
        SOL_INT_CHECK_GOTO(r, < 0, err_coap); \
    } while (0);

enum tlv_length_size_type {
    LENGTH_SIZE_CHECK_NEXT_TWO_BITS = 0,
    LENGTH_SIZE_8_BITS = 8,
    LENGTH_SIZE_16_BITS = 16,
    LENGTH_SIZE_24_BITS = 24
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
    struct sol_ptr_vector observers;
    struct lifetime_ctx lifetime_ctx;
};

struct sol_lwm2m_bootstrap_server {
    struct sol_coap_server *coap;
    struct sol_ptr_vector clients;
    struct sol_monitors bootstrap;
    const char **known_clients;
};

struct sol_lwm2m_client_object {
    struct sol_ptr_vector instances;
    uint16_t id;
};

struct sol_lwm2m_client_info {
    struct sol_ptr_vector objects;
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

struct sol_lwm2m_bootstrap_client_info {
    char *name;
    struct sol_network_link_addr cliaddr;
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

enum bootstrap_type {
    BOOTSTRAP_DELETE,
    BOOTSTRAP_WRITE
};

struct bootstrap_ctx {
    enum bootstrap_type type;
    struct sol_lwm2m_bootstrap_server *server;
    struct sol_lwm2m_bootstrap_client_info *cinfo;
    char *path;
    void *cb;
    const void *data;
};

struct resource_ctx {
    char *str_id;
    struct sol_coap_resource *res;
    uint16_t id;
};

//Data structs used by LWM2M Client.
struct obj_instance {
    uint16_t id;
    bool should_delete;
    char *str_id;
    const void *data;
    struct sol_vector resources_ctx;
    struct sol_coap_resource *instance_res;
};

struct obj_ctx {
    const struct sol_lwm2m_object *obj;
    char *str_id;
    struct sol_vector instances;
    struct sol_coap_resource *obj_res;
};

struct sol_lwm2m_client {
    struct sol_coap_server *coap_server;
    struct lifetime_ctx lifetime_ctx;
    struct sol_ptr_vector connections;
    struct sol_vector objects;
    struct sol_monitors bootstrap;
    struct {
        struct sol_timeout *timeout;
        struct sol_blob *server_uri;
    } bootstrap_ctx;
    const void *user_data;
    uint16_t splitted_path_len;
    char *name;
    char **splitted_path;
    char *sms;
    bool running;
    bool removed;
    bool is_bootstrapping;
};

struct server_conn_ctx {
    struct sol_network_hostname_pending *hostname_handle;
    struct sol_lwm2m_client *client;
    struct sol_vector server_addr_list;
    struct sol_coap_packet *pending_pkt; //Pending registration or bootstrap reply
    int64_t server_id;
    int64_t lifetime;
    uint16_t port;
    uint16_t addr_list_idx;
    time_t registration_time;
    char *location;
};

static bool lifetime_server_timeout(void *data);
static bool lifetime_client_timeout(void *data);
static int register_with_server(struct sol_lwm2m_client *client,
    struct server_conn_ctx *conn_ctx, bool is_update);
static int bootstrap_with_server(struct sol_lwm2m_client *client,
    struct server_conn_ctx *conn_ctx);
static int handle_resource(void *data, struct sol_coap_server *server,
    const struct sol_coap_resource *resource, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr);

static void
send_ack_if_needed(struct sol_coap_server *coap, struct sol_coap_packet *msg,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_coap_packet *ack;
    uint8_t type;

    sol_coap_header_get_type(msg, &type);

    if (type == SOL_COAP_MESSAGE_TYPE_CON) {
        ack = sol_coap_packet_new(msg);
        SOL_NULL_CHECK(ack);
        if (sol_coap_send_packet(coap, ack, cliaddr) < 0)
            SOL_WRN("Could not send the response ACK");
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
        ((void (*)(void *, struct sol_lwm2m_server *,
        struct sol_lwm2m_client_info *,
        enum sol_lwm2m_registration_event))m->cb)((void *)m->data, server,
            cinfo, event);
}

static void
dispatch_bootstrap_event_to_client(struct sol_lwm2m_client *client,
    enum sol_lwm2m_bootstrap_event event)
{
    uint16_t i;
    struct sol_monitors_entry *m;

    SOL_MONITORS_WALK (&client->bootstrap, m, i)
        ((void (*)(void *, struct sol_lwm2m_client *,
        enum sol_lwm2m_bootstrap_event))m->cb)((void *)m->data, client, event);
}

static void
dispatch_bootstrap_event_to_server(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo)
{
    uint16_t i;
    struct sol_monitors_entry *m;

    SOL_MONITORS_WALK (&server->bootstrap, m, i)
        ((void (*)(void *, struct sol_lwm2m_bootstrap_server *,
        struct sol_lwm2m_bootstrap_client_info *))m->cb)((void *)m->data, server,
            bs_cinfo);
}

static void
client_objects_clear(struct sol_ptr_vector *objects)
{
    uint16_t i, j, *id;
    struct sol_lwm2m_client_object *object;

    SOL_PTR_VECTOR_FOREACH_IDX (objects, object, i) {
        SOL_PTR_VECTOR_FOREACH_IDX (&object->instances, id, j)
            free(id);
        sol_ptr_vector_clear(&object->instances);
        free(object);
    }

    sol_ptr_vector_clear(objects);
}

static void
bootstrap_client_info_del(struct sol_lwm2m_bootstrap_client_info *bs_cinfo)
{
    free(bs_cinfo->name);
    free(bs_cinfo);
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
            sol_coap_unobserve_by_token(server->coap, &cinfo->cliaddr,
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
find_client_object_by_id(struct sol_ptr_vector *objects,
    uint16_t id)
{
    uint16_t i;
    struct sol_lwm2m_client_object *cobject;

    SOL_PTR_VECTOR_FOREACH_IDX (objects, cobject, i) {
        if (cobject->id == id)
            return cobject;
    }

    return NULL;
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
extract_bootstrap_client_info(struct sol_coap_packet *req,
    struct sol_str_slice *client_name)
{
    struct sol_str_slice query;
    int r;

    r = sol_coap_find_options(req, SOL_COAP_OPTION_URI_QUERY, &query,
        LWM2M_BOOTSTRAP_QUERY_PARAMS);
    SOL_INT_CHECK(r, < 0, r);

    struct sol_str_slice key, value;
    const char *sep;

    SOL_DBG("Query:%.*s", SOL_STR_SLICE_PRINT(query));
    sep = memchr(query.data, '=', query.len);

    if (!sep) {
        SOL_WRN("Could not find the separator '=' at: %.*s",
            SOL_STR_SLICE_PRINT(query));
        return -EINVAL;
    }

    key.data = query.data;
    key.len = sep - query.data;
    value.data = sep + 1;
    value.len = query.len - key.len - 1;

    if (sol_str_slice_str_eq(key, "ep")) {
        //Required info
        *client_name = value;
    } else {
        SOL_WRN("The client did not provide its name!");
        return -EINVAL;
    }

    return 0;
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
    (void)sol_coap_send_packet(coap, response, cliaddr);
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

static int
new_bootstrap_client_info(struct sol_lwm2m_bootstrap_client_info **bs_cinfo,
    const struct sol_network_link_addr *cliaddr, const struct sol_str_slice client_name)
{
    *bs_cinfo = calloc(1, sizeof(struct sol_lwm2m_bootstrap_client_info));
    SOL_NULL_CHECK(bs_cinfo, -ENOMEM);

    memcpy(&(*bs_cinfo)->cliaddr, cliaddr, sizeof(struct sol_network_link_addr));

    (*bs_cinfo)->name = sol_str_slice_to_str(client_name);
    SOL_NULL_CHECK_GOTO((*bs_cinfo)->name, err_no_name);
    return 0;

err_no_name:
    free(*bs_cinfo);
    return -ENOMEM;
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

    r = sol_coap_server_register_resource(server->coap, &cinfo->resource,
        cinfo);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_del_client);

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
        " lifetime: %" PRIu32 " objects paths: %s",
        cinfo->name, cinfo->location, cinfo->sms,
        cinfo->binding, cinfo->lifetime, cinfo->objects_path);

    r = sol_coap_send_packet(coap, response, cliaddr);
    dispatch_registration_event(server, cinfo,
        SOL_LWM2M_REGISTRATION_EVENT_REGISTER);
    return r;

err_exit_unregister:
    if (sol_coap_server_unregister_resource(server->coap, &cinfo->resource) < 0)
        SOL_WRN("Could not unregister resource for client: %s", cinfo->name);
err_exit_del_client:
    client_info_del(cinfo);
err_exit:
    sol_coap_header_set_code(response, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);
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

static int
bootstrap_request(void *data, struct sol_coap_server *coap,
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo;
    struct sol_lwm2m_bootstrap_server *server = data;
    struct sol_coap_packet *response;
    struct sol_str_slice client_name = SOL_STR_SLICE_EMPTY;
    int r;
    size_t i;
    bool know_client = false;

    SOL_DBG("Client Bootstrap Request received");

    response = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response, -ENOMEM);

    r = extract_bootstrap_client_info(req, &client_name);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    for (i = 0; server->known_clients[i]; i++) {
        if (sol_str_slice_str_eq(client_name, server->known_clients[i]))
            know_client = true;
    }

    if (!know_client) {
        SOL_WRN("Client %.*s bootstrap request received, but this Bootstrap Server"
            " doesn't have Bootstrap Information for this client.",
            SOL_STR_SLICE_PRINT(client_name));
        goto err_exit;
    }

    r = new_bootstrap_client_info(&bs_cinfo, cliaddr, client_name);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    r = sol_ptr_vector_append(&server->clients, bs_cinfo);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_del_client);


    r = sol_coap_header_set_code(response, SOL_COAP_RESPONSE_CODE_CHANGED);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit_del_client_list);

    SOL_DBG("Client %s bootstrap request received."
        " Bootstrap Process will start now.", bs_cinfo->name);

    r = sol_coap_send_packet(coap, response, cliaddr);
    dispatch_bootstrap_event_to_server(server, bs_cinfo);

    return r;

err_exit_del_client_list:
    (void)sol_ptr_vector_remove(&server->clients, bs_cinfo);
err_exit_del_client:
    bootstrap_client_info_del(bs_cinfo);
err_exit:
    sol_coap_header_set_code(response, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);
    (void)sol_coap_send_packet(coap, response, cliaddr);
    return r;
}

static const struct sol_coap_resource bootstrap_request_interface = {
    SOL_SET_API_VERSION(.api_version = SOL_COAP_RESOURCE_API_VERSION, )
    .post = bootstrap_request,
    .flags = SOL_COAP_FLAGS_NONE,
    .path = {
        SOL_STR_SLICE_LITERAL("bs"),
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
add_to_monitors(struct sol_monitors *monitors, sol_monitors_cb_t cb, const void *data)
{
    struct sol_monitors_entry *m;

    SOL_NULL_CHECK(cb, -EINVAL);

    m = sol_monitors_append(monitors, cb, data);
    SOL_NULL_CHECK(m, -ENOMEM);

    return 0;
}

static int
remove_from_monitors(struct sol_monitors *monitors, sol_monitors_cb_t cb, const void *data)
{
    int i;

    SOL_NULL_CHECK(cb, -EINVAL);

    i = sol_monitors_find(monitors, cb, data);
    SOL_INT_CHECK(i, < 0, i);

    return sol_monitors_del(monitors, i);
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
sol_lwm2m_server_new(uint16_t port)
{
    struct sol_lwm2m_server *server;
    struct sol_network_link_addr servaddr = { .family = SOL_NETWORK_FAMILY_INET6,
                                              .port = port };
    int r;

    SOL_LOG_INTERNAL_INIT_ONCE;

    server = calloc(1, sizeof(struct sol_lwm2m_server));
    SOL_NULL_CHECK(server, NULL);

    server->coap = sol_coap_server_new(&servaddr, false);
    SOL_NULL_CHECK_GOTO(server->coap, err_coap);

    sol_ptr_vector_init(&server->clients);
    sol_ptr_vector_init(&server->clients_to_delete);
    sol_ptr_vector_init(&server->observers);
    sol_monitors_init(&server->registration, NULL);

    r = sol_coap_server_register_resource(server->coap,
        &registration_interface, server);
    SOL_INT_CHECK_GOTO(r, < 0, err_register);

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

    SOL_PTR_VECTOR_FOREACH_IDX (&server->observers, entry, i)
        entry->removed = true;

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

SOL_API struct sol_lwm2m_bootstrap_server *
sol_lwm2m_bootstrap_server_new(uint16_t port, const char **known_clients)
{
    struct sol_lwm2m_bootstrap_server *server;
    struct sol_network_link_addr servaddr = { .family = SOL_NETWORK_FAMILY_INET6,
                                              .port = port };
    int r;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(known_clients, NULL);

    server = calloc(1, sizeof(struct sol_lwm2m_bootstrap_server));
    SOL_NULL_CHECK(server, NULL);

    server->coap = sol_coap_server_new(&servaddr, false);
    SOL_NULL_CHECK_GOTO(server->coap, err_coap);

    server->known_clients = known_clients;

    sol_ptr_vector_init(&server->clients);

    sol_monitors_init(&server->bootstrap, NULL);

    r = sol_coap_server_register_resource(server->coap,
        &bootstrap_request_interface, server);
    SOL_INT_CHECK_GOTO(r, < 0, err_register);

    return server;

err_register:
    sol_coap_server_unref(server->coap);
err_coap:
    free(server);
    return NULL;
}

SOL_API void
sol_lwm2m_bootstrap_server_del(struct sol_lwm2m_bootstrap_server *server)
{
    uint16_t i;
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo;

    SOL_NULL_CHECK(server);

    sol_coap_server_unref(server->coap);

    SOL_PTR_VECTOR_FOREACH_IDX (&server->clients, bs_cinfo, i)
        bootstrap_client_info_del(bs_cinfo);

    sol_monitors_clear(&server->bootstrap);
    sol_ptr_vector_clear(&server->clients);
    free(server);
}

SOL_API int
sol_lwm2m_bootstrap_server_add_request_monitor(struct sol_lwm2m_bootstrap_server *server,
    void (*cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo),
    const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);

    return add_to_monitors(&server->bootstrap, (sol_monitors_cb_t)cb, data);
}

SOL_API int
sol_lwm2m_bootstrap_server_del_request_monitor(struct sol_lwm2m_bootstrap_server *server,
    void (*cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo),
    const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);

    return remove_from_monitors(&server->bootstrap, (sol_monitors_cb_t)cb, data);
}

SOL_API int
sol_lwm2m_client_add_bootstrap_finish_monitor(struct sol_lwm2m_client *client,
    void (*cb)(void *data,
    struct sol_lwm2m_client *client,
    enum sol_lwm2m_bootstrap_event event),
    const void *data)
{
    SOL_NULL_CHECK(client, -EINVAL);

    return add_to_monitors(&client->bootstrap, (sol_monitors_cb_t)cb, data);
}

SOL_API int
sol_lwm2m_client_del_bootstrap_finish_monitor(struct sol_lwm2m_client *client,
    void (*cb)(void *data,
    struct sol_lwm2m_client *client,
    enum sol_lwm2m_bootstrap_event event),
    const void *data)
{
    SOL_NULL_CHECK(client, -EINVAL);

    return remove_from_monitors(&client->bootstrap, (sol_monitors_cb_t)cb, data);
}

SOL_API const struct sol_ptr_vector *
sol_lwm2m_server_get_clients(const struct sol_lwm2m_server *server)
{
    SOL_NULL_CHECK(server, NULL);

    return &server->clients;
}

SOL_API const char *
sol_lwm2m_bootstrap_client_info_get_name(const struct sol_lwm2m_bootstrap_client_info *client)
{
    SOL_NULL_CHECK(client, NULL);

    return client->name;
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

SOL_API const struct sol_network_link_addr *
sol_lwm2m_bootstrap_client_info_get_address(const struct sol_lwm2m_bootstrap_client_info *client)
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

SOL_API int
sol_lwm2m_client_object_get_id(const struct sol_lwm2m_client_object *object,
    uint16_t *id)
{
    SOL_NULL_CHECK(object, -EINVAL);
    SOL_NULL_CHECK(id, -EINVAL);

    *id = object->id;
    return 0;
}

SOL_API const struct sol_ptr_vector *
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

static int
get_resource_len(const struct sol_lwm2m_resource *resource, uint16_t index,
    size_t *len)
{
    switch (resource->data_type) {
    case SOL_LWM2M_RESOURCE_DATA_TYPE_STRING:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE:
        *len = resource->data[index].blob->size;
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_INT:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_TIME:
        *len = get_int_size(resource->data[index].integer);
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL:
        *len = 1;
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT:
        *len = 8;
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK:
        *len = OBJ_LINK_LEN;
        return 0;
    default:
        return -EINVAL;
    }
}

static void
swap_bytes(uint8_t *to_swap, size_t len)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return;
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t swap;
    size_t i, j, stop;

    stop = len / 2;

    for (i = 0, j = len - 1; i != stop; i++, j--) {
        swap = to_swap[i];
        to_swap[i] = to_swap[j];
        to_swap[j] = swap;
    }
#else
#error "Unknown byte order"
#endif
}

static int
add_float_resource(struct sol_buffer *buf, double fp, size_t len)
{
    uint8_t *bytes = NULL;
    float f;
    double d;

    if (len == 4) {
        f = (float)fp;
        swap_bytes((uint8_t *)&f, len);
        bytes = (uint8_t *)&f;
    } else {
        d = fp;
        swap_bytes((uint8_t *)&d, len);
        bytes = (uint8_t *)&d;
    }

    return sol_buffer_append_bytes(buf, bytes, len);
}

static int
add_int_resource(struct sol_buffer *buf, int64_t i, size_t len)
{
    swap_bytes((uint8_t *)&i, len);
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
        return sol_buffer_append_slice(buf, sol_str_slice_from_blob(resource->data[idx].blob));
    case SOL_LWM2M_RESOURCE_DATA_TYPE_INT:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_TIME:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK:
        return add_int_resource(buf, resource->data[idx].integer, len);
    case SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL:
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
    struct sol_buffer *buf;
    int r;

    r = sol_coap_packet_get_payload(pkt, &buf, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return sol_buffer_append_bytes(buf, data, len);
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
    } else if (data_len <= UINT24_MAX) {
        tlv_data[tlv_data_len++] = (data_len >> 16) & 255;
        tlv_data[tlv_data_len++] = (data_len >> 8) & 255;
        tlv_data[tlv_data_len++] = data_len & 255;
        tlv_data[0] |= LEN_IS_24BITS_MASK;
    } else
        return -ENOMEM;

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

    LWM2M_RESOURCE_CHECK_API(resource, -EINVAL);

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
instances_to_tlv(struct sol_lwm2m_resource **instances,
    size_t *instances_len, uint16_t *instances_ids, size_t len, struct sol_buffer *tlvs)
{
    int r;
    size_t i, j, res, instance_data_len, data_len;

    for (i = 0; i < len; i++) {
        for (res = 0, instance_data_len = 0; res < instances_len[i]; res++)
            for (j = 0; j < instances[i][res].data_len; j++) {
                r = get_resource_len(&instances[i][res], j, &data_len);
                SOL_INT_CHECK(r, < 0, r);

                instance_data_len += data_len;
            }

        r = setup_tlv_header(SOL_LWM2M_TLV_TYPE_OBJECT_INSTANCE,
            instances_ids[i], tlvs, instance_data_len);
        SOL_INT_CHECK(r, < 0, r);

        r = resources_to_tlv(instances[i], instances_len[i], tlvs);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    return 0;

exit:
    return r;
}

static int
add_coap_int_option(struct sol_coap_packet *pkt,
    enum sol_coap_option opt, const void *data, uint16_t len)
{
    uint8_t buf[sizeof(int64_t)] = { };

    memcpy(buf, data, len);
    swap_bytes(buf, len);
    return sol_coap_add_option(pkt, opt, buf, len);
}

static int
get_coap_int_option(struct sol_coap_packet *pkt,
    enum sol_coap_option opt, uint16_t *value)
{
    const void *v;
    uint16_t len;

    v = sol_coap_find_first_option(pkt, opt, &len);

    if (!v)
        return -ENOENT;

    memcpy(value, v, len);
    swap_bytes((uint8_t *)value, len);
    return 0;
}

static int
setup_coap_packet(enum sol_coap_method method,
    enum sol_coap_message_type type, const char *objects_path, const char *path,
    uint8_t *obs, int64_t *token, struct sol_lwm2m_resource *resources,
    struct sol_lwm2m_resource **instances, size_t *instances_len,
    uint16_t *instances_ids, size_t len,
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

    *pkt = sol_coap_packet_new_request(method, type);
    r = -ENOMEM;
    SOL_NULL_CHECK_GOTO(*pkt, exit);

    r = sol_random_get_int64(random, &t);
    if (r < 0) {
        SOL_WRN("Could not generate a random number");
        goto exit;
    }

    r = sol_coap_header_set_token(*pkt, (uint8_t *)&t,
        (uint8_t)sizeof(int64_t));
    if (r < 0) {
        SOL_WRN("Could not set the token");
        goto exit;
    }
    SOL_DBG("Setting token as %" PRId64 ", len = %zu", t, sizeof(int64_t));

    if (token)
        *token = t;

    if (obs) {
        r = add_coap_int_option(*pkt, SOL_COAP_OPTION_OBSERVE, obs,
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

    if (strcmp(path, "/") != 0) {
        r = sol_coap_packet_add_uri_path_option(*pkt, buf.data);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

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
    } else if (instances) {
        content_type = SOL_LWM2M_CONTENT_TYPE_TLV;
        r = instances_to_tlv(instances, instances_len, instances_ids, len, &tlvs);
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
            " path:%s", entry->path, entry->cinfo->name);
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

    r = setup_coap_packet(SOL_COAP_METHOD_GET, SOL_COAP_MESSAGE_TYPE_CON,
        client->objects_path, path, &obs, &entry->token, NULL, NULL, NULL, NULL, 0, NULL, &pkt);
    SOL_INT_CHECK(r, < 0, r);

    return sol_coap_send_packet_with_reply(server->coap, pkt, &client->cliaddr,
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
    int r;
    int64_t token;

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
    token = entry->token;

    return sol_coap_unobserve_by_token(server->coap, &entry->cinfo->cliaddr,
        (uint8_t *)&token, sizeof(token));
}

SOL_API int
sol_lwm2m_resource_init(struct sol_lwm2m_resource *resource,
    uint16_t id, uint16_t resource_len,
    enum sol_lwm2m_resource_data_type data_type, ...)
{
    uint16_t i;
    va_list ap;
    struct sol_blob *blob;
    int r = -EINVAL;

    if (!resource || data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_NONE ||
        !resource_len)
        return -EINVAL;

    LWM2M_RESOURCE_CHECK_API(resource, -EINVAL);

    resource->id = id;
    if (resource_len > 1)
        resource->type = SOL_LWM2M_RESOURCE_TYPE_MULTIPLE;
    else
        resource->type = SOL_LWM2M_RESOURCE_TYPE_SINGLE;
    resource->data_type = data_type;
    resource->data = calloc(resource_len, sizeof(union sol_lwm2m_resource_data));
    SOL_NULL_CHECK(resource->data, -ENOMEM);
    resource->data_len = resource_len;

    va_start(ap, data_type);

    for (i = 0; i < resource_len; i++) {
        switch (resource->data_type) {
        case SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE:
        case SOL_LWM2M_RESOURCE_DATA_TYPE_STRING:
            blob = va_arg(ap, struct sol_blob *);
            SOL_NULL_CHECK_GOTO(blob, err_exit);
            resource->data[i].blob = sol_blob_ref(blob);
            SOL_NULL_CHECK_GOTO(resource->data[i].blob, err_ref);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT:
            resource->data[i].fp = va_arg(ap, double);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_INT:
        case SOL_LWM2M_RESOURCE_DATA_TYPE_TIME:
            resource->data[i].integer = va_arg(ap, int64_t);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL:
            resource->data[i].integer = va_arg(ap, int);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK:
            resource->data[i].integer = (uint16_t)va_arg(ap, int);
            resource->data[i].integer = (resource->data[i].integer << 16) |
                (uint16_t)va_arg(ap, int);
            break;
        default:
            SOL_WRN("Unknown resource data type");
            goto err_exit;
        }
    }

    va_end(ap);
    return 0;

err_ref:
    r = -EOVERFLOW;
err_exit:
    if (data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE ||
        data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_STRING) {
        uint16_t until = i;

        for (i = 0; i < until; i++)
            sol_blob_unref(resource->data[i].blob);
    }
    free(resource->data);
    va_end(ap);
    return r;
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

/**
 * This checks if the path has the following form: /2/0/1
 */
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
sol_lwm2m_server_execute_resource(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path, const char *args,
    void (*cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

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
    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

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
    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

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
    LWM2M_TLV_CHECK_API(tlv);
    sol_buffer_fini(&tlv->content);
}

SOL_API void
sol_lwm2m_tlv_clear(struct sol_lwm2m_tlv *tlv)
{
    SOL_NULL_CHECK(tlv);
    tlv_clear(tlv);
}

SOL_API void
sol_lwm2m_tlv_list_clear(struct sol_vector *tlvs)
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
            tlv_content.len = (content.data[offset] << 8) |
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
    sol_lwm2m_tlv_list_clear(out);
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
sol_lwm2m_tlv_get_int(struct sol_lwm2m_tlv *tlv, int64_t *value)
{
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;

    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(tlv, -EINVAL);

#define TO_LOCAL_INT_VALUE(_network_int, _local_int, _out) \
    memcpy(&(_local_int), _network_int, sizeof((_local_int))); \
    swap_bytes((uint8_t *)&(_local_int), sizeof((_local_int))); \
    *(_out) = _local_int;

    switch (tlv->content.used) {
    case 1:
        TO_LOCAL_INT_VALUE(tlv->content.data, i8, value);
        break;
    case 2:
        TO_LOCAL_INT_VALUE(tlv->content.data, i16, value);
        break;
    case 4:
        TO_LOCAL_INT_VALUE(tlv->content.data, i32, value);
        break;
    case 8:
        TO_LOCAL_INT_VALUE(tlv->content.data, i64, value);
        break;
    default:
        SOL_WRN("Invalid int size: %zu", tlv->content.used);
        return -EINVAL;
    }

    SOL_DBG("TLV has integer data. Value: %" PRId64 "", *value);
    return 0;

#undef TO_LOCAL_INT_VALUE
}

SOL_API int
sol_lwm2m_tlv_get_bool(struct sol_lwm2m_tlv *tlv, bool *value)
{
    char v;

    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(tlv, -EINVAL);
    SOL_INT_CHECK(tlv->content.used, != 1, -EINVAL);

    v = (((char *)tlv->content.data))[0];

    if (v != 0 && v != 1) {
        SOL_WRN("The TLV value is not '0' or '1'. Actual value:%d", v);
        return -EINVAL;
    }

    *value = (bool)v;
    SOL_DBG("TLV data as bool: %d", (int)*value);
    return 0;
}

SOL_API int
sol_lwm2m_tlv_get_float(struct sol_lwm2m_tlv *tlv, double *value)
{
    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(tlv, -EINVAL);

    if (tlv->content.used == 4) {
        float f;
        memcpy(&f, tlv->content.data, sizeof(float));
        swap_bytes((uint8_t *)&f, sizeof(float));
        *value = f;
    } else if (tlv->content.used == 8) {
        memcpy(value, tlv->content.data, sizeof(double));
        swap_bytes((uint8_t *)value, sizeof(double));
    } else
        return -EINVAL;

    SOL_DBG("TLV has float data. Value: %g", *value);
    return 0;
}

SOL_API int
sol_lwm2m_tlv_get_obj_link(struct sol_lwm2m_tlv *tlv,
    uint16_t *object_id, uint16_t *instance_id)
{
    int32_t i = 0;

    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(object_id, -EINVAL);
    SOL_NULL_CHECK(instance_id, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(tlv, -EINVAL);
    SOL_INT_CHECK(tlv->content.used, != OBJ_LINK_LEN, -EINVAL);


    memcpy(&i, tlv->content.data, OBJ_LINK_LEN);
    swap_bytes((uint8_t *)&i, OBJ_LINK_LEN);
    *object_id = (i >> 16) & 0xFFFF;
    *instance_id = i & 0xFFFF;

    SOL_DBG("TLV has object link value. Object id:%" PRIu16
        "  Instance id:%" PRIu16 "", *object_id, *instance_id);
    return 0;
}

SOL_API int
sol_lwm2m_tlv_get_bytes(struct sol_lwm2m_tlv *tlv, struct sol_buffer *buf)
{
    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(tlv, -EINVAL);

    return sol_buffer_append_bytes(buf, (uint8_t *)tlv->content.data, tlv->content.used);
}

SOL_API void
sol_lwm2m_resource_clear(struct sol_lwm2m_resource *resource)
{
    uint16_t i;

    SOL_NULL_CHECK(resource);
    LWM2M_RESOURCE_CHECK_API(resource);

    if (resource->data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE ||
        resource->data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_STRING) {
        for (i = 0; i < resource->data_len; i++)
            sol_blob_unref(resource->data[i].blob);
    }
    free(resource->data);
    resource->data = NULL;
}

static int
extract_path(struct sol_lwm2m_client *client, struct sol_coap_packet *req,
    uint16_t *path_id, uint16_t *path_size)
{
    struct sol_str_slice path[16] = { };
    int i, j, r;

    r = sol_coap_find_options(req, SOL_COAP_OPTION_URI_PATH, path,
        sol_util_array_size(path));
    SOL_INT_CHECK(r, < 0, r);

    for (i = client->splitted_path_len ? client->splitted_path_len : 0, j = 0;
        i < r; i++, j++) {
        char *end;
        //Only numbers are allowed.
        path_id[j] = sol_util_strtoul_n(path[i].data, &end, path[i].len, 10);
        if (end == path[i].data || end != path[i].data + path[i].len ||
            errno != 0) {
            SOL_WRN("Could not convert %.*s to integer",
                SOL_STR_SLICE_PRINT(path[i]));
            return -EINVAL;
        }
        SOL_DBG("Path ID at request: %" PRIu16 "", path_id[j]);
    }

    *path_size = j;
    return 0;
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

static void
obj_instance_clear(struct sol_lwm2m_client *client, struct obj_ctx *obj_ctx,
    struct obj_instance *obj_instance)
{
    uint16_t i;
    struct resource_ctx *res_ctx;

    SOL_VECTOR_FOREACH_IDX (&obj_instance->resources_ctx, res_ctx, i) {
        if (!client->removed && res_ctx->res) {
            sol_coap_server_unregister_resource(client->coap_server,
                res_ctx->res);
        }
        free(res_ctx->res);
        free(res_ctx->str_id);
    }

    if (!client->removed && obj_instance->instance_res) {
        sol_coap_server_unregister_resource(client->coap_server,
            obj_instance->instance_res);
    }
    free(obj_instance->instance_res);
    free(obj_instance->str_id);
    sol_vector_clear(&obj_instance->resources_ctx);
}

static int
setup_object_resource(struct sol_lwm2m_client *client, struct obj_ctx *obj_ctx)
{
    int r;
    uint16_t segments = 2, i = 0;

    r = asprintf(&obj_ctx->str_id, "%" PRIu16 "", obj_ctx->obj->id);
    SOL_INT_CHECK(r, == -1, -ENOMEM);

    if (client->splitted_path)
        segments += client->splitted_path_len;

    obj_ctx->obj_res = calloc(1, sizeof(struct sol_coap_resource) +
        (sizeof(struct sol_str_slice) * segments));
    SOL_NULL_CHECK_GOTO(obj_ctx->obj_res, err_exit);

    SOL_SET_API_VERSION(obj_ctx->obj_res->api_version = SOL_COAP_RESOURCE_API_VERSION; )

    if (client->splitted_path) {
        uint16_t j;
        for (j = 0; j < client->splitted_path_len; j++)
            obj_ctx->obj_res->path[i++] = sol_str_slice_from_str(client->splitted_path[j]);
    }
    obj_ctx->obj_res->path[i++] = sol_str_slice_from_str(obj_ctx->str_id);
    obj_ctx->obj_res->path[i++] = sol_str_slice_from_str("");

    obj_ctx->obj_res->get = handle_resource;
    obj_ctx->obj_res->post = handle_resource;
    return 0;

err_exit:
    free(obj_ctx->str_id);
    return -ENOMEM;
}

static int
setup_resources_ctx(struct sol_lwm2m_client *client, struct obj_ctx *obj_ctx,
    struct obj_instance *instance, bool register_with_coap)
{
    uint16_t i, j, segments = 4;
    struct resource_ctx *res_ctx;
    int r;

    if (client->splitted_path)
        segments += client->splitted_path_len;

    for (i = 0; i < obj_ctx->obj->resources_count; i++) {
        j = 0;
        res_ctx = sol_vector_append(&instance->resources_ctx);
        SOL_NULL_CHECK_GOTO(res_ctx, err_exit);

        res_ctx->res = calloc(1, sizeof(struct sol_coap_resource) +
            (sizeof(struct sol_str_slice) * segments));

        SOL_NULL_CHECK_GOTO(res_ctx->res, err_exit);

        r = asprintf(&res_ctx->str_id, "%" PRIu16 "", i);
        SOL_INT_CHECK_GOTO(r, == -1, err_exit);
        res_ctx->id = i;

        SOL_SET_API_VERSION(res_ctx->res->api_version = SOL_COAP_RESOURCE_API_VERSION; )

        if (client->splitted_path) {
            uint16_t k;
            for (k = 0; k < client->splitted_path_len; k++)
                res_ctx->res->path[j++] = sol_str_slice_from_str(client->splitted_path[k]);
        }
        res_ctx->res->path[j++] = sol_str_slice_from_str(obj_ctx->str_id);
        res_ctx->res->path[j++] = sol_str_slice_from_str(instance->str_id);
        res_ctx->res->path[j++] = sol_str_slice_from_str(res_ctx->str_id);
        res_ctx->res->path[j++] = sol_str_slice_from_str("");

        res_ctx->res->get = handle_resource;
        res_ctx->res->post = handle_resource;
        res_ctx->res->put = handle_resource;
        res_ctx->res->del = handle_resource;

        if (register_with_coap) {
            r = sol_coap_server_register_resource(client->coap_server,
                res_ctx->res, client);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }
    }

    return 0;

err_exit:
    SOL_VECTOR_FOREACH_IDX (&instance->resources_ctx, res_ctx, i) {
        if (res_ctx->res) {
            sol_coap_server_unregister_resource(client->coap_server,
                res_ctx->res);
            free(res_ctx->res);
        }
        free(res_ctx->str_id);
    }
    sol_vector_clear(&instance->resources_ctx);
    return -ENOMEM;
}

static int
setup_instance_resource(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance,
    bool register_with_coap)
{
    int r;
    uint16_t i = 0, segments = 3;

    if (client->splitted_path)
        segments += client->splitted_path_len;

    r = asprintf(&obj_instance->str_id, "%" PRIu16 "", obj_instance->id);
    SOL_INT_CHECK(r, == -1, -ENOMEM);

    obj_instance->instance_res = calloc(1, sizeof(struct sol_coap_resource) +
        (sizeof(struct sol_str_slice) * segments));
    SOL_NULL_CHECK_GOTO(obj_instance->instance_res, err_exit);

    SOL_SET_API_VERSION(obj_instance->instance_res->api_version = SOL_COAP_RESOURCE_API_VERSION; )

    if (client->splitted_path) {
        uint16_t j;
        for (j = 0; j < client->splitted_path_len; j++)
            obj_instance->instance_res->path[i++] =
                sol_str_slice_from_str(client->splitted_path[j]);
    }
    obj_instance->instance_res->path[i++] =
        sol_str_slice_from_str(obj_ctx->str_id);
    obj_instance->instance_res->path[i++] =
        sol_str_slice_from_str(obj_instance->str_id);
    obj_instance->instance_res->path[i++] = sol_str_slice_from_str("");

    obj_instance->instance_res->get = handle_resource;
    obj_instance->instance_res->post = handle_resource;
    obj_instance->instance_res->put = handle_resource;
    obj_instance->instance_res->del = handle_resource;

    if (register_with_coap) {
        r = sol_coap_server_register_resource(client->coap_server,
            obj_instance->instance_res, client);
        SOL_INT_CHECK_GOTO(r, < 0, err_register);
    }

    r = setup_resources_ctx(client, obj_ctx, obj_instance, register_with_coap);
    SOL_INT_CHECK_GOTO(r, < 0, err_resources);

    return 0;

err_resources:
    sol_coap_server_unregister_resource(client->coap_server,
        obj_instance->instance_res);
err_register:
    free(obj_instance->instance_res);
    obj_instance->instance_res = NULL;
err_exit:
    free(obj_instance->str_id);
    obj_instance->str_id = NULL;
    return -ENOMEM;
}

static void
clear_bootstrap_ctx(struct sol_lwm2m_client *client)
{
    if (client->bootstrap_ctx.timeout) {
        sol_timeout_del(client->bootstrap_ctx.timeout);
        sol_blob_unref(client->bootstrap_ctx.server_uri);
        client->bootstrap_ctx.timeout = NULL;
        client->bootstrap_ctx.server_uri = NULL;
    }
}

static uint8_t
handle_delete(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance)
{
    int r, ret = SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
    uint16_t i;

    //Specific Instance?
    if (obj_ctx && obj_instance) {
        if (!obj_ctx->obj->del) {
            SOL_WRN("The object %" PRIu16 " does not implement the delete method",
                obj_ctx->obj->id);
            return client->is_bootstrapping ? SOL_COAP_RESPONSE_CODE_BAD_REQUEST :
                   SOL_COAP_RESPONSE_CODE_NOT_ALLOWED;
        }

        r = obj_ctx->obj->del((void *)obj_instance->data,
            (void *)client->user_data, client, obj_instance->id);
        if (r < 0) {
            SOL_WRN("Could not properly delete object id %"
                PRIu16 " instance id: %" PRIu16 " reason:%d",
                obj_ctx->obj->id, obj_instance->id, r);
            return client->is_bootstrapping ? SOL_COAP_RESPONSE_CODE_BAD_REQUEST :
                   SOL_COAP_RESPONSE_CODE_NOT_ALLOWED;
        }

        obj_instance->should_delete = true;
        ret = SOL_COAP_RESPONSE_CODE_DELETED;
    } else if (client->is_bootstrapping) {
        SOL_VECTOR_FOREACH_IDX (&client->objects, obj_ctx, i) {
            if (!obj_ctx->obj->del) {
                SOL_WRN("The object %" PRIu16 " does not implement the delete method."
                    " Skipping this Object.",
                    obj_ctx->obj->id);
                continue;
            }

            while ((obj_instance = sol_vector_get(&obj_ctx->instances, 0)) != NULL) {
                r = obj_ctx->obj->del((void *)obj_instance->data,
                    (void *)client->user_data, client, obj_instance->id);

                if (r < 0) {
                    SOL_WRN("Could not properly delete object id %"
                        PRIu16 " instance id: %" PRIu16 " reason:%d",
                        obj_ctx->obj->id, obj_instance->id, r);
                    ret = SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
                } else {
                    SOL_DBG("Deleted object id %"
                        PRIu16 " instance id: %" PRIu16,
                        obj_ctx->obj->id, obj_instance->id);
                    obj_instance_clear(client, obj_ctx, obj_instance);
                    (void)sol_vector_del_element(&obj_ctx->instances, obj_instance);
                    ret = SOL_COAP_RESPONSE_CODE_DELETED;
                }
            }
        }
    }

    return ret;
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
            if (isdigit((uint8_t)args.data[i]))
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
    uint16_t resource, struct sol_lwm2m_payload payload)
{
    int r;

    if (!obj_instance) {
        SOL_WRN("Object instance was not provided to execute the path"
            "/%" PRIu16 "/?/%" PRIu16 "", obj_ctx->obj->id, resource);
        return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
    }

    if (!obj_ctx->obj->execute) {
        SOL_WRN("Obj id %" PRIu16 " does not implemet the execute",
            obj_ctx->obj->id);
        return SOL_COAP_RESPONSE_CODE_NOT_ALLOWED;
    }

    if (payload.type != SOL_LWM2M_CONTENT_TYPE_TEXT) {
        SOL_WRN("Only text payload is valid for execution");
        return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
    }

    if (!is_valid_args(payload.payload.slice_content)) {
        SOL_WRN("Invalid arguments. Args: %.*s",
            SOL_STR_SLICE_PRINT(payload.payload.slice_content));
        return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
    }

    r = obj_ctx->obj->execute((void *)obj_instance->data,
        (void *)client->user_data, client, obj_instance->id, resource,
        payload.payload.slice_content);

    if (r < 0) {
        SOL_WRN("Could not execute the path /%" PRIu16
            "/%" PRIu16 "/%" PRIu16 " with args: %.*s", obj_ctx->obj->id,
            obj_instance->id, resource, SOL_STR_SLICE_PRINT(payload.payload.slice_content));
        return SOL_COAP_RESPONSE_CODE_NOT_ALLOWED;
    }

    return SOL_COAP_RESPONSE_CODE_CHANGED;
}

static uint8_t
write_instance_tlv_or_resource(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance,
    int32_t resource, struct sol_lwm2m_payload payload)
{
    int r;

    //If write_resource is not NULL then write_tlv is guaranteed to be valid as well.
    if (!obj_ctx->obj->write_resource) {
        SOL_WRN("Object %" PRIu16 " does not support the write method",
            obj_ctx->obj->id);
        return SOL_COAP_RESPONSE_CODE_NOT_ALLOWED;
    }

    if (!obj_instance) {
        SOL_WRN("Object instance was not provided."
            " Can not complete the write operation");
        return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
    }

    if (payload.type == SOL_LWM2M_CONTENT_TYPE_TLV) {
        r = obj_ctx->obj->write_tlv((void *)obj_instance->data,
            (void *)client->user_data, client, obj_instance->id, &payload.payload.tlv_content);
        SOL_INT_CHECK(r, < 0, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);
    } else if (payload.type == SOL_LWM2M_CONTENT_TYPE_TEXT ||
        payload.type == SOL_LWM2M_CONTENT_TYPE_OPAQUE) {
        struct sol_lwm2m_resource res;
        struct sol_blob *blob;

        if (resource < 0) {
            SOL_WRN("Unexpected content format (%" PRIu16
                "). It must be TLV", payload.type);
            return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
        }

        blob = sol_blob_new(&SOL_BLOB_TYPE_NO_FREE_DATA, NULL,
            payload.payload.slice_content.data, payload.payload.slice_content.len);
        SOL_NULL_CHECK(blob, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);

        SOL_SET_API_VERSION(res.api_version = SOL_LWM2M_RESOURCE_API_VERSION; )
        r = sol_lwm2m_resource_init(&res, resource, 1,
            payload.type == SOL_LWM2M_CONTENT_TYPE_TEXT ?
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING :
            SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE, blob);
        sol_blob_unref(blob);
        SOL_INT_CHECK(r, < 0, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);
        r = obj_ctx->obj->write_resource((void *)obj_instance->data,
            (void *)client->user_data, client, obj_instance->id, res.id, &res);
        sol_lwm2m_resource_clear(&res);
        SOL_INT_CHECK(r, < 0, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);
    } else {
        SOL_WRN("Only TLV, string or opaque is supported for writing."
            " Received: %" PRIu16 "", payload.type);
        return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
    }

    return SOL_COAP_RESPONSE_CODE_CHANGED;
}

static uint8_t
handle_create(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, int32_t instance_id, struct sol_lwm2m_payload payload)
{
    int r;
    struct obj_instance *obj_instance;

    if (!obj_ctx->obj->create) {
        SOL_WRN("Object %" PRIu16 " does not support the create method",
            obj_ctx->obj->id);
        return SOL_COAP_RESPONSE_CODE_NOT_ALLOWED;
    }

    obj_instance = sol_vector_append(&obj_ctx->instances);
    SOL_NULL_CHECK(obj_instance, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);

    if (instance_id < 0)
        obj_instance->id = obj_ctx->instances.len - 1;
    else
        obj_instance->id = instance_id;

    sol_vector_init(&obj_instance->resources_ctx, sizeof(struct resource_ctx));

    r = obj_ctx->obj->create((void *)client->user_data, client,
        obj_instance->id, (void *)&obj_instance->data, payload);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    r = setup_instance_resource(client, obj_ctx, obj_instance, !client->is_bootstrapping);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    return SOL_COAP_RESPONSE_CODE_CREATED;

err_exit:
    obj_instance_clear(client, obj_ctx, obj_instance);
    return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
}

static uint8_t
handle_write(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance,
    uint16_t *path, uint16_t path_size,
    struct sol_lwm2m_payload payload)
{
    int r;

    if (path_size < 2 && client->is_bootstrapping) {
        //Bootstrap Write on Object (e.g.: PUT /1)
        //In this case the payload is composed of <multiple TLVs> of type OBJECT_INSTANCE,
        // <each one containing multiple TLVs> of type MULTIPLE_RESOURCES or RESOURCE_WITH_VALUE
        // and each TLV of type MULTIPLE_RESOURCES can contain multiple TLVs of type
        // RESOURCE_INSTANCE
        struct sol_lwm2m_tlv *instance_tlvs;
        uint16_t i;

        if (payload.type == SOL_LWM2M_CONTENT_TYPE_TLV) {
            SOL_VECTOR_FOREACH_IDX (&payload.payload.tlv_content, instance_tlvs, i) {
                struct sol_lwm2m_payload instance_payload = { .type = SOL_LWM2M_CONTENT_TYPE_TLV };

                if (instance_tlvs->type != SOL_LWM2M_TLV_TYPE_OBJECT_INSTANCE) {
                    SOL_WRN("Only TLV is supported for writing an individual Object Instance."
                        " Received: %" PRIu16 ". Skipping this instance.", instance_tlvs->type);
                    continue;
                }

                sol_vector_init(&instance_payload.payload.tlv_content,
                    sizeof(struct sol_lwm2m_tlv));

                instance_payload.payload.tlv_content.data =
                    (unsigned char *)payload.payload.tlv_content.data +
                    (payload.payload.tlv_content.elem_size * (i + 1));
                instance_payload.payload.tlv_content.len = instance_tlvs->content.used;

                i += instance_tlvs->content.used;

                obj_instance = find_object_instance_by_instance_id(obj_ctx, instance_tlvs->id);

                //Instance already exists?
                if (obj_instance) {
                    r = write_instance_tlv_or_resource(client, obj_ctx, obj_instance, -1,
                        instance_payload);
                } else {
                    r = handle_create(client, obj_ctx, instance_tlvs->id, instance_payload);
                }

                if (r == SOL_COAP_RESPONSE_CODE_CHANGED || r == SOL_COAP_RESPONSE_CODE_CREATED) {
                    SOL_DBG("Bootstrap Write on Object Instance /%"
                        PRIu16 "/%" PRIu16 " succeeded!", obj_ctx->obj->id, instance_tlvs->id);
                } else {
                    SOL_WRN("Bootstrap Write on Object Instance /%"
                        PRIu16 "/%" PRIu16 " failed!", obj_ctx->obj->id, instance_tlvs->id);
                    return r;
                }
            }

            SOL_DBG("Bootstrap Write on Object /%" PRIu16 " succeeded!", obj_ctx->obj->id);

            return SOL_COAP_RESPONSE_CODE_CHANGED;
        } else {
            SOL_WRN("Only TLV is supported for writing multiple Object Instances."
                " Received: %" PRIu16 "", payload.type);
            return SOL_COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT;
        }

    } else if (path_size < 3 && client->is_bootstrapping) {
        //Bootstrap Write on Object Instance (e.g.: PUT /1/5)
        //In this case the payload is composed of <multiple TLVs> of type MULTIPLE_RESOURCES
        // or RESOURCE_WITH_VALUE and each TLV of type MULTIPLE_RESOURCES can contain
        // multiple TLVs of type RESOURCE_INSTANCE
        if (payload.type == SOL_LWM2M_CONTENT_TYPE_TLV) {
            //Instance already exists?
            if (obj_instance) {
                r = write_instance_tlv_or_resource(client, obj_ctx, obj_instance, -1, payload);
            } else {
                r = handle_create(client, obj_ctx, path[1], payload);
            }

            if (r == SOL_COAP_RESPONSE_CODE_CHANGED || r == SOL_COAP_RESPONSE_CODE_CREATED) {
                SOL_DBG("Bootstrap Write on Object Instance /%"
                    PRIu16 "/%" PRIu16 " succeeded!", obj_ctx->obj->id, path[1]);
                return SOL_COAP_RESPONSE_CODE_CHANGED;
            } else {
                SOL_WRN("Bootstrap Write on Object Instance /%"
                    PRIu16 "/%" PRIu16 " failed!", obj_ctx->obj->id, path[1]);
                return r;
            }
        } else {
            SOL_WRN("Only TLV is supported for writing Object Instance."
                " Received: %" PRIu16 "", payload.type);
            return SOL_COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT;
        }

    } else {
        //{Bootstrap Write on Resource (e.g.: PUT /1/5/1)
        //In this case the payload is composed of a <single TLV> of type MULTIPLE_RESOURCES
        // or RESOURCE_WITH_VALUE and in case of TLV of type MULTIPLE_RESOURCES this TLV can contain
        // multiple TLVs of type RESOURCE_INSTANCE} or
        //{Management Write as [Replace] on Object Instance (e.g.: PUT /1/5) or Resource (e.g.: PUT /1/5/1)
        // or as [Partial Update] on Object Instance (e.g.: POST /1/5)}
        r = write_instance_tlv_or_resource(client, obj_ctx, obj_instance, path[2], payload);

        if (r == SOL_COAP_RESPONSE_CODE_CHANGED || r == SOL_COAP_RESPONSE_CODE_CREATED) {
            SOL_DBG("Bootstrap/Management Write on Resource /%"
                PRIu16 "/%" PRIu16 "/%" PRIu16 " succeeded!",
                obj_ctx->obj->id, obj_instance->id, path[2]);
            return SOL_COAP_RESPONSE_CODE_CHANGED;
        } else {
            SOL_WRN("Bootstrap/Management Write on Resource /%"
                PRIu16 "/%" PRIu16 "/%" PRIu16 " failed!",
                obj_ctx->obj->id, obj_instance->id, path[2]);
            return r;
        }
    }
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
            (void *)client->user_data, client, obj_instance->id, i, res);

        if (r == -ENOENT) {
            (void)sol_vector_del_element(resources, res);
            continue;
        }
        if (r == -EINVAL) {
            (void)sol_vector_del_element(resources, res);
            break;
        }
        LWM2M_RESOURCE_CHECK_API_GOTO(*res, err_api);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    return 0;

#ifndef SOL_NO_API_VERSION
err_api:
    r = -EINVAL;
#endif
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
        return SOL_COAP_RESPONSE_CODE_NOT_ALLOWED;
    }

    if (obj_instance && resource_id >= 0) {
        res = sol_vector_append(&resources);
        SOL_NULL_CHECK(res, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);

        r = obj_ctx->obj->read((void *)obj_instance->data,
            (void *)client->user_data, client,
            obj_instance->id, resource_id, res);

        if (r == -ENOENT || r == -EINVAL) {
            sol_vector_clear(&resources);
            return SOL_COAP_RESPONSE_CODE_NOT_FOUND;
        }

        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        LWM2M_RESOURCE_CHECK_API_GOTO(*res, err_exit);
    } else if (obj_instance) {
        r = read_object_instance(client, obj_ctx, obj_instance, &resources);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    } else {
        struct obj_instance *instance;

        SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, instance, i) {
            if (instance->should_delete)
                continue;
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
    return SOL_COAP_RESPONSE_CODE_CONTENT;

err_exit:
    SOL_VECTOR_FOREACH_IDX (&resources, res, i)
        sol_lwm2m_resource_clear(res);
    sol_buffer_fini(&buf);
    sol_vector_clear(&resources);
    return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
}

static bool
send_notification_pkt(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance,
    int32_t resource_id, struct sol_coap_resource *resource)
{
    struct sol_coap_packet *pkt;
    uint8_t ret;
    int r;

    pkt = sol_coap_packet_new_notification(client->coap_server, resource);
    SOL_NULL_CHECK(pkt, false);

    r = sol_coap_header_set_type(pkt, SOL_COAP_MESSAGE_TYPE_CON);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    r = sol_coap_header_set_code(pkt, SOL_COAP_RESPONSE_CODE_CHANGED);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    ret = handle_read(client, obj_ctx, obj_instance, resource_id, pkt);
    SOL_INT_CHECK_GOTO(ret, != SOL_COAP_RESPONSE_CODE_CONTENT, err_exit);

    return sol_coap_notify(client->coap_server,
        resource, pkt) == 0;

err_exit:
    sol_coap_packet_unref(pkt);
    return false;
}

static bool
dispatch_notifications(struct sol_lwm2m_client *client,
    const struct sol_coap_resource *resource, bool is_delete)
{
    uint16_t i, path_idx = 0;
    struct obj_ctx *obj_ctx;
    bool stop = false, r;

    if (client->splitted_path_len)
        path_idx = client->splitted_path_len;

    SOL_VECTOR_FOREACH_IDX (&client->objects, obj_ctx, i) {
        struct obj_instance *instance;
        uint16_t j;

        if (!sol_str_slice_eq(obj_ctx->obj_res->path[path_idx],
            resource->path[path_idx]))
            continue;

        r = send_notification_pkt(client, obj_ctx, NULL, -1, obj_ctx->obj_res);
        SOL_EXP_CHECK(!r, false);

        if (!resource->path[1].len || is_delete)
            break;

        SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, instance, j) {
            uint16_t k;
            struct resource_ctx *res_ctx;

            if (!sol_str_slice_eq(instance->instance_res->path[path_idx + 1],
                resource->path[path_idx + 1]))
                continue;

            r = send_notification_pkt(client, obj_ctx, instance, -1,
                instance->instance_res);
            SOL_EXP_CHECK(!r, false);

            if (!resource->path[2].len) {
                stop = true;
                break;
            }

            SOL_VECTOR_FOREACH_IDX (&instance->resources_ctx, res_ctx, k) {
                if (!sol_str_slice_eq(res_ctx->res->path[path_idx + 2],
                    resource->path[path_idx + 2]))
                    continue;

                r = send_notification_pkt(client, obj_ctx, instance, k,
                    res_ctx->res);
                SOL_EXP_CHECK(!r, false);
                stop = true;
                break;
            }

            if (stop)
                break;
        }

        if (stop)
            break;
    }

    return true;
}

static bool
is_observe_request(struct sol_coap_packet *req)
{
    const void *obs;
    uint16_t len;

    obs = sol_coap_find_first_option(req, SOL_COAP_OPTION_OBSERVE, &len);

    if (!obs)
        return false;

    return true;
}

static bool
should_dispatch_notifications(uint8_t code, bool is_execute)
{
    if (code == SOL_COAP_RESPONSE_CODE_CREATED ||
        code == SOL_COAP_RESPONSE_CODE_DELETED ||
        (code == SOL_COAP_RESPONSE_CODE_CHANGED && !is_execute))
        return true;
    return false;
}

static int
handle_resource(void *data, struct sol_coap_server *server,
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    int r;
    uint8_t method;
    struct sol_coap_packet *resp;
    struct sol_lwm2m_client *client = data;
    struct obj_ctx *obj_ctx = NULL;
    struct obj_instance *obj_instance = NULL;
    uint16_t path[3], path_size = 0, content_format;
    uint8_t header_code;
    bool is_execute = false;
    struct sol_lwm2m_payload payload = { 0 };

    if (client->is_bootstrapping) {
        clear_bootstrap_ctx(client);
    }

    resp = sol_coap_packet_new(req);
    SOL_NULL_CHECK(resp, -ENOMEM);

    r = get_coap_int_option(req, SOL_COAP_OPTION_CONTENT_FORMAT,
        &content_format);

    if (r < 0)
        payload.type = SOL_LWM2M_CONTENT_TYPE_TEXT;
    else
        payload.type = content_format;

    if (payload.type == SOL_LWM2M_CONTENT_TYPE_JSON) {
        SOL_WRN("JSON content format is not supported");
        header_code = SOL_COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT;
        goto exit;
    }

    r = extract_path(client, req, path, &path_size);
    header_code = SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    if (path_size >= 1) {
        obj_ctx = find_object_ctx_by_id(client, path[0]);
        header_code = client->is_bootstrapping ? SOL_COAP_RESPONSE_CODE_NOT_FOUND :
            SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
        SOL_NULL_CHECK_GOTO(obj_ctx, exit);
    }
    if (path_size >= 2)
        obj_instance = find_object_instance_by_instance_id(obj_ctx, path[1]);

    if (sol_coap_packet_has_payload(req)) {
        struct sol_buffer *buf;
        struct sol_str_slice slice;
        size_t offset;

        r = sol_coap_packet_get_payload(req, &buf, &offset);
        header_code = SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
        SOL_INT_CHECK_GOTO(r, < 0, exit);

        slice.len = buf->used - offset;
        slice.data = sol_buffer_at(buf, offset);

        if (payload.type == SOL_LWM2M_CONTENT_TYPE_TLV) {
            r = sol_lwm2m_parse_tlv(slice, &payload.payload.tlv_content);
            header_code = SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
            SOL_INT_CHECK_GOTO(r, < 0, exit);
        } else
            payload.payload.slice_content = slice;
    }

    sol_coap_header_get_code(req, &method);

    if (client->is_bootstrapping &&
        (method == SOL_COAP_METHOD_GET || method == SOL_COAP_METHOD_POST)) {
        header_code = SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
        goto exit;
    }

    switch (method) {
    case SOL_COAP_METHOD_GET:
        if (is_observe_request(req)) {
            uint8_t obs = 1;
            r = add_coap_int_option(resp, SOL_COAP_OPTION_OBSERVE,
                &obs, sizeof(obs));
            SOL_INT_CHECK_GOTO(r, < 0, exit);
        }
        header_code = handle_read(client, obj_ctx, obj_instance,
            path_size > 2 ? path[2] : -1, resp);
        break;
    case SOL_COAP_METHOD_POST:
        if (path_size == 1)
            //This is a create op
            header_code = handle_create(client, obj_ctx, -1, payload);
        else if (path_size == 2 && !obj_instance)
            //This is a create with chosen by the LWM2M server.
            header_code = handle_create(client, obj_ctx, path[1], payload);
        else if (path_size == 2)
            //Management Write on object instance
            header_code = handle_write(client, obj_ctx, obj_instance, path, path_size, payload);
        else {
            //Execute.
            is_execute = true;
            header_code = handle_execute(client, obj_ctx, obj_instance, path[2], payload);
        }
        break;
    case SOL_COAP_METHOD_PUT:
        if ((path_size == 3 && !client->is_bootstrapping) ||
            client->is_bootstrapping)
            //Bootstrap Write on Obj, ObjInst or Res; or Management Write on a resource.
            header_code = handle_write(client, obj_ctx, obj_instance, path, path_size, payload);
        else {
            header_code = SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
            SOL_WRN("Write request without full path specified!");
        }
        break;
    case SOL_COAP_METHOD_DELETE:
        header_code = handle_delete(client, obj_ctx, obj_instance);
        break;
    default:
        header_code = SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
        SOL_WRN("Unknown COAP method: %" PRIu8 "", method);
    }

exit:
    sol_coap_header_set_code(resp, header_code);
    r = sol_coap_send_packet(server, resp, cliaddr);

    if (should_dispatch_notifications(header_code, is_execute) &&
        resource &&
        !dispatch_notifications(client, resource,
        header_code == SOL_COAP_RESPONSE_CODE_DELETED)) {
        SOL_WRN("Could not dispatch the observe notifications");
    }

    if (header_code == SOL_COAP_RESPONSE_CODE_DELETED &&
        !client->is_bootstrapping) {
        obj_instance_clear(client, obj_ctx, obj_instance);
        (void)sol_vector_del_element(&obj_ctx->instances, obj_instance);
    }

    if (payload.type == SOL_LWM2M_CONTENT_TYPE_TLV)
        sol_lwm2m_tlv_list_clear(&payload.payload.tlv_content);

    return r;
}

static int
handle_unknown_bootstrap_resource(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    return handle_resource(data, server, NULL, req, cliaddr);
}

static char **
split_path(const char *path, uint16_t *splitted_path_len)
{
    char **splitted_path;
    struct sol_vector tokens;
    struct sol_str_slice *token;
    uint16_t i;

    tokens = sol_str_slice_split(sol_str_slice_from_str(path), "/", 0);

    if (!tokens.len)
        return NULL;

    splitted_path = calloc(tokens.len, sizeof(char *));
    SOL_NULL_CHECK_GOTO(splitted_path, err_exit);

    SOL_VECTOR_FOREACH_IDX (&tokens, token, i) {
        splitted_path[i] = sol_str_slice_to_str(*token);
        SOL_NULL_CHECK_GOTO(splitted_path[i], err_cpy);
    }

    *splitted_path_len = tokens.len;
    sol_vector_clear(&tokens);
    return splitted_path;

err_cpy:
    for (i = 0; i < tokens.len; i++)
        free(splitted_path[i]);
    free(splitted_path);
err_exit:
    sol_vector_clear(&tokens);
    return NULL;
}

SOL_API struct sol_lwm2m_client *
sol_lwm2m_client_new(const char *name, const char *path, const char *sms,
    const struct sol_lwm2m_object **objects, const void *data)
{
    struct sol_lwm2m_client *client;
    struct obj_ctx *obj_ctx;
    size_t i;
    int r;
    struct sol_network_link_addr servaddr = { .family = SOL_NETWORK_FAMILY_INET6,
                                              .port = 0 };

    SOL_NULL_CHECK(name, NULL);
    SOL_NULL_CHECK(objects, NULL);
    SOL_NULL_CHECK(objects[0], NULL);

    SOL_LOG_INTERNAL_INIT_ONCE;

    client = calloc(1, sizeof(struct sol_lwm2m_client));
    SOL_NULL_CHECK(client, NULL);

    if (path) {
        client->splitted_path = split_path(path, &client->splitted_path_len);
        SOL_NULL_CHECK_GOTO(client->splitted_path, err_path);
    }

    sol_vector_init(&client->objects, sizeof(struct obj_ctx));
    sol_ptr_vector_init(&client->connections);

    for (i = 0; objects[i]; i++) {
        LWM2M_OBJECT_CHECK_API_GOTO(*objects[i], err_obj);
        SOL_INT_CHECK_GOTO(objects[i]->resources_count, == 0, err_obj);
        obj_ctx = sol_vector_append(&client->objects);
        SOL_NULL_CHECK_GOTO(obj_ctx, err_obj);
        if ((objects[i]->write_resource && !objects[i]->write_tlv) ||
            (!objects[i]->write_resource && objects[i]->write_tlv)) {
            SOL_WRN("write_resource and write_tlv must be provided!");
            goto err_obj;
        }
        obj_ctx->obj = objects[i];
        sol_vector_init(&obj_ctx->instances, sizeof(struct obj_instance));
        r = setup_object_resource(client, obj_ctx);
        SOL_INT_CHECK_GOTO(r, < 0, err_obj);
    }

    client->name = strdup(name);
    SOL_NULL_CHECK_GOTO(client->name, err_obj);

    if (sms) {
        client->sms = strdup(sms);
        SOL_NULL_CHECK_GOTO(client->sms, err_sms);
    }

    client->coap_server = sol_coap_server_new(&servaddr, false);
    SOL_NULL_CHECK_GOTO(client->coap_server, err_coap);

    sol_monitors_init(&client->bootstrap, NULL);

    client->user_data = data;

    return client;

err_coap:
    free(client->sms);
err_sms:
    free(client->name);
err_obj:
    SOL_VECTOR_FOREACH_IDX (&client->objects, obj_ctx, i) {
        free(obj_ctx->str_id);
        free(obj_ctx->obj_res);
    }
    sol_vector_clear(&client->objects);
    for (i = 0; i < client->splitted_path_len; i++) {
        free(client->splitted_path[i]);
    }
    free(client->splitted_path);
err_path:
    free(client);
    return NULL;
}

static void
obj_ctx_clear(struct sol_lwm2m_client *client, struct obj_ctx *ctx)
{
    uint16_t i;
    struct obj_instance *instance;

    SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, i) {
        if (ctx->obj->del) {
            ctx->obj->del((void *)instance->data,
                (void *)client->user_data, client, instance->id);
        }
        obj_instance_clear(client, ctx, instance);
    }
    sol_vector_clear(&ctx->instances);
    free(ctx->obj_res);
    free(ctx->str_id);
}

static void
server_connection_ctx_free(struct server_conn_ctx *conn_ctx)
{
    if (conn_ctx->pending_pkt)
        sol_coap_packet_unref(conn_ctx->pending_pkt);
    if (conn_ctx->hostname_handle)
        sol_network_hostname_pending_cancel(conn_ctx->hostname_handle);
    sol_vector_clear(&conn_ctx->server_addr_list);
    free(conn_ctx->location);
    free(conn_ctx);
}

static void
server_connection_ctx_remove(struct sol_ptr_vector *conns,
    struct server_conn_ctx *conn_ctx)
{
    server_connection_ctx_free(conn_ctx);
    (void)sol_ptr_vector_del_element(conns, conn_ctx);
}

static void
server_connection_ctx_list_clear(struct sol_ptr_vector *conns)
{
    uint16_t i;
    struct server_conn_ctx *conn_ctx;

    SOL_PTR_VECTOR_FOREACH_IDX (conns, conn_ctx, i)
        server_connection_ctx_free(conn_ctx);
    sol_ptr_vector_clear(conns);
}

SOL_API void
sol_lwm2m_client_del(struct sol_lwm2m_client *client)
{
    uint16_t i;
    struct obj_ctx *ctx;

    SOL_NULL_CHECK(client);
    client->removed = true;

    clear_bootstrap_ctx(client);

    sol_coap_server_unref(client->coap_server);

    SOL_VECTOR_FOREACH_IDX (&client->objects, ctx, i)
        obj_ctx_clear(client, ctx);

    server_connection_ctx_list_clear(&client->connections);
    sol_vector_clear(&client->objects);
    sol_monitors_clear(&client->bootstrap);
    free(client->name);
    if (client->splitted_path) {
        for (i = 0; i < client->splitted_path_len; i++)
            free(client->splitted_path[i]);
        free(client->splitted_path);
    }
    free(client->sms);
    free(client);
}

SOL_API int
sol_lwm2m_client_add_object_instance(struct sol_lwm2m_client *client,
    const struct sol_lwm2m_object *obj, const void *data)
{
    struct obj_ctx *ctx;
    struct obj_instance *instance;
    int r;

    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(obj, -EINVAL);
    LWM2M_OBJECT_CHECK_API(*obj, -EINVAL);

    ctx = find_object_ctx_by_id(client, obj->id);
    SOL_NULL_CHECK(ctx, -ENOENT);

    instance = sol_vector_append(&ctx->instances);
    SOL_NULL_CHECK(instance, -ENOMEM);
    instance->id = ctx->instances.len - 1;
    instance->data = data;
    sol_vector_init(&instance->resources_ctx, sizeof(struct resource_ctx));

    r = setup_instance_resource(client, ctx, instance, false);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    return 0;

err_exit:
    (void)sol_vector_del_element(&ctx->instances, instance);
    return r;
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
            (void *)client->user_data, client, instance->id,
            (uint16_t)va_arg(ap, int), &res[i]);
        LWM2M_RESOURCE_CHECK_API_GOTO(res[i], err_exit_api);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    va_end(ap);
    return 0;

#ifndef SOL_NO_API_VERSION
err_exit_api:
    r = -EINVAL;
#endif
err_exit:
    clear_resource_array(res, i);
    va_end(ap);
    return r;
}

static int
get_binding_and_lifetime(struct sol_lwm2m_client *client, int64_t server_id,
    int64_t *lifetime, struct sol_blob **binding)
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
        r = read_resources(client, ctx, instance, res, sol_util_array_size(res),
            SERVER_OBJECT_SERVER_ID, SERVER_OBJECT_LIFETIME,
            SERVER_OBJECT_BINDING);
        SOL_INT_CHECK(r, < 0, r);

        if (res[0].data[0].integer == server_id) {
            r = -EINVAL;
            SOL_INT_CHECK_GOTO(get_binding_mode_from_str(sol_str_slice_from_blob(res[2].data[0].blob)),
                == SOL_LWM2M_BINDING_MODE_UNKNOWN, exit);
            *lifetime = res[1].data[0].integer;
            *binding = sol_blob_ref(res[2].data[0].blob);
            r = 0;
            goto exit;
        }
        clear_resource_array(res, sol_util_array_size(res));
    }

    return -ENOENT;

exit:
    clear_resource_array(res, sol_util_array_size(res));
    return r;
}

static int
setup_objects_payload(struct sol_lwm2m_client *client, struct sol_buffer *objs)
{
    uint16_t i, j;
    struct obj_ctx *ctx;
    struct obj_instance *instance;
    int r;

    sol_buffer_init(objs);

    if (client->splitted_path) {
        r = sol_buffer_append_slice(objs, sol_str_slice_from_str("</"));
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        for (i = 0; i < client->splitted_path_len; i++) {
            r = sol_buffer_append_printf(objs, "%s/", client->splitted_path[i]);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }
        //Remove the last '/'
        objs->used--;
        r = sol_buffer_append_slice(objs,
            sol_str_slice_from_str(">;rt=\"oma.lwm2m\","));
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
    uint32_t smallest, remaining, lf = 0;
    struct server_conn_ctx *conn_ctx;
    bool has_connection = false;
    time_t now;
    int r;

    now = time(NULL);
    smallest = UINT32_MAX;

    SOL_PTR_VECTOR_FOREACH_IDX (&client->connections, conn_ctx, i) {
        if (!conn_ctx->location)
            continue;
        remaining = conn_ctx->lifetime - (now - conn_ctx->registration_time);
        if (remaining < smallest) {
            smallest = remaining;
            lf = conn_ctx->lifetime;
        }
        has_connection = true;
    }

    if (!has_connection)
        return 0;

    if (client->lifetime_ctx.timeout)
        sol_timeout_del(client->lifetime_ctx.timeout);

    //Set to NULL in case we fail.
    client->lifetime_ctx.timeout = NULL;
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
register_reply(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *server_addr)
{
    struct server_conn_ctx *conn_ctx = data;
    struct sol_str_slice path[2];
    uint8_t code;
    int r;

    SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

    sol_coap_packet_unref(conn_ctx->pending_pkt);
    conn_ctx->pending_pkt = NULL;

    if (!pkt && !server_addr) {
        SOL_WRN("Registration request timeout");
        if (conn_ctx->client->removed)
            return false;
        SOL_INT_CHECK_GOTO(++conn_ctx->addr_list_idx,
            == conn_ctx->server_addr_list.len, err_exit);
        r = register_with_server(conn_ctx->client, conn_ctx, false);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        SOL_WRN("Trying another address");
        return false;
    }

    if (!sol_network_link_addr_to_str(server_addr, &addr))
        SOL_WRN("Could not convert the server address to string");

    sol_coap_header_get_code(pkt, &code);
    SOL_INT_CHECK_GOTO(code, != SOL_COAP_RESPONSE_CODE_CREATED, err_exit);

    r = sol_coap_find_options(pkt, SOL_COAP_OPTION_LOCATION_PATH, path,
        sol_util_array_size(path));
    SOL_INT_CHECK_GOTO(r, != 2, err_exit);

    conn_ctx->location = sol_str_slice_to_str(path[1]);
    SOL_NULL_CHECK_GOTO(conn_ctx->location, err_exit);

    SOL_DBG("Registered with server %.*s at location %s",
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)),
        conn_ctx->location);

    r = reschedule_client_timeout(conn_ctx->client);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    return false;

err_exit:
    server_connection_ctx_remove(&conn_ctx->client->connections, conn_ctx);
    return false;
}

static bool
update_reply(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *server_addr)
{
    uint8_t code;
    struct server_conn_ctx *conn_ctx = data;

    if (!pkt && !server_addr)
        goto err_exit;

    sol_coap_header_get_code(pkt, &code);
    SOL_INT_CHECK_GOTO(code, != SOL_COAP_RESPONSE_CODE_CHANGED, err_exit);
    return false;

err_exit:
    server_connection_ctx_remove(&conn_ctx->client->connections, conn_ctx);
    return false;
}

static int
register_with_server(struct sol_lwm2m_client *client,
    struct server_conn_ctx *conn_ctx, bool is_update)
{
    struct sol_buffer query = SOL_BUFFER_INIT_EMPTY, objs_payload;
    uint8_t format = SOL_COAP_CONTENT_TYPE_APPLICATION_LINK_FORMAT;
    struct sol_blob *binding = NULL;
    struct sol_coap_packet *pkt;
    struct sol_buffer *buf;
    int r;

    r = setup_objects_payload(client, &objs_payload);
    SOL_INT_CHECK(r, < 0, r);

    r = get_binding_and_lifetime(client, conn_ctx->server_id,
        &conn_ctx->lifetime, &binding);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    pkt = sol_coap_packet_new_request(SOL_COAP_METHOD_POST, SOL_COAP_MESSAGE_TYPE_CON);
    r = -ENOMEM;
    SOL_NULL_CHECK_GOTO(pkt, err_exit);

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
    ADD_QUERY("lt", "%" PRId64, conn_ctx->lifetime);
    ADD_QUERY("binding", "%.*s", SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(binding)));
    if (client->sms)
        ADD_QUERY("sms", "%s", client->sms);

    r = sol_coap_packet_get_payload(pkt, &buf, NULL);
    SOL_INT_CHECK_GOTO(r, < 0, err_coap);

    r = sol_buffer_append_bytes(buf, objs_payload.data, objs_payload.used);
    SOL_INT_CHECK_GOTO(r, < 0, err_coap);

    conn_ctx->registration_time = time(NULL);

    SOL_DBG("Connecting with LWM2M server - binding '%.*s' -"
        "lifetime '%" PRId64 "'", SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(binding)),
        conn_ctx->lifetime);
    r = sol_coap_send_packet_with_reply(client->coap_server,
        pkt,
        sol_vector_get_no_check(&conn_ctx->server_addr_list,
        conn_ctx->addr_list_idx),
        is_update ? update_reply : register_reply, conn_ctx);
    sol_buffer_fini(&query);
    sol_buffer_fini(&objs_payload);
    sol_blob_unref(binding);
    return r;

err_coap:
    sol_coap_packet_unref(pkt);
    sol_buffer_fini(&query);
err_exit:
    sol_buffer_fini(&objs_payload);
    if (binding)
        sol_blob_unref(binding);
    return r;
}

static bool
bootstrap_request_reply(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *server_addr)
{
    struct server_conn_ctx *conn_ctx = data;
    uint8_t code;
    int r;

    SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

    sol_coap_packet_unref(conn_ctx->pending_pkt);
    conn_ctx->pending_pkt = NULL;

    if (!pkt && !server_addr) {
        SOL_WRN("Bootstrap request timeout");
        SOL_INT_CHECK_GOTO(++conn_ctx->addr_list_idx,
            == conn_ctx->server_addr_list.len, err_exit);
        r = bootstrap_with_server(conn_ctx->client, conn_ctx);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        SOL_WRN("Trying another address");
        return false;
    }
    if (!sol_network_link_addr_to_str(server_addr, &addr))
        SOL_WRN("Could not convert the server address to string");

    sol_coap_header_get_code(pkt, &code);
    SOL_INT_CHECK_GOTO(code, != SOL_COAP_RESPONSE_CODE_CHANGED, err_exit);

    SOL_DBG("Bootstrap process with server %.*s can start",
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)));
    sol_vector_clear(&conn_ctx->server_addr_list);

    return false;

err_exit:
    SOL_WRN("Bootstrap process with server %.*s failed!",
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)));
    server_connection_ctx_remove(&conn_ctx->client->connections, conn_ctx);
    return false;
}

static int
bootstrap_with_server(struct sol_lwm2m_client *client,
    struct server_conn_ctx *conn_ctx)
{
    struct sol_buffer query = SOL_BUFFER_INIT_EMPTY;
    struct sol_coap_packet *pkt;
    int r;

    pkt = sol_coap_packet_new_request(SOL_COAP_METHOD_POST, SOL_COAP_MESSAGE_TYPE_CON);
    r = -ENOMEM;
    SOL_NULL_CHECK_GOTO(pkt, err_exit);

    r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH, "bs", strlen("bs"));
    SOL_INT_CHECK_GOTO(r, < 0, err_coap);

    conn_ctx->pending_pkt = sol_coap_packet_ref(pkt);

    ADD_QUERY("ep", "%s", client->name);

    SOL_DBG("Sending Bootstrap Request to LWM2M Bootstrap Server");
    r = sol_coap_send_packet_with_reply(client->coap_server,
        pkt,
        sol_vector_get_no_check(&conn_ctx->server_addr_list,
        conn_ctx->addr_list_idx), bootstrap_request_reply, conn_ctx);
    sol_buffer_fini(&query);
    return r;

err_coap:
    sol_coap_packet_unref(pkt);
    sol_buffer_fini(&query);
err_exit:
    return r;
}

static void
hostname_ready(void *data,
    const struct sol_str_slice hostname, const struct sol_vector *addr_list)
{
    struct server_conn_ctx *conn_ctx = data;
    struct sol_network_link_addr *addr, *cpy;
    uint16_t i;
    int r;

    conn_ctx->hostname_handle = NULL;
    SOL_NULL_CHECK_GOTO(addr_list, err_exit);

    SOL_VECTOR_FOREACH_IDX (addr_list, addr, i) {
        cpy = sol_vector_append(&conn_ctx->server_addr_list);
        SOL_NULL_CHECK_GOTO(cpy, err_exit);
        memcpy(cpy, addr, sizeof(struct sol_network_link_addr));
        cpy->port = conn_ctx->port;
    }

    if (conn_ctx->server_id != DEFAULT_SHORT_SERVER_ID) {
        r = register_with_server(conn_ctx->client, conn_ctx, false);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    } else {
        r = bootstrap_with_server(conn_ctx->client, conn_ctx);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    return;

err_exit:
    server_connection_ctx_remove(&conn_ctx->client->connections, conn_ctx);
}

static struct server_conn_ctx *
server_connection_ctx_new(struct sol_lwm2m_client *client,
    const struct sol_str_slice str_addr, int64_t server_id)
{
    struct server_conn_ctx *conn_ctx;
    struct sol_http_url uri;
    int r;

    r = sol_http_split_uri(str_addr, &uri);
    SOL_INT_CHECK(r, < 0, NULL);

    conn_ctx = calloc(1, sizeof(struct server_conn_ctx));
    SOL_NULL_CHECK(conn_ctx, NULL);

    r = sol_ptr_vector_append(&client->connections, conn_ctx);
    SOL_INT_CHECK_GOTO(r, < 0, err_append);
    conn_ctx->client = client;
    conn_ctx->server_id = server_id;
    sol_vector_init(&conn_ctx->server_addr_list,
        sizeof(struct sol_network_link_addr));

    if (!uri.port)
        conn_ctx->port = SOL_LWM2M_DEFAULT_SERVER_PORT;
    else
        conn_ctx->port = uri.port;

    SOL_DBG("Fetching hostname info for:%.*s", SOL_STR_SLICE_PRINT(str_addr));
    conn_ctx->hostname_handle =
        sol_network_get_hostname_address_info(uri.host,
        SOL_NETWORK_FAMILY_UNSPEC, hostname_ready, conn_ctx);
    SOL_NULL_CHECK_GOTO(conn_ctx->hostname_handle, err_exit);

    //For Registration Interface, location will be filled in register_reply()

    return conn_ctx;

err_exit:
    (void)sol_ptr_vector_del_element(&client->connections, conn_ctx);
err_append:
    free(conn_ctx);
    return NULL;
}

static int
spam_update(struct sol_lwm2m_client *client, bool consider_lifetime)
{
    int r;
    uint16_t i;
    struct server_conn_ctx *conn_ctx;

    SOL_PTR_VECTOR_FOREACH_IDX (&client->connections, conn_ctx, i) {
        if (!conn_ctx->location || (consider_lifetime &&
            conn_ctx->lifetime != client->lifetime_ctx.lifetime))
            continue;

        r = register_with_server(client, conn_ctx, true);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = reschedule_client_timeout(client);
    SOL_INT_CHECK_GOTO(r, < 0, exit);
exit:
    return r;
}

static bool
lifetime_client_timeout(void *data)
{
    if (spam_update(data, true) < 0)
        SOL_WRN("Could not spam the update");
    return false;
}

static bool
bootstrap_reply(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr)
{
    struct bootstrap_ctx *ctx = data;
    uint8_t code = 0;

    if (!cliaddr && !req)
        code = SOL_COAP_RESPONSE_CODE_GATEWAY_TIMEOUT;

    if (!code)
        sol_coap_header_get_code(req, &code);
    ((void (*)(void *,
    struct sol_lwm2m_bootstrap_server *,
    struct sol_lwm2m_bootstrap_client_info *, const char *,
    enum sol_coap_response_code))ctx->cb)
        ((void *)ctx->data, ctx->server, ctx->cinfo, ctx->path, code);

    if (code != SOL_COAP_RESPONSE_CODE_GATEWAY_TIMEOUT)
        send_ack_if_needed(server, req, cliaddr);
    free(ctx->path);
    free(ctx);
    return false;
}

static int
send_bootstrap_packet(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    enum bootstrap_type type, void *cb, const void *data,
    enum sol_coap_method method,
    struct sol_lwm2m_resource *resources,
    struct sol_lwm2m_resource **instances, size_t *instances_len,
    uint16_t *instances_ids, size_t len)
{
    int r;
    struct sol_coap_packet *pkt;
    struct bootstrap_ctx *ctx;

    r = setup_coap_packet(method, SOL_COAP_MESSAGE_TYPE_CON,
        NULL, path, NULL, NULL, resources, instances, instances_len,
        instances_ids, len, NULL, &pkt);
    SOL_INT_CHECK(r, < 0, r);

    if (!cb)
        return sol_coap_send_packet(server->coap, pkt, &client->cliaddr);

    ctx = malloc(sizeof(struct bootstrap_ctx));
    SOL_NULL_CHECK_GOTO(ctx, err_exit);

    ctx->path = strdup(path);
    SOL_NULL_CHECK_GOTO(ctx->path, err_exit);
    ctx->type = type;
    ctx->server = server;
    ctx->cinfo = client;
    ctx->data = data;
    ctx->cb = cb;

    return sol_coap_send_packet_with_reply(server->coap, pkt, &client->cliaddr,
        bootstrap_reply, ctx);

err_exit:
    free(ctx);
    sol_coap_packet_unref(pkt);
    return -ENOMEM;
}

SOL_API int
sol_lwm2m_bootstrap_server_write_object(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    struct sol_lwm2m_resource **instances, size_t *instances_len,
    uint16_t *instances_ids, size_t len,
    void (*cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    return send_bootstrap_packet(server, client, path,
        BOOTSTRAP_WRITE, cb, data, SOL_COAP_METHOD_PUT, NULL, instances,
        instances_len, instances_ids, len);
}

SOL_API int
sol_lwm2m_bootstrap_server_write(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    void (*cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    return send_bootstrap_packet(server, client, path,
        BOOTSTRAP_WRITE, cb, data, SOL_COAP_METHOD_PUT, resources, NULL, NULL, NULL, len);
}

SOL_API int
sol_lwm2m_bootstrap_server_delete_object_instance(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    void (*cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    return send_bootstrap_packet(server, client, path,
        BOOTSTRAP_DELETE, cb, data, SOL_COAP_METHOD_DELETE, NULL, NULL, NULL, NULL, 0);
}

SOL_API int
sol_lwm2m_bootstrap_server_send_finish(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client)
{
    struct sol_coap_packet *pkt;
    int r;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(client, -EINVAL);

    pkt = sol_coap_packet_new_request(SOL_COAP_METHOD_POST, SOL_COAP_MESSAGE_TYPE_CON);
    r = -ENOMEM;
    SOL_NULL_CHECK_GOTO(pkt, err_exit);

    r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH, "bs", strlen("bs"));
    SOL_INT_CHECK_GOTO(r, < 0, err_coap);

    SOL_DBG("Sending Bootstrap Finish to LWM2M Client %s", client->name);
    r = sol_coap_send_packet(server->coap, pkt, &client->cliaddr);

    r = sol_ptr_vector_remove(&server->clients, client);
    if (r < 0)
        SOL_WRN("Could not remove the client %s from the clients list",
            client->name);
    bootstrap_client_info_del(client);

    return r;

err_coap:
    sol_coap_packet_unref(pkt);
err_exit:
    return r;
}

static int
bootstrap_finish(void *data, struct sol_coap_server *coap,
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_lwm2m_client *client = data;
    struct sol_coap_packet *response;
    int r;

    SOL_DBG("Bootstrap Finish received");

    response = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response, -ENOMEM);

    r = sol_coap_header_set_code(response, SOL_COAP_RESPONSE_CODE_CHANGED);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    //The '/bs' endpoint can be removed from the Client now
    r = sol_coap_server_unregister_resource(coap, resource);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    client->is_bootstrapping = false;

    r = sol_coap_send_packet(coap, response, cliaddr);
    dispatch_bootstrap_event_to_client(client, SOL_LWM2M_BOOTSTRAP_EVENT_FINISHED);
    return r;

err_exit:
    sol_coap_header_set_code(response, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);
    (void)sol_coap_send_packet(coap, response, cliaddr);
    dispatch_bootstrap_event_to_client(client, SOL_LWM2M_BOOTSTRAP_EVENT_ERROR);
    return r;
}

static const struct sol_coap_resource bootstrap_finish_interface = {
    SOL_SET_API_VERSION(.api_version = SOL_COAP_RESOURCE_API_VERSION, )
    .post = bootstrap_finish,
    .flags = SOL_COAP_FLAGS_NONE,
    .path = {
        SOL_STR_SLICE_LITERAL("bs"),
        SOL_STR_SLICE_EMPTY
    }
};

static bool
client_bootstrap(void *data)
{
    struct sol_lwm2m_client *client = data;
    struct server_conn_ctx *conn_ctx;

    client->bootstrap_ctx.timeout = NULL;

    //Try client-initiated bootstrap:
    conn_ctx = server_connection_ctx_new(client,
        sol_str_slice_from_blob(client->bootstrap_ctx.server_uri),
        DEFAULT_SHORT_SERVER_ID);

    if (!conn_ctx) {
        SOL_WRN("Could not perform Client-initiated Bootstrap with server %.*s",
            SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(client->bootstrap_ctx.server_uri)));

        if (sol_coap_server_set_unknown_resource_handler(client->coap_server,
            NULL, client) < 0)
            SOL_WRN("Could not unregister Bootstrap Unknown resource for client.");
        if (sol_coap_server_unregister_resource(client->coap_server,
            &bootstrap_finish_interface) < 0)
            SOL_WRN("Could not unregister Bootstrap Finish resource for client.");
    }

    sol_blob_unref(client->bootstrap_ctx.server_uri);
    client->bootstrap_ctx.server_uri = NULL;

    return false;
}

SOL_API int
sol_lwm2m_client_start(struct sol_lwm2m_client *client)
{
    uint16_t i, j, k;
    uint16_t bootstrap_account_idx = 0;
    struct obj_ctx *ctx;
    bool has_server = false;
    struct obj_instance *instance;
    struct server_conn_ctx *conn_ctx;
    struct resource_ctx *res_ctx;
    struct sol_lwm2m_resource res[3];
    int r;

    SOL_NULL_CHECK(client, -EINVAL);

    ctx = find_object_ctx_by_id(client, SECURITY_SERVER_OBJECT_ID);
    if (!ctx) {
        SOL_WRN("LWM2M Security object not provided!");
        return -ENOENT;
    }

    if (!ctx->instances.len) {
        SOL_WRN("There are no Security Server instances");
        return -ENOENT;
    }

    //Try to register with all available [non-bootstrap] servers
    SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, i) {
        r = read_resources(client, ctx, instance, res, 1,
            SECURITY_SERVER_IS_BOOTSTRAP);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        //Is it a bootstap?
        if (!res[0].data[0].b) {
            sol_lwm2m_resource_clear(&res[0]);
            r = read_resources(client, ctx, instance, res, 2,
                SECURITY_SERVER_URI, SECURITY_SERVER_ID);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);

            conn_ctx = server_connection_ctx_new(client, sol_str_slice_from_blob(res[0].data[0].blob),
                res[1].data[0].integer);
            r = -ENOMEM;
            SOL_NULL_CHECK_GOTO(conn_ctx, err_clear_2);
            has_server = true;

            clear_resource_array(res, 2);
        } else {
            sol_lwm2m_resource_clear(&res[0]);
            bootstrap_account_idx = i;
        }
    }

    //If all attempts to register failed, try to bootstrap
    if (!has_server) {
        SOL_DBG("The client did not specify a LWM2M server to connect."
            " Trying to bootstrap now.");

        client->is_bootstrapping = true;

        r = read_resources(client, ctx,
            sol_vector_get_no_check(&ctx->instances, bootstrap_account_idx), res, 3,
            SECURITY_SERVER_URI, SECURITY_SERVER_CLIENT_HOLD_OFF_TIME,
            SECURITY_SERVER_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT);

        //Create '/bs' CoAP resource to receive Bootstrap Finish notification
        r = sol_coap_server_register_resource(client->coap_server,
            &bootstrap_finish_interface, client);
        SOL_INT_CHECK_GOTO(r, < 0, err_clear_3);

        //Create unknown CoAP resource to handle Bootstrap Write and Bootstrap Delete
        r = sol_coap_server_set_unknown_resource_handler(client->coap_server,
            &handle_unknown_bootstrap_resource, client);
        SOL_INT_CHECK_GOTO(r, < 0, err_unregister_bs);

        SOL_DBG("Expecting server-initiated Bootstrap for"
            " %" PRId64 " seconds", res[1].data[0].integer);

        //Expect server-initiated bootstrap with sol_timeout before client-initiated bootstrap
        client->bootstrap_ctx.server_uri = sol_blob_ref(res[0].data[0].blob);
        SOL_NULL_CHECK_GOTO(client->bootstrap_ctx.server_uri, err_unregister_unknown);

        client->bootstrap_ctx.timeout = sol_timeout_add(res[1].data[0].integer * ONE_SECOND,
            client_bootstrap, client);
        SOL_NULL_CHECK_GOTO(client->bootstrap_ctx.timeout, err_unregister_unknown);

        clear_resource_array(res, sol_util_array_size(res));

        return 0;
    }

    SOL_VECTOR_FOREACH_IDX (&client->objects, ctx, i) {
        r = sol_coap_server_register_resource(client->coap_server,
            ctx->obj_res, client);
        SOL_INT_CHECK(r, < 0, r);

        SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, j) {
            r = sol_coap_server_register_resource(client->coap_server,
                instance->instance_res, client);
            SOL_INT_CHECK(r, < 0, r);

            SOL_VECTOR_FOREACH_IDX (&instance->resources_ctx, res_ctx, k) {
                r = sol_coap_server_register_resource(client->coap_server,
                    res_ctx->res, client);
                SOL_INT_CHECK(r, < 0, r);
            }
        }
    }

    client->running = true;

    return 0;

err_unregister_unknown:
    if (sol_coap_server_set_unknown_resource_handler(client->coap_server, NULL, client) < 0)
        SOL_WRN("Could not unregister Bootstrap Unknown resource for client.");
err_unregister_bs:
    if (sol_coap_server_unregister_resource(client->coap_server, &bootstrap_finish_interface) < 0)
        SOL_WRN("Could not unregister Bootstrap Finish resource for client.");
err_clear_3:
    sol_lwm2m_resource_clear(&res[2]);
err_clear_2:
    sol_lwm2m_resource_clear(&res[1]);
    sol_lwm2m_resource_clear(&res[0]);
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
            conn_ctx->pending_pkt,
            sol_vector_get_no_check(&conn_ctx->server_addr_list,
            conn_ctx->addr_list_idx));
        sol_coap_packet_unref(conn_ctx->pending_pkt);
        conn_ctx->pending_pkt = NULL;
        return r;
    }

    pkt = sol_coap_packet_new_request(SOL_COAP_METHOD_DELETE,
        SOL_COAP_MESSAGE_TYPE_NON_CON);
    SOL_NULL_CHECK(pkt, -ENOMEM);

    r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH, "rd", strlen("rd"));
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH,
        conn_ctx->location, strlen(conn_ctx->location));
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    return sol_coap_send_packet(client->coap_server, pkt,
        sol_vector_get_no_check(&conn_ctx->server_addr_list,
        conn_ctx->addr_list_idx));

err_exit:
    sol_coap_packet_unref(pkt);
    return r;
}

SOL_API int
sol_lwm2m_client_stop(struct sol_lwm2m_client *client)
{
    struct server_conn_ctx *conn_ctx;
    struct obj_ctx *ctx;
    struct obj_instance *instance;
    struct resource_ctx *res_ctx;
    uint16_t i, j, k;
    int r;

    SOL_NULL_CHECK(client, -EINVAL);

    SOL_PTR_VECTOR_FOREACH_IDX (&client->connections, conn_ctx, i) {
        //Send unregister only to non-bootstrap servers
        if (conn_ctx->registration_time) {
            r = send_client_delete_request(client, conn_ctx);
            SOL_INT_CHECK(r, < 0, r);
        }

        if (conn_ctx->pending_pkt) {
            r = sol_coap_cancel_send_packet(client->coap_server,
                conn_ctx->pending_pkt,
                sol_vector_get_no_check(&conn_ctx->server_addr_list,
                conn_ctx->addr_list_idx));
            sol_coap_packet_unref(conn_ctx->pending_pkt);
            conn_ctx->pending_pkt = NULL;
            SOL_INT_CHECK(r, < 0, r);
        }
    }

    if (client->running) {
        SOL_VECTOR_FOREACH_IDX (&client->objects, ctx, i) {
            r = sol_coap_server_unregister_resource(client->coap_server,
                ctx->obj_res);
            SOL_INT_CHECK(r, < 0, r);

            SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, j) {
                r = sol_coap_server_unregister_resource(client->coap_server,
                    instance->instance_res);
                SOL_INT_CHECK(r, < 0, r);

                SOL_VECTOR_FOREACH_IDX (&instance->resources_ctx, res_ctx, k) {
                    r = sol_coap_server_unregister_resource(client->coap_server,
                        res_ctx->res);
                    SOL_INT_CHECK(r, < 0, r);
                }
            }
        }

        client->running = false;
    }

    server_connection_ctx_list_clear(&client->connections);

    return 0;
}

SOL_API int
sol_lwm2m_client_send_update(struct sol_lwm2m_client *client)
{
    SOL_NULL_CHECK(client, -EINVAL);

    return spam_update(client, false);
}

static struct resource_ctx *
find_resource_ctx_by_id(struct obj_instance *instance, uint16_t id)
{
    uint16_t i;
    struct resource_ctx *res_ctx;

    SOL_VECTOR_FOREACH_IDX (&instance->resources_ctx, res_ctx, i) {
        if (res_ctx->id == id)
            return res_ctx;
    }

    return NULL;
}

static bool
notification_already_sent(struct sol_ptr_vector *vector, const void *ptr)
{
    uint16_t i;
    const void *v;

    SOL_PTR_VECTOR_FOREACH_IDX (vector, v, i) {
        if (v == ptr)
            return true;
    }

    return false;
}

SOL_API int
sol_lwm2m_client_notify(struct sol_lwm2m_client *client, const char **paths)
{
    size_t i;
    struct sol_ptr_vector already_sent = SOL_PTR_VECTOR_INIT;
    int r;

    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(paths, -EINVAL);

    for (i = 0; paths[i]; i++) {
        bool r_bool;
        uint16_t j, k;
        struct obj_ctx *obj_ctx;
        struct obj_instance *obj_instance;
        struct resource_ctx *res_ctx;
        struct sol_vector tokens;
        uint16_t path[3];
        struct sol_str_slice *token;

        tokens = sol_str_slice_split(sol_str_slice_from_str(paths[i]), "/", 0);

        if (tokens.len != 4) {
            sol_vector_clear(&tokens);
            SOL_WRN("The path must contain an object, instance id and resource id");
            goto err_exit_einval;
        }

        k = 0;
        SOL_VECTOR_FOREACH_IDX (&tokens, token, j) {
            char *end;
            if (j == 0)
                continue;
            path[k++] = sol_util_strtoul_n(token->data, &end, token->len, 10);
            if (end == token->data || end != token->data + token->len ||
                errno != 0) {
                r = -errno;
                SOL_WRN("Could not convert %.*s to integer",
                    SOL_STR_SLICE_PRINT(*token));
                sol_vector_clear(&tokens);
                goto err_exit;
            }
        }

        sol_vector_clear(&tokens);

        obj_ctx = find_object_ctx_by_id(client, path[0]);
        SOL_NULL_CHECK_GOTO(obj_ctx, err_exit_einval);
        obj_instance = find_object_instance_by_instance_id(obj_ctx, path[1]);
        SOL_NULL_CHECK_GOTO(obj_instance, err_exit_einval);
        res_ctx = find_resource_ctx_by_id(obj_instance, path[2]);
        SOL_NULL_CHECK_GOTO(res_ctx, err_exit_einval);

        if (!notification_already_sent(&already_sent, obj_ctx)) {
            r_bool = send_notification_pkt(client, obj_ctx, NULL, -1, obj_ctx->obj_res);
            SOL_EXP_CHECK_GOTO(!r_bool, err_exit_einval);

            r = sol_ptr_vector_append(&already_sent, obj_ctx);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }

        if (!notification_already_sent(&already_sent, obj_instance)) {
            r_bool = send_notification_pkt(client, obj_ctx, obj_instance, -1,
                obj_instance->instance_res);
            SOL_EXP_CHECK_GOTO(!r_bool, err_exit_einval);

            r = sol_ptr_vector_append(&already_sent, obj_instance);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }

        r_bool = send_notification_pkt(client, obj_ctx, obj_instance, path[2],
            res_ctx->res);
        SOL_EXP_CHECK_GOTO(!r_bool, err_exit_einval);
    }

    sol_ptr_vector_clear(&already_sent);
    return 0;

err_exit_einval:
    r = -EINVAL;
err_exit:
    sol_ptr_vector_clear(&already_sent);
    return r;
}
