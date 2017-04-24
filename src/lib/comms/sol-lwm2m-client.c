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

#define SOL_LOG_DOMAIN &_lwm2m_client_domain

#include "sol-log-internal.h"
#include "sol-util-internal.h"
#include "sol-list.h"
#include "sol-socket.h"
#include "sol-lwm2m.h"
#include "sol-lwm2m-common.h"
#include "sol-lwm2m-client.h"
#include "sol-lwm2m-security.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-monitors.h"
#include "sol-random.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util.h"
#include "sol-http.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_lwm2m_client_domain, "lwm2m-client");

#define ADD_QUERY(_key, _format, _value) \
    do { \
        query.used = 0; \
        r = sol_buffer_append_printf(&query, "%s=" _format "", _key, _value); \
        SOL_INT_CHECK_GOTO(r, < 0, err_coap); \
        r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_QUERY, \
            query.data, query.used); \
        SOL_INT_CHECK_GOTO(r, < 0, err_coap); \
    } while (0);

#define SOL_COAP_SERVER_UNREGISTER_RESOURCE_ALL(__resource) \
    do { \
        sol_coap_server_unregister_resource(client->coap_server, \
            __resource); \
 \
        if (client->security) { \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY)) \
                sol_coap_server_unregister_resource(client->dtls_server_psk, \
                    __resource); \
 \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY)) \
                sol_coap_server_unregister_resource(client->dtls_server_rpk, \
                    __resource); \
        } \
    } while (0);

#define SOL_COAP_SERVER_UNREGISTER_RESOURCE_ALL_INT(__resource) \
    do { \
        r = sol_coap_server_unregister_resource(client->coap_server, \
            __resource); \
        SOL_INT_CHECK(r, < 0, r); \
 \
        if (client->security) { \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY)) { \
                r = sol_coap_server_unregister_resource(client->dtls_server_psk, \
                    __resource); \
                SOL_INT_CHECK(r, < 0, r); \
            } \
 \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY)) { \
                r = sol_coap_server_unregister_resource(client->dtls_server_rpk, \
                    __resource); \
                SOL_INT_CHECK(r, < 0, r); \
            } \
        } \
    } while (0);

#define SOL_COAP_SERVER_REGISTER_RESOURCE_ALL_GOTO(__resource, \
        __err_label_1, __err_label_2, __err_label_3) \
    do { \
        r = sol_coap_server_register_resource(client->coap_server, \
            __resource, client); \
        SOL_INT_CHECK_GOTO(r, < 0, __err_label_1); \
 \
        if (client->security) { \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY)) { \
                r = sol_coap_server_register_resource(client->dtls_server_psk, \
                    __resource, client); \
                SOL_INT_CHECK_GOTO(r, < 0, __err_label_2); \
            } \
 \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY)) { \
                r = sol_coap_server_register_resource(client->dtls_server_rpk, \
                    __resource, client); \
                SOL_INT_CHECK_GOTO(r, < 0, __err_label_3); \
            } \
        } \
    } while (0);

#define SOL_COAP_SERVER_REGISTER_RESOURCE_ALL_INT(__resource) \
    do { \
        r = sol_coap_server_register_resource(client->coap_server, \
            __resource, client); \
        SOL_INT_CHECK(r, < 0, r); \
 \
        if (client->security) { \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY)) { \
                r = sol_coap_server_register_resource(client->dtls_server_psk, \
                    __resource, client); \
                SOL_INT_CHECK(r, < 0, r); \
            } \
 \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY)) { \
                r = sol_coap_server_register_resource(client->dtls_server_rpk, \
                    __resource, client); \
                SOL_INT_CHECK(r, < 0, r); \
            } \
        } \
    } while (0);

#define SOL_COAP_NOTIFY_BY_CALLBACK_ALL_INT(__resource) \
    do { \
        r = sol_coap_notify_by_callback(client->coap_server, __resource, \
            notification_cb, &ctx); \
        SOL_INT_CHECK(r, < 0, false); \
 \
        if (client->security) { \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY)) { \
                r = sol_coap_notify_by_callback(client->dtls_server_psk, __resource, \
                    notification_cb, &ctx); \
                SOL_INT_CHECK(r, < 0, false); \
            } \
 \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY)) { \
                r = sol_coap_notify_by_callback(client->dtls_server_rpk, __resource, \
                    notification_cb, &ctx); \
                SOL_INT_CHECK(r, < 0, false); \
            } \
        } \
    } while (0);

#define SOL_COAP_NOTIFY_BY_CALLBACK_ALL_GOTO(__resource) \
    do { \
        r = sol_coap_notify_by_callback(client->coap_server, __resource, \
            notification_cb, &ctx); \
        SOL_INT_CHECK_GOTO(r, < 0, err_exit); \
 \
        if (client->security) { \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY)) { \
                r = sol_coap_notify_by_callback(client->dtls_server_psk, __resource, \
                    notification_cb, &ctx); \
                SOL_INT_CHECK_GOTO(r, < 0, err_exit); \
            } \
 \
            if (sol_lwm2m_security_supports_security_mode(client->security, \
                SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY)) { \
                r = sol_coap_notify_by_callback(client->dtls_server_rpk, __resource, \
                    notification_cb, &ctx); \
                SOL_INT_CHECK_GOTO(r, < 0, err_exit); \
            } \
        } \
    } while (0);

struct notification_ctx {
    struct sol_lwm2m_client *client;
    struct obj_ctx *obj_ctx;
    struct obj_instance *obj_instance;
    int32_t resource_id;
};

struct resource_ctx {
    char *str_id;
    struct sol_coap_resource *res;
    uint16_t id;
};

static bool lifetime_client_timeout(void *data);
static int register_with_server(struct sol_lwm2m_client *client,
    struct server_conn_ctx *conn_ctx, bool is_update);
static int bootstrap_with_server(struct sol_lwm2m_client *client,
    struct server_conn_ctx *conn_ctx);
static int handle_resource(void *data, struct sol_coap_server *server,
    const struct sol_coap_resource *resource, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr);
static int setup_access_control_object_instance_for_instance(
    struct sol_lwm2m_client *client, uint16_t object_id,
    uint16_t instance_id, int64_t server_id,
    struct sol_lwm2m_resource *acl_res, bool register_with_coap);
static int setup_access_control_object_instances(
    struct sol_lwm2m_client *client);

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
            SOL_COAP_SERVER_UNREGISTER_RESOURCE_ALL(res_ctx->res);
        }
        free(res_ctx->res);
        free(res_ctx->str_id);
    }

    if (!client->removed && obj_instance->instance_res) {
        SOL_COAP_SERVER_UNREGISTER_RESOURCE_ALL(obj_instance->instance_res);
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
            SOL_COAP_SERVER_REGISTER_RESOURCE_ALL_GOTO(res_ctx->res,
                err_exit, err_exit, err_exit);
        }
    }

    return 0;

err_exit:
    SOL_VECTOR_FOREACH_IDX (&instance->resources_ctx, res_ctx, i) {
        if (res_ctx->res) {
            SOL_COAP_SERVER_UNREGISTER_RESOURCE_ALL(res_ctx->res);

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
        SOL_COAP_SERVER_REGISTER_RESOURCE_ALL_GOTO(obj_instance->instance_res,
            err_register, err_resources, err_resources);
    }

    r = setup_resources_ctx(client, obj_ctx, obj_instance, register_with_coap);
    SOL_INT_CHECK_GOTO(r, < 0, err_resources);

    return 0;

err_resources:
    SOL_COAP_SERVER_UNREGISTER_RESOURCE_ALL(obj_instance->instance_res);
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

/* Returns 1 if authorized; 0 if unauthorized and < 0 if error */
static int
check_authorization(struct sol_lwm2m_client *client,
    int64_t server_id, uint16_t obj_id,
    int32_t instance_id, int64_t rights_needed)
{
    struct obj_ctx *obj_ctx;
    struct obj_instance *obj_instance;
    struct sol_lwm2m_resource res[2] = { };
    int r = 0;
    int64_t default_acl = SOL_LWM2M_ACL_NONE;
    uint16_t i, j;

    //If only one server or Bootstrap Server ID, then full access rights
    if (sol_ptr_vector_get_len(&client->connections) == 1 || server_id == UINT16_MAX) {
        SOL_DBG("Full access rights granted. This is either a Bootstrap Server"
            " or single-server scenario");
        return 1;
    }

    obj_ctx = find_object_ctx_by_id(client, ACCESS_CONTROL_OBJECT_ID);
    //If the target Object is an Access Control Object itself,
    // the server is authorized iff it is the owner of the object instance.
    if (obj_id == ACCESS_CONTROL_OBJECT_ID) {
        SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, obj_instance, i) {
            if (obj_instance->id == instance_id) {
                r = read_resources(client, obj_ctx, obj_instance, res, 1,
                    ACCESS_CONTROL_OBJECT_OWNER_RES_ID);
                if (r < 0) {
                    SOL_WRN("Could not read Access Control"
                        " Object's [Owner ID] resource\n");
                    goto exit_clear_1;
                }

                if (res[0].data[0].content.integer == server_id)
                    r = 1;
                else
                    r = 0;
                goto exit_clear_1;
            }
        }

        r = -ENOENT;
        goto exit_clear_1;
    }

    SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, obj_instance, i) {
        r = read_resources(client, obj_ctx, obj_instance, res, 2,
            ACCESS_CONTROL_OBJECT_OBJECT_RES_ID,
            ACCESS_CONTROL_OBJECT_INSTANCE_RES_ID);
        if (r < 0) {
            SOL_WRN("Could not read Access Control Object's"
                " [Object ID] and [Instance ID] resources\n");
            goto exit_clear_2;
        }

        //Retrieve the associated Access Control Object Instance, by matching Object ID
        // and Instance ID, or if instance_id == -1 and 'R'ead is needed, this
        // is an Observe Request on Object level and any Instance with this access right
        // is enough to reply with the Observe flag on CoAP
        if ((res[0].data[0].content.integer == obj_id &&
            res[1].data[0].content.integer == instance_id) ||
            (res[0].data[0].content.integer == obj_id &&
            instance_id == -1 && (rights_needed & SOL_LWM2M_ACL_READ))) {
            clear_resource_array(res, sol_util_array_size(res));

            r = read_resources(client, obj_ctx, obj_instance, res, 2,
                ACCESS_CONTROL_OBJECT_ACL_RES_ID,
                ACCESS_CONTROL_OBJECT_OWNER_RES_ID);
            if (r < 0) {
                SOL_WRN("Could not read Access Control"
                    " Object's [ACL] and [Owner ID] resources\n");
                goto exit_clear_2;
            }

            //Retrive this server's ACL Resource Instance
            for (j = 0; j < res[0].data_len; j++) {
                if (res[0].data[j].id == server_id) {
                    if (res[0].data[j].content.integer & rights_needed) {
                        r = 1;
                        goto exit_clear_2;
                    } else {
                        r = 0;
                        goto exit_clear_2;
                    }
                }

                //Keep the default ACL Resource Instance, if any, to save another loop later
                if (res[0].data[j].id == DEFAULT_SHORT_SERVER_ID)
                    default_acl = res[0].data[j].content.integer;
            }

            //If no ACL for this server, check if it is the owner of the object.
            // If owner and no specific ACL Resource Instance, then full access rights.
            if (res[1].data[0].content.integer == server_id) {
                r = 1;
                goto exit_clear_2;
            }

            //If no ACL and not owner, check if the default ACL Resource Instance applies
            if (default_acl & rights_needed) {
                r = 1;
                goto exit_clear_2;
            }

            //If not Observe operation on Object level, do not check next instance;
            // only break and return
            if (!(instance_id == -1 && (rights_needed & SOL_LWM2M_ACL_READ)))
                goto exit_clear_2;
        }

        clear_resource_array(res, sol_util_array_size(res));
    }

    /*
     * The server is trying to observe all instances of an object and no ACLs
     * were found, REJECT HIM!
     */
    if ((instance_id == -1 && (rights_needed & SOL_LWM2M_ACL_READ)))
        return 0;

    return -ENOENT;

exit_clear_2:
    sol_lwm2m_resource_clear(&res[1]);
exit_clear_1:
    sol_lwm2m_resource_clear(&res[0]);

    return r;
}

static uint8_t
handle_delete(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance,
    int64_t server_id)
{
    int r, ret = SOL_COAP_RESPONSE_CODE_NOT_ALLOWED;
    uint16_t i, j;
    struct obj_ctx *acc_obj_ctx;
    struct obj_instance *acc_obj_instance;

    //Specific Instance?
    if (obj_ctx && obj_instance) {
        if (client->supports_access_control) {
            r = check_authorization(client, server_id, obj_ctx->obj->id,
                obj_instance->id, SOL_LWM2M_ACL_DELETE);

            if (r > 0) {
                SOL_DBG("Server ID %" PRId64 " authorized for D on"
                    " Object Instance /%" PRIu16 "/%" PRIu16,
                    server_id, obj_ctx->obj->id, obj_instance->id);
            } else if (r == 0) {
                SOL_WRN("Server ID %" PRId64 " is not authorized for D on"
                    " Object Instance /%" PRIu16 "/%" PRIu16,
                    server_id, obj_ctx->obj->id, obj_instance->id);
                return SOL_COAP_RESPONSE_CODE_UNAUTHORIZED;
            } else {
                SOL_WRN("Error checking for authorization. Server ID:"
                    " %" PRId64 "; Object Instance: /%" PRIu16 "/%" PRIu16 "; Reason: %d",
                    server_id, obj_ctx->obj->id, obj_instance->id, r);
                return SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
            }
        }

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

            SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, obj_instance, j) {
                r = obj_ctx->obj->del((void *)obj_instance->data,
                    (void *)client->user_data, client, obj_instance->id);

                if (r < 0) {
                    SOL_WRN("Could not properly delete object id %"
                        PRIu16 " instance id: %" PRIu16 " reason:%d.\n"
                        " Still deleting from the instances list.",
                        obj_ctx->obj->id, obj_instance->id, r);
                    obj_instance_clear(client, obj_ctx, obj_instance);
                } else {
                    SOL_DBG("Deleted object id %"
                        PRIu16 " instance id: %" PRIu16,
                        obj_ctx->obj->id, obj_instance->id);
                    obj_instance_clear(client, obj_ctx, obj_instance);
                    ret = SOL_COAP_RESPONSE_CODE_DELETED;
                }
            }

            sol_vector_clear(&obj_ctx->instances);
        }

        if (client->supports_access_control) {
            r = setup_access_control_object_instances(client);
            SOL_INT_CHECK_GOTO(r, < 0, err_access_control);
        }
    }

    return ret;

err_access_control:
    if (client->supports_access_control) {
        acc_obj_ctx = find_object_ctx_by_id(client, ACCESS_CONTROL_OBJECT_ID);
        SOL_VECTOR_FOREACH_IDX (&acc_obj_ctx->instances, acc_obj_instance, i) {
            obj_instance_clear(client, acc_obj_ctx, acc_obj_instance);
        }
        sol_vector_clear(&acc_obj_ctx->instances);
    }

    return SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
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
    uint16_t resource, struct sol_lwm2m_payload payload,
    int64_t server_id)
{
    int r;

    if (!obj_instance) {
        SOL_WRN("Object instance was not provided to execute the path"
            "/%" PRIu16 "/?/%" PRIu16 "", obj_ctx->obj->id, resource);
        return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
    }

    if (client->supports_access_control) {
        r = check_authorization(client, server_id, obj_ctx->obj->id,
            obj_instance->id, SOL_LWM2M_ACL_EXECUTE);

        if (r > 0) {
            SOL_DBG("Server ID %" PRId64 " authorized for E on"
                " Object Instance /%" PRIu16 "/%" PRIu16,
                server_id, obj_ctx->obj->id, obj_instance->id);
        } else if (r == 0) {
            SOL_WRN("Server ID %" PRId64 " is not authorized for E on"
                " Object Instance /%" PRIu16 "/%" PRIu16,
                server_id, obj_ctx->obj->id, obj_instance->id);
            return SOL_COAP_RESPONSE_CODE_UNAUTHORIZED;
        } else {
            SOL_WRN("Error checking for authorization. Server ID:"
                " %" PRId64 "; Object Instance: /%" PRIu16 "/%" PRIu16 "; Reason: %d",
                server_id, obj_ctx->obj->id, obj_instance->id, r);
            return SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
        }
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
    int32_t resource, struct sol_lwm2m_payload payload,
    int64_t server_id)
{
    int r;

    if (!obj_instance) {
        SOL_WRN("Object instance was not provided."
            " Can not complete the write operation");
        return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
    }

    if (client->supports_access_control) {
        r = check_authorization(client, server_id, obj_ctx->obj->id,
            obj_instance->id, SOL_LWM2M_ACL_WRITE);

        if (r > 0) {
            SOL_DBG("Server ID %" PRId64 " authorized for W on"
                " Object Instance /%" PRIu16 "/%" PRIu16,
                server_id, obj_ctx->obj->id, obj_instance->id);
        } else if (r == 0) {
            SOL_WRN("Server ID %" PRId64 " is not authorized for W on"
                " Object Instance /%" PRIu16 "/%" PRIu16,
                server_id, obj_ctx->obj->id, obj_instance->id);
            return SOL_COAP_RESPONSE_CODE_UNAUTHORIZED;
        } else {
            SOL_WRN("Error checking for authorization. Server ID:"
                " %" PRId64 "; Object Instance: /%" PRIu16 "/%" PRIu16 "; Reason: %d",
                server_id, obj_ctx->obj->id, obj_instance->id, r);
            return SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
        }
    }

    //If write_resource is not NULL then write_tlv is guaranteed to be valid as well.
    if (!obj_ctx->obj->write_resource) {
        SOL_WRN("Object %" PRIu16 " does not support the write method",
            obj_ctx->obj->id);
        return SOL_COAP_RESPONSE_CODE_NOT_ALLOWED;
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
        r = sol_lwm2m_resource_init(&res, resource, 1, SOL_LWM2M_RESOURCE_TYPE_SINGLE,
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
    struct obj_ctx *obj_ctx, int32_t instance_id, struct sol_lwm2m_payload payload,
    uint64_t owner_server_id, bool register_with_coap)
{
    int r;
    struct obj_instance *obj_instance;

    if (client->supports_access_control) {
        r = check_authorization(client, owner_server_id, obj_ctx->obj->id,
            UINT16_MAX, SOL_LWM2M_ACL_CREATE);

        if (r > 0) {
            SOL_DBG("Server ID %" PRId64 " authorized for C on"
                " Object /%" PRIu16,
                owner_server_id, obj_ctx->obj->id);
        } else if (r == 0) {
            SOL_WRN("Server ID %" PRId64 " is not authorized for C on"
                " Object /%" PRIu16,
                owner_server_id, obj_ctx->obj->id);
            return SOL_COAP_RESPONSE_CODE_UNAUTHORIZED;
        } else {
            SOL_WRN("Error checking for authorization. Server ID:"
                " %" PRId64 "; Object : /%" PRIu16 "; Reason: %d",
                owner_server_id, obj_ctx->obj->id, r);
            return SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
        }
    }

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

    r = setup_instance_resource(client, obj_ctx, obj_instance, register_with_coap);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    if (client->supports_access_control &&
        obj_ctx->obj->id != ACCESS_CONTROL_OBJECT_ID) {
        r = setup_access_control_object_instance_for_instance(client,
            obj_ctx->obj->id, obj_instance->id, owner_server_id, NULL, register_with_coap);

        if (r == 0) {
            SOL_DBG("Access Control Object Instance and Security Object Instance does"
                " not need an Access Control Object Instance nor ACLs");
        } else if (r != SOL_COAP_RESPONSE_CODE_CHANGED && r != SOL_COAP_RESPONSE_CODE_CREATED) {
            SOL_WRN("Failed to create Access Control Object Instance for Object /%"
                PRIu16 "/%" PRIu16, obj_ctx->obj->id, obj_instance->id);
            goto err_exit;
        }
    }

    return SOL_COAP_RESPONSE_CODE_CREATED;

err_exit:
    obj_instance_clear(client, obj_ctx, obj_instance);
    sol_vector_del_element(&obj_ctx->instances, obj_instance);
    return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
}

static uint8_t
handle_write(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance,
    uint16_t *path, uint16_t path_size,
    struct sol_lwm2m_payload payload, int64_t server_id)
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
                        instance_payload, UINT16_MAX);
                } else {
                    r = handle_create(client, obj_ctx, instance_tlvs->id,
                        instance_payload, UINT16_MAX, false);
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
                r = write_instance_tlv_or_resource(client, obj_ctx,
                    obj_instance, -1, payload, UINT16_MAX);
            } else {
                r = handle_create(client, obj_ctx, path[1], payload, UINT16_MAX, false);
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
        r = write_instance_tlv_or_resource(client, obj_ctx, obj_instance,
            path[2], payload, client->is_bootstrapping ? UINT16_MAX : server_id);

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
            sol_vector_del_element(resources, res);
            continue;
        }
        if (r == -EINVAL) {
            sol_vector_del_element(resources, res);
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
    sol_vector_del_element(resources, res);
    return r;
}

static uint8_t
handle_read(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *obj_instance,
    int32_t resource_id, struct sol_coap_packet *resp, int64_t server_id)
{
    struct sol_vector resources = SOL_VECTOR_INIT(struct sol_lwm2m_resource);
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    struct sol_lwm2m_resource *res;
    uint16_t format = SOL_LWM2M_CONTENT_TYPE_TLV;
    uint16_t i;
    int r;

    if ((obj_ctx->obj->id == SECURITY_OBJECT_ID) &&
        (server_id != UINT16_MAX)) {
        SOL_WRN("Only the Bootstrap Server is allowed to access the Security Object."
            " Server ID %" PRId64 " trying to access it", server_id);
        return SOL_COAP_RESPONSE_CODE_UNAUTHORIZED;
    }

    if (client->supports_access_control && obj_instance) {
        r = check_authorization(client, server_id, obj_ctx->obj->id,
            obj_instance->id, SOL_LWM2M_ACL_READ);

        if (r > 0) {
            SOL_DBG("Server ID %" PRId64 " authorized for R on"
                " Object Instance /%" PRIu16 "/%" PRIu16,
                server_id, obj_ctx->obj->id, obj_instance->id);
        } else if (r == 0) {
            SOL_WRN("Server ID %" PRId64 " is not authorized for R on"
                " Object Instance /%" PRIu16 "/%" PRIu16,
                server_id, obj_ctx->obj->id, obj_instance->id);
            return SOL_COAP_RESPONSE_CODE_UNAUTHORIZED;
        } else {
            SOL_WRN("Error checking for authorization. Server ID:"
                " %" PRId64 "; Object Instance: /%" PRIu16 "/%" PRIu16 "; Reason: %d",
                server_id, obj_ctx->obj->id, obj_instance->id, r);
            return SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
        }
    }

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
        bool read_an_instance = false;

        SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, instance, i) {
            if (instance->should_delete)
                continue;

            if (client->supports_access_control) {
                r = check_authorization(client, server_id, obj_ctx->obj->id,
                    instance->id, SOL_LWM2M_ACL_READ);

                if (r > 0) {
                    SOL_DBG("Server ID %" PRId64 " authorized for R on"
                        " Object Instance /%" PRIu16 "/%" PRIu16,
                        server_id, obj_ctx->obj->id, instance->id);
                } else if (r == 0) {
                    SOL_WRN("Server ID %" PRId64 " is not authorized for R on"
                        " Object Instance /%" PRIu16 "/%" PRIu16,
                        server_id, obj_ctx->obj->id, instance->id);
                    continue;
                } else {
                    SOL_WRN("Error checking for authorization. Server ID:"
                        " %" PRId64 "; Object Instance: /%" PRIu16 "/%" PRIu16 "; Reason: %d",
                        server_id, obj_ctx->obj->id, instance->id, r);
                    return SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
                }
            }

            read_an_instance = true;
            r = read_object_instance(client, obj_ctx, instance, &resources);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }
        //The server is not authorized to read the object!
        if (!read_an_instance)
            return SOL_COAP_RESPONSE_CODE_UNAUTHORIZED;
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

static int
notification_cb(void *data, struct sol_coap_server *server,
    struct sol_coap_resource *resource, struct sol_network_link_addr *addr,
    struct sol_coap_packet **pkt)
{
    struct notification_ctx *ctx = data;
    int r;
    int64_t server_id;

    r = get_server_id_by_link_addr(&ctx->client->connections, addr, &server_id);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    *pkt = sol_coap_packet_new_notification(server, resource);
    SOL_NULL_CHECK(*pkt, false);

    r = sol_coap_header_set_type(*pkt, SOL_COAP_MESSAGE_TYPE_CON);
    SOL_INT_CHECK_GOTO(r, < 0, err_pkt);
    r = sol_coap_header_set_code(*pkt, SOL_COAP_RESPONSE_CODE_CHANGED);
    SOL_INT_CHECK_GOTO(r, < 0, err_pkt);
    r = handle_read(ctx->client, ctx->obj_ctx, ctx->obj_instance,
        ctx->resource_id, *pkt, server_id);
    if ((uint8_t)r == SOL_COAP_RESPONSE_CODE_UNAUTHORIZED) {
        SOL_WRN("Server ID %" PRId64 " is not authorized for Notify [R]", server_id);
        r = -EPERM;
        goto err_pkt;
    } else if ((uint8_t)r != SOL_COAP_RESPONSE_CODE_CONTENT) {
        SOL_WRN("Error while reading data to create notification"
            " packet. Reason: %d", r);
        r = -EINVAL;
        goto err_pkt;
    }

    return 0;

err_pkt:
    sol_coap_packet_unref(*pkt);
err_exit:
    return r;
}

static bool
dispatch_notifications(struct sol_lwm2m_client *client,
    const struct sol_coap_resource *resource, bool is_delete)
{
    uint16_t i, path_idx = 0;
    struct obj_ctx *obj_ctx;
    bool stop = false;
    int r;
    struct notification_ctx ctx = { };

    if (client->splitted_path_len)
        path_idx = client->splitted_path_len;

    ctx.client = client;

    SOL_VECTOR_FOREACH_IDX (&client->objects, obj_ctx, i) {
        struct obj_instance *instance;
        uint16_t j;

        if (!sol_str_slice_eq(obj_ctx->obj_res->path[path_idx],
            resource->path[path_idx]))
            continue;

        ctx.obj_ctx = obj_ctx;
        ctx.resource_id = -1;
        SOL_COAP_NOTIFY_BY_CALLBACK_ALL_INT(obj_ctx->obj_res);

        if (!resource->path[1].len || is_delete)
            break;

        SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, instance, j) {
            uint16_t k;
            struct resource_ctx *res_ctx;

            if (!sol_str_slice_eq(instance->instance_res->path[path_idx + 1],
                resource->path[path_idx + 1]))
                continue;

            ctx.obj_instance = instance;
            SOL_COAP_NOTIFY_BY_CALLBACK_ALL_INT(instance->instance_res);

            if (!resource->path[2].len) {
                stop = true;
                break;
            }

            SOL_VECTOR_FOREACH_IDX (&instance->resources_ctx, res_ctx, k) {
                if (!sol_str_slice_eq(res_ctx->res->path[path_idx + 2],
                    resource->path[path_idx + 2]))
                    continue;

                ctx.resource_id = k;
                SOL_COAP_NOTIFY_BY_CALLBACK_ALL_INT(res_ctx->res);

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
    uint16_t i;
    int64_t server_id = INT64_MIN;

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

    r = get_server_id_by_link_addr(&client->connections, cliaddr, &server_id);
    header_code = SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    if (path_size >= 1 && !client->is_bootstrapping &&
        obj_ctx->obj->id == SECURITY_OBJECT_ID) {
        SOL_WRN("Only the Bootstrap Server is allowed to access the Security Object."
            " Server ID %" PRId64 " trying to access it", server_id);
        header_code = SOL_COAP_RESPONSE_CODE_UNAUTHORIZED;
        goto exit;
    }

    switch (method) {
    case SOL_COAP_METHOD_GET:
        if (is_observe_request(req)) {
            uint8_t obs = 1;

            if (client->supports_access_control) {
                r = check_authorization(client, server_id, path[0],
                    path_size > 1 ? path[1] : -1, SOL_LWM2M_ACL_READ);

                if (r > 0) {
                    SOL_DBG("Server ID %" PRId64 " authorized for Observe"
                        " [R] on Object Instance /%" PRIu16 "/%" PRIu16,
                        server_id, path[0], path_size > 1 ? path[1] : -1);
                } else if (r == 0) {
                    SOL_WRN("Server ID %" PRId64 " is not authorized for Observe"
                        " [R] on Object Instance /%" PRIu16 "/%" PRIu16,
                        server_id, path[0], path_size > 1 ? path[1] : -1);
                    header_code = SOL_COAP_RESPONSE_CODE_UNAUTHORIZED;
                    goto exit;
                } else {
                    SOL_WRN("Error checking for authorization. Server ID:"
                        " %" PRId64 "; Object Instance: /%" PRIu16 "/%" PRIu16 "; Reason: %d",
                        server_id, path[0], path_size > 1 ? path[1] : -1, r);
                    header_code = SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
                    goto exit;
                }
            }

            r = add_coap_int_option(resp, SOL_COAP_OPTION_OBSERVE,
                &obs, sizeof(obs));
            SOL_INT_CHECK_GOTO(r, < 0, exit);
        }
        header_code = handle_read(client, obj_ctx, obj_instance,
            path_size > 2 ? path[2] : -1, resp, server_id);
        break;
    case SOL_COAP_METHOD_POST:
        if (path_size == 1)
            //This is a create op
            header_code = handle_create(client, obj_ctx, -1, payload, server_id, true);
        else if (path_size == 2 && !obj_instance)
            //This is a create with chosen by the LWM2M server.
            header_code = handle_create(client, obj_ctx, path[1], payload, server_id, true);
        else if (path_size == 2)
            //Management Write on object instance
            header_code = handle_write(client, obj_ctx, obj_instance,
                path, path_size, payload, server_id);
        else {
            //Execute.
            is_execute = true;
            header_code = handle_execute(client, obj_ctx, obj_instance, path[2], payload, server_id);
        }
        break;
    case SOL_COAP_METHOD_PUT:
        if ((path_size == 3 && !client->is_bootstrapping) ||
            client->is_bootstrapping)
            //Bootstrap Write on Obj, ObjInst or Res; or Management Write on a resource.
            header_code = handle_write(client, obj_ctx, obj_instance,
                path, path_size, payload, server_id);
        else {
            header_code = SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
            SOL_WRN("Write request without full path specified!");
        }
        break;
    case SOL_COAP_METHOD_DELETE:
        header_code = handle_delete(client, obj_ctx, obj_instance, server_id);
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

    if (header_code == SOL_COAP_RESPONSE_CODE_DELETED && path_size > 0) {
        obj_instance_clear(client, obj_ctx, obj_instance);
        sol_vector_del_element(&obj_ctx->instances, obj_instance);

        //If the server performing the Delete operation is the owner of the
        // associated Access Control Object Instance, delete it as well
        if (client->supports_access_control) {
            struct sol_lwm2m_resource res[2];
            obj_ctx = find_object_ctx_by_id(client, ACCESS_CONTROL_OBJECT_ID);
            SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, obj_instance, i) {
                r = read_resources(client, obj_ctx, obj_instance, res, 2,
                    ACCESS_CONTROL_OBJECT_INSTANCE_RES_ID,
                    ACCESS_CONTROL_OBJECT_OWNER_RES_ID);
                if (r < 0) {
                    SOL_WRN("Could not read Access Control"
                        " Object's [Instance ID] and [Owner ID] resources\n");
                    continue;
                }

                if (res[0].data->content.integer == path[1] &&
                    res[1].data->content.integer == server_id) {
                    r = obj_ctx->obj->del((void *)obj_instance->data,
                        (void *)client->user_data, client, obj_instance->id);
                    if (r < 0) {
                        SOL_WRN("Could not properly delete object id %"
                            PRIu16 " instance id: %" PRIu16 " reason:%d",
                            obj_ctx->obj->id, obj_instance->id, r);
                    }

                    obj_instance->should_delete = true;
                    break;
                }
            }

            clear_resource_array(res, sol_util_array_size(res));
        }
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

static sol_coap_server *
get_coap_server_by_security_mode(struct sol_lwm2m_client *client,
    enum sol_lwm2m_security_mode sec_mode)
{
    switch (sec_mode) {
    case SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY:
        return client->dtls_server_psk;
    case SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY:
        return client->dtls_server_rpk;
    case SOL_LWM2M_SECURITY_MODE_CERTIFICATE:
        return NULL;
    case SOL_LWM2M_SECURITY_MODE_NO_SEC:
        return client->coap_server;
    default:
        return NULL;
    }
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

        if (obj_ctx->obj->id == ACCESS_CONTROL_OBJECT_ID) {
            client->supports_access_control = true;
        }
    }

    client->name = strdup(name);
    SOL_NULL_CHECK_GOTO(client->name, err_obj);

    if (sms) {
        client->sms = strdup(sms);
        SOL_NULL_CHECK_GOTO(client->sms, err_sms);
    }

    client->coap_server = sol_coap_server_new(&servaddr, false);
    SOL_NULL_CHECK_GOTO(client->coap_server, err_coap);

    client->dtls_server_psk = sol_coap_server_new_by_cipher_suites(&servaddr,
        (enum sol_socket_dtls_cipher []){ SOL_SOCKET_DTLS_CIPHER_PSK_AES128_CCM8 }, 1);
    if (!client->dtls_server_psk) {
        if (errno == ENOSYS) {
            SOL_INF("DTLS support not built in, LWM2M client"
                " running only \"NoSec\" security mode");
        } else {
            SOL_WRN("DTLS server for Pre-Shared Key mode"
                " could not be created for LWM2M client: %s",
                sol_util_strerrora(errno));
            goto err_dtls_psk;
        }
    }

    client->dtls_server_rpk = sol_coap_server_new_by_cipher_suites(&servaddr,
        (enum sol_socket_dtls_cipher []){ SOL_SOCKET_DTLS_CIPHER_ECDHE_ECDSA_AES128_CCM8 }, 1);
    if (!client->dtls_server_rpk) {
        if (errno == ENOSYS) {
            SOL_INF("DTLS support not built in, LWM2M client"
                " running only \"NoSec\" security mode");
        } else {
            SOL_WRN("DTLS server for Raw Public Key mode"
                " could not be created for LWM2M client: %s",
                sol_util_strerrora(errno));
            goto err_dtls_rpk;
        }
    }

    sol_monitors_init(&client->bootstrap, NULL);

    client->first_time_starting = true;

    client->user_data = data;

    return client;

err_dtls_rpk:
    sol_coap_server_unref(client->dtls_server_psk);
err_dtls_psk:
    sol_coap_server_unref(client->coap_server);
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
    sol_ptr_vector_del_element(conns, conn_ctx);
    server_connection_ctx_free(conn_ctx);
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

    if (client->dtls_server_psk)
        sol_coap_server_unref(client->dtls_server_psk);
    if (client->dtls_server_rpk)
        sol_coap_server_unref(client->dtls_server_rpk);

    if (client->security)
        sol_lwm2m_client_security_del(client->security);

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

    if (client->supports_access_control &&
        ctx->obj->id != SECURITY_OBJECT_ID &&
        ctx->obj->id != ACCESS_CONTROL_OBJECT_ID) {
        //Since this API is expected to be used only in Factory Bootstrap mode,
        // the owner of this Access Control Object Instance will be the
        // Bootstrap Server, as well as the only server allowed to perform
        // any (and all) operations, so there's no need for an ACL Resource
        r = setup_access_control_object_instance_for_instance(client,
            ctx->obj->id, instance->id, UINT16_MAX, NULL, false);

        if (r == 0) {
            SOL_DBG("Security Object Instance and Access Control Object Instance "
                " does not need an Access Control Object Instance nor ACLs");
        } else if (r != SOL_COAP_RESPONSE_CODE_CHANGED && r != SOL_COAP_RESPONSE_CODE_CREATED) {
            SOL_WRN("Failed to create Access Control Object Instance for Object /%"
                PRIu16 "/%" PRIu16, ctx->obj->id, instance->id);
            r = -ECANCELED;
            goto err_exit;
        }
    }

    return 0;

err_exit:
    sol_vector_del_element(&ctx->instances, instance);
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

        if (res[0].data[0].content.integer == server_id) {
            r = -EINVAL;
            SOL_INT_CHECK_GOTO(get_binding_mode_from_str(sol_str_slice_from_blob(res[2].data[0].content.blob)),
                == SOL_LWM2M_BINDING_MODE_UNKNOWN, exit);
            *lifetime = res[1].data[0].content.integer;
            *binding = sol_blob_ref(res[2].data[0].content.blob);
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

    SOL_DBG("Connecting with LWM2M server - id %" PRId64 " - binding '%.*s' -"
        " lifetime '%" PRId64 "' - sec_mode '%s'",
        conn_ctx->server_id, SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(binding)),
        conn_ctx->lifetime, get_security_mode_str(conn_ctx->sec_mode));
    r = sol_coap_send_packet_with_reply(
        get_coap_server_by_security_mode(client, conn_ctx->sec_mode),
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

    SOL_DBG("Sending Bootstrap Request to LWM2M Bootstrap Server"
        " using Security Mode %s", get_security_mode_str(conn_ctx->sec_mode));
    r = sol_coap_send_packet_with_reply(
        get_coap_server_by_security_mode(client, conn_ctx->sec_mode),
        pkt, sol_vector_get_no_check(&conn_ctx->server_addr_list,
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
    const struct sol_str_slice str_addr, int64_t server_id,
    enum sol_lwm2m_security_mode sec_mode)
{
    struct server_conn_ctx *conn_ctx;
    struct sol_http_url uri;
    int r;

    r = sol_http_split_uri(str_addr, &uri);
    SOL_INT_CHECK(r, < 0, NULL);

    SOL_NULL_CHECK(&uri.scheme, NULL);
    if (sol_str_slice_str_case_eq(uri.scheme, "coaps"))
        SOL_EXP_CHECK(sec_mode == SOL_LWM2M_SECURITY_MODE_NO_SEC, NULL);
    else if (sol_str_slice_str_case_eq(uri.scheme, "coap"))
        SOL_EXP_CHECK(sec_mode != SOL_LWM2M_SECURITY_MODE_NO_SEC, NULL);
    else
        return NULL;

    conn_ctx = calloc(1, sizeof(struct server_conn_ctx));
    SOL_NULL_CHECK(conn_ctx, NULL);

    r = sol_ptr_vector_append(&client->connections, conn_ctx);
    SOL_INT_CHECK_GOTO(r, < 0, err_append);
    conn_ctx->client = client;
    conn_ctx->server_id = server_id;
    conn_ctx->sec_mode = sec_mode;
    sol_vector_init(&conn_ctx->server_addr_list,
        sizeof(struct sol_network_link_addr));

    if (!uri.port)
        if (sec_mode != SOL_LWM2M_SECURITY_MODE_NO_SEC)
            conn_ctx->port = SOL_LWM2M_DEFAULT_SERVER_PORT_DTLS;
        else
            conn_ctx->port = SOL_LWM2M_DEFAULT_SERVER_PORT_COAP;
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
    sol_ptr_vector_del_element(&client->connections, conn_ctx);
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

static int
bootstrap_finish(void *data, struct sol_coap_server *coap,
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_lwm2m_client *client = data;
    struct sol_coap_packet *response;
    int r;
    struct server_conn_ctx *conn_ctx;
    struct sol_network_link_addr *server_addr;
    uint16_t i;

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

    SOL_PTR_VECTOR_FOREACH_IDX (&client->connections, conn_ctx, i) {
        server_addr = sol_vector_get_no_check(&conn_ctx->server_addr_list, conn_ctx->addr_list_idx);
        if (sol_network_link_addr_eq_full(cliaddr, server_addr, true)) {
            server_connection_ctx_remove(&client->connections, conn_ctx);
            break;
        }
    }

    return r;

err_exit:
    sol_coap_header_set_code(response, SOL_COAP_RESPONSE_CODE_BAD_REQUEST);
    sol_coap_send_packet(coap, response, cliaddr);
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
        DEFAULT_SHORT_SERVER_ID, client->bootstrap_ctx.sec_mode);

    if (!conn_ctx) {
        SOL_WRN("Could not perform Client-initiated Bootstrap with server %.*s"
            " through Security Mode %s",
            SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(client->bootstrap_ctx.server_uri)),
            get_security_mode_str(client->bootstrap_ctx.sec_mode));

        if (sol_coap_server_set_unknown_resource_handler(
            get_coap_server_by_security_mode(client, client->bootstrap_ctx.sec_mode),
            NULL, client) < 0)
            SOL_WRN("Could not unregister Bootstrap Unknown resource for client.");
        if (sol_coap_server_unregister_resource(
            get_coap_server_by_security_mode(client, client->bootstrap_ctx.sec_mode),
            &bootstrap_finish_interface) < 0)
            SOL_WRN("Could not unregister Bootstrap Finish resource for client.");
    }

    sol_blob_unref(client->bootstrap_ctx.server_uri);
    client->bootstrap_ctx.server_uri = NULL;

    return false;
}

static int
setup_access_control_object_instance_for_instance(
    struct sol_lwm2m_client *client,
    uint16_t target_object_id,
    uint16_t target_instance_id,
    int64_t owner_server_id,
    struct sol_lwm2m_resource *acl_res,
    bool register_with_coap)
{
    struct obj_ctx *acc_obj_ctx;
    int r;
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(NULL, 0, SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    struct sol_lwm2m_resource res[4] = { };
    int res_last_id = 4;
    struct sol_lwm2m_payload payload = { .type = SOL_LWM2M_CONTENT_TYPE_TLV };

    //Only the Client itself and the owner server is able to manage
    // Access Control Object Instances, and Security Object Instances
    // can only be managed by Bootstrap Servers, so there's no sense in
    // creating Access Control Object Instances or ACLs for them
    if (target_object_id == SECURITY_OBJECT_ID ||
        target_object_id == ACCESS_CONTROL_OBJECT_ID) {
        return 0;
    }

    acc_obj_ctx = find_object_ctx_by_id(client, ACCESS_CONTROL_OBJECT_ID);

    if (acl_res)
        res[2] = *acl_res;
    else
        res_last_id = 3;

    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &res[0],
        ACCESS_CONTROL_OBJECT_OBJECT_RES_ID, target_object_id);
    if (r < 0) {
        SOL_WRN("Could not init Access Control Object's [Object ID] resource\n");
        return r;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &res[1],
        ACCESS_CONTROL_OBJECT_INSTANCE_RES_ID, target_instance_id);
    if (r < 0) {
        SOL_WRN("Could not init Access Control Object's [Instance ID] resource\n");
        sol_lwm2m_resource_clear(&res[0]);
        return r;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &res[res_last_id - 1],
        ACCESS_CONTROL_OBJECT_OWNER_RES_ID, owner_server_id);
    if (r < 0) {
        SOL_WRN("Could not init Access Control Object's [Owner ID] resource\n");
        sol_lwm2m_resource_clear(&res[0]);
        sol_lwm2m_resource_clear(&res[1]);
        sol_lwm2m_resource_clear(&res[res_last_id - 1]);
        return r;
    }

    //From array of sol_lwm2m_resource to a sol_buffer in TLV format
    r = resources_to_tlv(res, res_last_id, &buf);
    sol_lwm2m_resource_clear(&res[0]);
    sol_lwm2m_resource_clear(&res[1]);
    sol_lwm2m_resource_clear(&res[res_last_id - 1]);
    SOL_INT_CHECK(r, < 0, r);

    //From sol_buffer to a sol_vector of sol_lwm2m_tlv
    r = sol_lwm2m_parse_tlv(sol_buffer_get_slice(&buf), &payload.payload.tlv_content);
    sol_buffer_fini(&buf);
    SOL_INT_CHECK(r, < 0, r);

    r = handle_create(client, acc_obj_ctx, -1, payload, UINT16_MAX, register_with_coap);
    sol_lwm2m_tlv_list_clear(&payload.payload.tlv_content);
    if (r == SOL_COAP_RESPONSE_CODE_CHANGED || r == SOL_COAP_RESPONSE_CODE_CREATED) {
        SOL_DBG("Access Control Object Instance for Object Instance /%"
            PRIu16 "/%" PRIu16 " created successfully!",
            target_object_id, target_instance_id);
    } else {
        SOL_WRN("Failed to create Access Control Object Instance for Object Instance /%"
            PRIu16 "/%" PRIu16, target_object_id, target_instance_id);
        return -ECANCELED;
    }

    return r;
}

static int
setup_access_control_object_instances(struct sol_lwm2m_client *client)
{
    struct obj_ctx *obj_ctx;
    int r;
    uint16_t i;
    struct obj_instance *instance;
    struct sol_lwm2m_resource res;
    struct sol_vector acl_instances;
    struct sol_lwm2m_resource_data *res_data;

    sol_vector_init(&acl_instances, sizeof(struct sol_lwm2m_resource_data));

    obj_ctx = find_object_ctx_by_id(client, SERVER_OBJECT_ID);
    if (obj_ctx && obj_ctx->instances.len > 0) {
        //If any Server Object Instance exists, this is already Bootstrapped and
        // one ACL will be created per existing [Server ID]
        SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, instance, i) {
            r = read_resources(client, obj_ctx, instance, &res, 1,
                SERVER_OBJECT_SERVER_ID);
            if (r < 0) {
                SOL_WRN("Could not read Server Object's [Server ID] resource\n");
                sol_vector_clear(&acl_instances);
                return r;
            }
            res_data = sol_vector_append(&acl_instances);
            r = -ENOMEM;
            SOL_NULL_CHECK_GOTO(res_data, err_vector);

            res_data->id = res.data->content.integer;
            res_data->content.integer = SOL_LWM2M_ACL_CREATE;

            sol_lwm2m_resource_clear(&res);
        }
    } else {
        //Else, there's no way to know which Server is authorized to
        // instantiate which Object, and thus <no Server> should be authorized
        // to instantiate <any> Object. However, since there's no way for the Bootstrap
        // Server to Read /2, in order to get the Access Control Instance relevant for
        // 'C'reate on a given Object, and change the ACLs granting access rights to a given
        // Server, <every Server> will be authorized to instantiate <every> Object
        SOL_DBG("LWM2M Server object not provided! No factory bootstrap.");

        res_data = sol_vector_append(&acl_instances);
        r = -ENOMEM;
        SOL_NULL_CHECK_GOTO(res_data, err_vector);

        res_data->id = DEFAULT_SHORT_SERVER_ID;
        res_data->content.integer = SOL_LWM2M_ACL_CREATE;
    }

    SOL_VECTOR_FOREACH_IDX (&client->objects, obj_ctx, i) {
        //No one is allowed to 'C'reate Security Objects, Server Objects,
        // and Access Control Objects
        if (obj_ctx->obj->id != SECURITY_OBJECT_ID &&
            obj_ctx->obj->id != SERVER_OBJECT_ID &&
            obj_ctx->obj->id != ACCESS_CONTROL_OBJECT_ID) {
            SOL_SET_API_VERSION(res.api_version = SOL_LWM2M_RESOURCE_API_VERSION; )
            r = sol_lwm2m_resource_init_vector(&res, ACCESS_CONTROL_OBJECT_ACL_RES_ID,
                SOL_LWM2M_RESOURCE_DATA_TYPE_INT, &acl_instances);
            if (r < 0) {
                SOL_WRN("Could not init Access Control Object's [ACL] resource\n");
                sol_vector_clear(&acl_instances);
                return r;
            }

            r = setup_access_control_object_instance_for_instance(client,
                obj_ctx->obj->id, UINT16_MAX, UINT16_MAX, &res, false);

            sol_lwm2m_resource_clear(&res);

            if (r == SOL_COAP_RESPONSE_CODE_CHANGED || r == SOL_COAP_RESPONSE_CODE_CREATED) {
                SOL_DBG("Access Control Object Instance for Object /%"
                    PRIu16 " created successfully!", obj_ctx->obj->id);
            } else if (r == 0) {
                SOL_DBG("Security Object and Access Control Object does"
                    " not need an Access Control Object Instance for 'Create' Operation");
            } else {
                SOL_WRN("Failed to create Access Control Object Instance for Object /%"
                    PRIu16, obj_ctx->obj->id);
                sol_vector_clear(&acl_instances);
                return -ECANCELED;
            }
        }
    }

    sol_vector_clear(&acl_instances);

    return 0;

err_vector:
    sol_vector_clear(&acl_instances);
    return r;
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
    struct sol_lwm2m_resource res[3] = { };
    enum sol_lwm2m_security_mode sec_mode = SOL_LWM2M_SECURITY_MODE_NO_SEC,
        bs_sec_mode = SOL_LWM2M_SECURITY_MODE_NO_SEC;
    int r;

    SOL_NULL_CHECK(client, -EINVAL);

    if (!client->first_time_starting && client->security) {
        sol_lwm2m_client_security_del(client->security);
        client->security = NULL;
    }

    if (client->supports_access_control &&
        client->first_time_starting) {
        r = setup_access_control_object_instances(client);
        SOL_INT_CHECK_GOTO(r, < 0, err_access_control);
    }

    client->first_time_starting = false;

    ctx = find_object_ctx_by_id(client, SECURITY_OBJECT_ID);
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
        //Setup DTLS parameters
        r = read_resources(client, ctx, instance, res, 1,
            SECURITY_SECURITY_MODE);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        sec_mode = res[0].data[0].content.integer;

        switch (res[0].data[0].content.integer) {
        case SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY:
            client->security = sol_lwm2m_client_security_add(client,
                SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY);
            if (!client->security) {
                r = -errno;
                SOL_ERR("Could not enable Pre-Shared Key security mode for LWM2M client");
                goto err_clear_1;
            } else {
                SOL_DBG("Using Pre-Shared Key security mode");
            }
            break;
        case SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY:
            client->security = sol_lwm2m_client_security_add(client,
                SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY);
            if (!client->security) {
                r = -errno;
                SOL_ERR("Could not enable Raw Public Key security mode for LWM2M client");
                goto err_clear_1;
            } else {
                SOL_DBG("Using Raw Public Key security mode");
            }
            break;
        case SOL_LWM2M_SECURITY_MODE_CERTIFICATE:
            SOL_WRN("Certificate security mode is not supported yet.");
            r = -ENOTSUP;
            goto err_clear_1;
        case SOL_LWM2M_SECURITY_MODE_NO_SEC:
            SOL_DBG("Using NoSec Security Mode (No DTLS).");
            break;
        default:
            SOL_WRN("Unknown DTLS [Security Mode] Resource from Security Object: %"
                PRId64, res[0].data[0].content.integer);
            r = -EINVAL;
            goto err_clear_1;
        }

        sol_lwm2m_resource_clear(&res[0]);

        r = read_resources(client, ctx, instance, res, 1,
            SECURITY_IS_BOOTSTRAP);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        //Is it a bootstap?
        if (!res[0].data[0].content.b) {
            sol_lwm2m_resource_clear(&res[0]);
            r = read_resources(client, ctx, instance, res, 2,
                SECURITY_SERVER_URI, SECURITY_SERVER_ID);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);

            conn_ctx = server_connection_ctx_new(client, sol_str_slice_from_blob(res[0].data[0].content.blob),
                res[1].data[0].content.integer, sec_mode);
            r = -ENOMEM;
            SOL_NULL_CHECK_GOTO(conn_ctx, err_clear_2);
            has_server = true;

            clear_resource_array(res, 2);
        } else {
            sol_lwm2m_resource_clear(&res[0]);
            bootstrap_account_idx = i;
            bs_sec_mode = sec_mode;
        }
    }

    //If all attempts to register failed, try to bootstrap
    if (!has_server) {
        SOL_DBG("The client did not specify a LWM2M server to connect."
            " Trying to bootstrap now.");

        client->is_bootstrapping = true;

        r = read_resources(client, ctx,
            sol_vector_get_no_check(&ctx->instances, bootstrap_account_idx), res, 3,
            SECURITY_SERVER_URI, SECURITY_CLIENT_HOLD_OFF_TIME,
            SECURITY_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT);

        //Create '/bs' CoAP resource to receive Bootstrap Finish notification
        r = sol_coap_server_register_resource(
            get_coap_server_by_security_mode(client, bs_sec_mode),
            &bootstrap_finish_interface, client);
        SOL_INT_CHECK_GOTO(r, < 0, err_clear_3);

        //Create unknown CoAP resource to handle Bootstrap Write and Bootstrap Delete
        r = sol_coap_server_set_unknown_resource_handler(
            get_coap_server_by_security_mode(client, bs_sec_mode),
            &handle_unknown_bootstrap_resource, client);
        SOL_INT_CHECK_GOTO(r, < 0, err_unregister_bs);

        SOL_DBG("Expecting server-initiated Bootstrap for"
            " %" PRId64 " seconds", res[1].data[0].content.integer);

        //Expect server-initiated bootstrap with sol_timeout before client-initiated bootstrap
        client->bootstrap_ctx.server_uri = sol_blob_ref(res[0].data[0].content.blob);
        SOL_NULL_CHECK_GOTO(client->bootstrap_ctx.server_uri, err_unregister_unknown);

        client->bootstrap_ctx.sec_mode = bs_sec_mode;

        client->bootstrap_ctx.timeout = sol_timeout_add(res[1].data[0].content.integer * ONE_SECOND,
            client_bootstrap, client);
        SOL_NULL_CHECK_GOTO(client->bootstrap_ctx.timeout, err_unregister_unknown);

        clear_resource_array(res, sol_util_array_size(res));

        return 0;
    }

    SOL_VECTOR_FOREACH_IDX (&client->objects, ctx, i) {
        SOL_COAP_SERVER_REGISTER_RESOURCE_ALL_INT(ctx->obj_res);

        SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, j) {
            SOL_COAP_SERVER_REGISTER_RESOURCE_ALL_INT(instance->instance_res);

            SOL_VECTOR_FOREACH_IDX (&instance->resources_ctx, res_ctx, k) {
                SOL_COAP_SERVER_REGISTER_RESOURCE_ALL_INT(res_ctx->res);
            }
        }
    }

    client->running = true;

    return 0;

err_access_control:
    if (client->supports_access_control) {
        ctx = find_object_ctx_by_id(client, ACCESS_CONTROL_OBJECT_ID);
        SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, i) {
            obj_instance_clear(client, ctx, instance);
        }
        sol_vector_clear(&ctx->instances);
    }
err_unregister_unknown:
    if (sol_coap_server_set_unknown_resource_handler(
        get_coap_server_by_security_mode(client, bs_sec_mode), NULL, client) < 0)
        SOL_WRN("Could not unregister Bootstrap Unknown resource for client.");
err_unregister_bs:
    if (sol_coap_server_unregister_resource(
        get_coap_server_by_security_mode(client, bs_sec_mode), &bootstrap_finish_interface) < 0)
        SOL_WRN("Could not unregister Bootstrap Finish resource for client.");
err_clear_3:
    sol_lwm2m_resource_clear(&res[2]);
err_clear_2:
    sol_lwm2m_resource_clear(&res[1]);
err_clear_1:
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
        r = sol_coap_cancel_send_packet(
            get_coap_server_by_security_mode(client, conn_ctx->sec_mode),
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

    return sol_coap_send_packet(
        get_coap_server_by_security_mode(client, conn_ctx->sec_mode), pkt,
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
            r = sol_coap_cancel_send_packet(
                get_coap_server_by_security_mode(client, conn_ctx->sec_mode),
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
            SOL_COAP_SERVER_UNREGISTER_RESOURCE_ALL_INT(ctx->obj_res);

            SOL_VECTOR_FOREACH_IDX (&ctx->instances, instance, j) {
                SOL_COAP_SERVER_UNREGISTER_RESOURCE_ALL_INT(instance->instance_res);

                SOL_VECTOR_FOREACH_IDX (&instance->resources_ctx, res_ctx, k) {
                    SOL_COAP_SERVER_UNREGISTER_RESOURCE_ALL_INT(res_ctx->res);
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
        uint16_t j, k;
        struct obj_ctx *obj_ctx;
        struct obj_instance *obj_instance;
        struct resource_ctx *res_ctx;
        struct sol_vector tokens;
        uint16_t path[3];
        struct sol_str_slice *token;
        struct notification_ctx ctx = { };

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

        ctx.client = client;
        ctx.obj_ctx = obj_ctx;
        ctx.resource_id = -1;

        if (!notification_already_sent(&already_sent, obj_ctx)) {
            SOL_COAP_NOTIFY_BY_CALLBACK_ALL_GOTO(obj_ctx->obj_res);

            r = sol_ptr_vector_append(&already_sent, obj_ctx);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }

        if (!notification_already_sent(&already_sent, obj_instance)) {
            ctx.obj_instance = obj_instance;
            SOL_COAP_NOTIFY_BY_CALLBACK_ALL_GOTO(obj_instance->instance_res);

            r = sol_ptr_vector_append(&already_sent, obj_instance);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }

        ctx.resource_id = path[2];
        SOL_COAP_NOTIFY_BY_CALLBACK_ALL_GOTO(res_ctx->res);
    }

    sol_ptr_vector_clear(&already_sent);
    return 0;

err_exit_einval:
    r = -EINVAL;
err_exit:
    sol_ptr_vector_clear(&already_sent);
    return r;
}
