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

/*
   To run: ./lwm2m-sample-client <client name> [bootstrap]
   If [bootstrap] argument is not given:
   This LWM2M client sample will try to connect to a LWM2M server @ locahost:5683.
   If a location object is created by the LWM2M server, it will report its location
   every one second.

   If [bootstrap] argument is given:
   This LWM2M client sample will try to connect to a LWM2M server @ locahost:5683.
   It should fail and thus will expect a server-initiated bootstrap.
   If it doesn't happen in 5s, it will try to connect to a LWM2M bootstrap server
   @ localhost:5783 and perform client-initiated bootstrap.
   If the bootstrap is successfull, it will try to connect and register with the
   server received in the bootstrap information.
 */


#include "sol-lwm2m.h"
#include "sol-mainloop.h"
#include "sol-vector.h"
#include "sol-util.h"

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

#define LOCATION_OBJ_ID (6)
#define LOCATION_OBJ_LATITUDE_RES_ID (0)
#define LOCATION_OBJ_LONGITUDE_RES_ID (1)
#define LOCATION_OBJ_TIMESTAMP_RES_ID (5)

#define ONE_SECOND (1000)
#define LIFETIME (60)

#define SERVER_OBJ_ID (1)
#define SERVER_OBJ_SHORT_RES_ID (0)
#define SERVER_OBJ_LIFETIME_RES_ID (1)
#define SERVER_OBJ_BINDING_RES_ID (7)
#define SERVER_OBJ_REGISTRATION_UPDATE_RES_ID (8)

#define SECURITY_SERVER_OBJ_ID (0)
#define SECURITY_SERVER_SERVER_URI_RES_ID (0)
#define SECURITY_SERVER_IS_BOOTSTRAP_RES_ID (1)
#define SECURITY_SERVER_SERVER_ID_RES_ID (10)
#define SECURITY_SERVER_CLIENT_HOLD_OFF_TIME_RES_ID (11)
#define SECURITY_SERVER_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT_RES_ID (12)

struct client_data_ctx {
    bool has_location_instance;
    bool is_bootstrap;
};

struct security_obj_instance_ctx {
    struct sol_lwm2m_client *client;
    struct sol_blob *server_uri;
    bool is_bootstrap;
    int64_t server_id;
    int64_t client_hold_off_time;
    int64_t bootstrap_server_account_timeout;
};

struct server_obj_instance_ctx {
    struct sol_lwm2m_client *client;
    struct sol_blob *binding;
    int64_t server_id;
    int64_t lifetime;
};

struct location_obj_instance_ctx {
    struct sol_timeout *timeout;
    struct sol_lwm2m_client *client;
    double latitude;
    double longitude;
    int64_t timestamp;
};

static struct sol_blob bootstrap_server_addr = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"coap://localhost:5783",
    .size = sizeof("coap://localhost:5783") - 1,
    .refcnt = 1
};

static struct sol_blob server_addr = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"coap://localhost:5683",
    .size = sizeof("coap://localhost:5683") - 1,
    .refcnt = 1
};

static struct sol_blob binding = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"U",
    .size = sizeof("U") - 1,
    .refcnt = 1
};

static struct sol_blob *
coord_to_str(double d)
{
    int r;
    char *str;
    struct sol_blob *blob;

    r = asprintf(&str, "%g", d);
    if (r < 0) {
        fprintf(stderr, "Could not convert the latitude/longitude to string\n");
        return NULL;
    }

    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, str, strlen(str));

    if (!blob) {
        fprintf(stderr, "Could not create a blob to store the latitude/longitude\n");
        free(str);
        return NULL;
    }
    return blob;
}

static bool
change_location(void *data)
{
    struct location_obj_instance_ctx *instance_ctx = data;
    int r = 0;
    static const char *paths[] = { "/6/0/0",
                                   "/6/0/1", "/6/0/5", NULL };

    instance_ctx->latitude = ((double)rand() / (double)RAND_MAX);
    instance_ctx->longitude = ((double)rand() / (double)RAND_MAX);

    instance_ctx->timestamp = (int64_t)time(NULL);

    printf("New latitude: %g - New longitude: %g\n", instance_ctx->latitude,
        instance_ctx->longitude);

    r = sol_lwm2m_client_notify(instance_ctx->client, paths);

    if (r < 0) {
        fprintf(stderr, "Could not notify the observers\n");
    } else
        printf("Sending new location coordinates to the observers\n");

    return true;
}

static int
create_location_obj(void *user_data, struct sol_lwm2m_client *client,
    uint16_t instance_id, void **instance_data, struct sol_lwm2m_payload payload)
{
    struct location_obj_instance_ctx *instance_ctx;
    struct client_data_ctx *data_ctx = user_data;
    int r;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;

    //Only one location object is allowed
    if (data_ctx->has_location_instance) {
        fprintf(stderr, "Only one location object instance is allowed\n");
        return -EINVAL;
    }

    if (payload.type != SOL_LWM2M_CONTENT_TYPE_TLV) {
        fprintf(stderr, "Content type is not in TLV format\n");
        return -EINVAL;
    }

    instance_ctx = calloc(1, sizeof(struct location_obj_instance_ctx));
    if (!instance_ctx) {
        fprintf(stderr, "Could not alloc memory for location object context\n");
        return -ENOMEM;
    }

    instance_ctx->timeout = sol_timeout_add(ONE_SECOND, change_location,
        instance_ctx);
    if (!instance_ctx->timeout) {
        fprintf(stderr, "Could not create the client timer\n");
        r = -ENOMEM;
        goto err_free_instance;
    }

    if (payload.payload.tlv_content.len != 3) {
        r = -EINVAL;
        fprintf(stderr, "Missing mandatory fields.\n");
        goto err_free_timeout;
    }

    SOL_VECTOR_FOREACH_IDX (&payload.payload.tlv_content, tlv, i) {
        SOL_BUFFER_DECLARE_STATIC(buf, 32);
        double *prop = NULL;

        if (tlv->id == LOCATION_OBJ_LATITUDE_RES_ID) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            prop = &instance_ctx->latitude;
        } else if (tlv->id == LOCATION_OBJ_LONGITUDE_RES_ID) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            prop = &instance_ctx->longitude;
        } else
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->timestamp);

        if (r < 0) {
            fprintf(stderr, "Could not get the tlv value for resource %"
                PRIu16 "\n", tlv->id);
            goto err_free_timeout;
        }

        if (buf.used) {
            char *endptr = NULL;

            *prop = sol_util_strtod_n(buf.data, &endptr, buf.used, false);

            if (errno != 0 || endptr == buf.data) {
                r = -EINVAL;
                fprintf(stderr, "Could not copy the longitude/latitude"
                    " property\n");
                goto err_free_timeout;
            }
        }
        sol_buffer_fini(&buf);
    }

    instance_ctx->client = client;
    *instance_data = instance_ctx;
    data_ctx->has_location_instance = true;
    printf("Location object created\n");

    return 0;

err_free_timeout:
    sol_timeout_del(instance_ctx->timeout);
err_free_instance:
    free(instance_ctx);
    return r;
}

static int
read_location_obj(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id,
    uint16_t res_id, struct sol_lwm2m_resource *res)
{
    struct sol_blob *blob;
    struct location_obj_instance_ctx *ctx = instance_data;
    int r;

    switch (res_id) {
    case LOCATION_OBJ_LATITUDE_RES_ID:
        blob = coord_to_str(ctx->latitude);
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, blob);
        sol_blob_unref(blob);
        break;
    case LOCATION_OBJ_LONGITUDE_RES_ID:
        blob = coord_to_str(ctx->longitude);
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, blob);
        sol_blob_unref(blob);
        break;
    case LOCATION_OBJ_TIMESTAMP_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_TIME, ctx->timestamp);
        break;
    default:
        if (res_id >= 2 && res_id <= 4)
            r = -ENOENT;
        else
            r = -EINVAL;
    }

    return r;
}

static int
read_security_obj(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, struct sol_lwm2m_resource *res)
{
    struct security_obj_instance_ctx *ctx = instance_data;
    int r;

    //It implements only the necassary info to connect to a LWM2M server
    // or bootstrap server without encryption.
    switch (res_id) {
    case SECURITY_SERVER_SERVER_URI_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, ctx->server_uri);
        break;
    case SECURITY_SERVER_IS_BOOTSTRAP_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, ctx->is_bootstrap);
        break;
    case SECURITY_SERVER_SERVER_ID_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->server_id);
        break;
    case SECURITY_SERVER_CLIENT_HOLD_OFF_TIME_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->client_hold_off_time);
        break;
    case SECURITY_SERVER_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->bootstrap_server_account_timeout);
        break;
    default:
        if (res_id >= 2 && res_id <= 9)
            r = -ENOENT;
        else
            r = -EINVAL;
    }

    return r;
}

static int
write_security_res(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, const struct sol_lwm2m_resource *res)
{
    struct security_obj_instance_ctx *instance_ctx = instance_data;
    int r = 0;

    switch (res->id) {
    case SECURITY_SERVER_SERVER_URI_RES_ID:
        sol_blob_unref(instance_ctx->server_uri);
        instance_ctx->server_uri = sol_blob_ref(res->data->content.blob);
        break;
    case SECURITY_SERVER_IS_BOOTSTRAP_RES_ID:
        instance_ctx->is_bootstrap = res->data->content.b;
        break;
    case SECURITY_SERVER_SERVER_ID_RES_ID:
        instance_ctx->server_id = res->data->content.integer;
        break;
    case SECURITY_SERVER_CLIENT_HOLD_OFF_TIME_RES_ID:
        instance_ctx->client_hold_off_time = res->data->content.integer;
        break;
    case SECURITY_SERVER_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT_RES_ID:
        instance_ctx->bootstrap_server_account_timeout = res->data->content.integer;
        break;
    default:
        if (res->id >= 2 && res->id <= 9)
            r = -ENOENT;
        else
            r = -EINVAL;
    }

    if (r >= 0)
        printf("Resource written to Security object at /0/%" PRIu16 "/%" PRIu16 "\n",
            instance_id, res->id);
    return r;
}

static int
write_security_tlv(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, struct sol_vector *tlvs)
{
    int r = 0;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;
    struct security_obj_instance_ctx *instance_ctx = instance_data;

    SOL_VECTOR_FOREACH_IDX (tlvs, tlv, i) {
        SOL_BUFFER_DECLARE_STATIC(buf, 64);

        switch (tlv->id) {
        case SECURITY_SERVER_SERVER_URI_RES_ID:
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                return r;
            sol_blob_unref(instance_ctx->server_uri);
            instance_ctx->server_uri = sol_buffer_to_blob(&buf);
            if (!instance_ctx->server_uri)
                return -EINVAL;
            break;
        case SECURITY_SERVER_IS_BOOTSTRAP_RES_ID:
            r = sol_lwm2m_tlv_get_bool(tlv, &instance_ctx->is_bootstrap);
            if (r < 0)
                return r;
            break;
        case SECURITY_SERVER_SERVER_ID_RES_ID:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->server_id);
            if (r < 0)
                return r;
            break;
        case SECURITY_SERVER_CLIENT_HOLD_OFF_TIME_RES_ID:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->client_hold_off_time);
            if (r < 0)
                return r;
            break;
        case SECURITY_SERVER_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT_RES_ID:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->bootstrap_server_account_timeout);
            if (r < 0)
                return r;
            break;
        default:
            fprintf(stderr, "tlv type: %u, ID: %" PRIu16 ", Size: %zu, Content: %.*s"
                " could not be written to Security Server Object at /0/%" PRIu16,
                tlv->type, tlv->id, tlv->content.used,
                SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&tlv->content)), instance_id);
            return -EINVAL;
        }
    }

    if (tlvs->len == 1 && r >= 0)
        printf("TLV written to Security object at /1/%" PRIu16 "/%" PRIu16 "\n",
            instance_id, tlv->id);
    else
        printf("TLV written to Security object at /1/%" PRIu16 "\n", instance_id);

    return r;
}

static int
create_security_obj(void *user_data, struct sol_lwm2m_client *client,
    uint16_t instance_id, void **instance_data,
    struct sol_lwm2m_payload payload)
{
    struct security_obj_instance_ctx *instance_ctx;
    int r;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;

    if (payload.type != SOL_LWM2M_CONTENT_TYPE_TLV) {
        fprintf(stderr, "Content type is not in TLV format\n");
        return -EINVAL;
    }

    instance_ctx = calloc(1, sizeof(struct security_obj_instance_ctx));
    if (!instance_ctx) {
        fprintf(stderr, "Could not alloc memory for security object context\n");
        return -ENOMEM;
    }

    SOL_VECTOR_FOREACH_IDX (&payload.payload.tlv_content, tlv, i) {
        SOL_BUFFER_DECLARE_STATIC(buf, 64);

        if (tlv->id == SECURITY_SERVER_SERVER_URI_RES_ID) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                goto err_free_instance;
            instance_ctx->server_uri = sol_buffer_to_blob(&buf);
            if (!instance_ctx->server_uri) {
                fprintf(stderr, "Could not set server_uri resource\n");
                goto err_free_instance;
            }
        } else if (tlv->id == SECURITY_SERVER_IS_BOOTSTRAP_RES_ID) {
            r = sol_lwm2m_tlv_get_bool(tlv, &instance_ctx->is_bootstrap);
        } else if (tlv->id == SECURITY_SERVER_SERVER_ID_RES_ID) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->server_id);
        } else if (tlv->id == SECURITY_SERVER_CLIENT_HOLD_OFF_TIME_RES_ID) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->client_hold_off_time);
        } else if (tlv->id == SECURITY_SERVER_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT_RES_ID) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->bootstrap_server_account_timeout);
        }

        if (r < 0) {
            fprintf(stderr, "Could not get the tlv value for resource %"
                PRIu16 "\n", tlv->id);
            goto err_free_instance;
        }
    }

    instance_ctx->client = client;
    *instance_data = instance_ctx;
    printf("Security object created at /0/%" PRIu16 "\n", instance_id);

    return 0;

err_free_instance:
    free(instance_ctx);
    return r;
}

static int
del_security_obj(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id)
{
    struct security_obj_instance_ctx *instance_ctx = instance_data;

    sol_blob_unref(instance_ctx->server_uri);
    free(instance_ctx);
    return 0;
}

static int
read_server_obj(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, struct sol_lwm2m_resource *res)
{
    struct server_obj_instance_ctx *ctx = instance_data;
    int r;

    //It implements only the necassary info to connect to a LWM2M server
    // without encryption.
    switch (res_id) {
    case SERVER_OBJ_SHORT_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->server_id);
        break;
    case SERVER_OBJ_LIFETIME_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->lifetime);
        break;
    case SERVER_OBJ_BINDING_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, ctx->binding);
        break;
    default:
        if (res_id >= 2 && res_id <= 6)
            r = -ENOENT;
        else
            r = -EINVAL;
    }

    return r;
}

static int
write_server_res(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, const struct sol_lwm2m_resource *res)
{
    struct server_obj_instance_ctx *instance_ctx = instance_data;
    int r = 0;

    switch (res->id) {
    case SERVER_OBJ_SHORT_RES_ID:
        instance_ctx->server_id = res->data->content.integer;
        break;
    case SERVER_OBJ_LIFETIME_RES_ID:
        instance_ctx->lifetime = res->data->content.integer;
        break;
    case SERVER_OBJ_BINDING_RES_ID:
        sol_blob_unref(instance_ctx->binding);
        instance_ctx->binding = sol_blob_ref(res->data->content.blob);
        break;
    default:
        if (res->id >= 2 && res->id <= 6)
            r = -ENOENT;
        else
            r = -EINVAL;
    }

    if (r >= 0)
        printf("Resource written to Server object at /1/%" PRIu16 "/%" PRIu16 "\n",
            instance_id, res->id);
    return r;
}

static int
write_server_tlv(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, struct sol_vector *tlvs)
{
    int r = 0;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;
    struct server_obj_instance_ctx *instance_ctx = instance_data;

    SOL_VECTOR_FOREACH_IDX (tlvs, tlv, i) {
        SOL_BUFFER_DECLARE_STATIC(buf, 64);

        switch (tlv->id) {
        case SERVER_OBJ_SHORT_RES_ID:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->server_id);
            if (r < 0)
                return r;
            break;
        case SERVER_OBJ_LIFETIME_RES_ID:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->lifetime);
            if (r < 0)
                return r;
            break;
        case SERVER_OBJ_BINDING_RES_ID:
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                return r;
            sol_blob_unref(instance_ctx->binding);
            instance_ctx->binding = sol_buffer_to_blob(&buf);
            if (!instance_ctx->binding)
                return -EINVAL;
            break;
        default:
            fprintf(stderr, "tlv type: %u, ID: %" PRIu16 ", Size: %zu, Content: %.*s"
                " could not be written to Server Object at /1/%" PRIu16,
                tlv->type, tlv->id, tlv->content.used,
                SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&tlv->content)), instance_id);
            return -EINVAL;
        }
    }

    if (tlvs->len == 1 && r >= 0)
        printf("TLV written to Server object at /1/%" PRIu16 "/%" PRIu16 "\n",
            instance_id, tlv->id);
    else
        printf("TLV written to Server object at /1/%" PRIu16 "\n", instance_id);

    return r;
}

static int
create_server_obj(void *user_data, struct sol_lwm2m_client *client,
    uint16_t instance_id, void **instance_data,
    struct sol_lwm2m_payload payload)
{
    struct server_obj_instance_ctx *instance_ctx;
    int r;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;

    if (payload.type != SOL_LWM2M_CONTENT_TYPE_TLV) {
        fprintf(stderr, "Content type is not in TLV format\n");
        return -EINVAL;
    }

    instance_ctx = calloc(1, sizeof(struct server_obj_instance_ctx));
    if (!instance_ctx) {
        fprintf(stderr, "Could not alloc memory for server object context\n");
        return -ENOMEM;
    }

    SOL_VECTOR_FOREACH_IDX (&payload.payload.tlv_content, tlv, i) {
        SOL_BUFFER_DECLARE_STATIC(buf, 64);

        if (tlv->id == SERVER_OBJ_SHORT_RES_ID) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->server_id);
        } else if (tlv->id == SERVER_OBJ_LIFETIME_RES_ID) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->lifetime);
        } else if (tlv->id == SERVER_OBJ_BINDING_RES_ID) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                goto err_free_instance;
            instance_ctx->binding = sol_buffer_to_blob(&buf);
            if (!instance_ctx->binding) {
                fprintf(stderr, "Could not set binding resource\n");
                goto err_free_instance;
            }
        }

        if (r < 0) {
            fprintf(stderr, "Could not get the tlv value for resource %"
                PRIu16 "\n", tlv->id);
            goto err_free_instance;
        }
    }

    instance_ctx->client = client;
    *instance_data = instance_ctx;
    printf("Server object created at /1/%" PRIu16 "\n", instance_id);

    return 0;

err_free_instance:
    free(instance_ctx);
    return r;
}

static int
execute_server_obj(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id,
    uint16_t res_id, const struct sol_str_slice args)
{
    if (res_id != SERVER_OBJ_REGISTRATION_UPDATE_RES_ID)
        return -EINVAL;

    return sol_lwm2m_client_send_update(client);
}

static int
del_server_obj(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id)
{
    struct server_obj_instance_ctx *instance_ctx = instance_data;

    sol_blob_unref(instance_ctx->binding);
    free(instance_ctx);
    return 0;
}

static int
del_location_obj(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id)
{
    struct location_obj_instance_ctx *instance_ctx = instance_data;
    struct client_data_ctx *data_ctx = user_data;

    if (instance_ctx->timeout)
        sol_timeout_del(instance_ctx->timeout);
    free(instance_ctx);
    data_ctx->has_location_instance = false;
    return 0;
}

static void
bootstrap_cb(void *data,
    struct sol_lwm2m_client *client,
    enum sol_lwm2m_bootstrap_event event)
{
    if (event == SOL_LWM2M_BOOTSTRAP_EVENT_FINISHED) {
        printf("...<Call local Bootstrap clean-up operations>...\n"
            "...<Now that it should have a Server, try to register again>\n");
        sol_lwm2m_client_start(client);
    } else if (event == SOL_LWM2M_BOOTSTRAP_EVENT_ERROR) {
        fprintf(stderr, "Bootstrap Request or Bootstrap Finish Failed!\n");
    }
}

static const struct sol_lwm2m_object location_object = {
    SOL_SET_API_VERSION(.api_version = SOL_LWM2M_OBJECT_API_VERSION, )
    .id = LOCATION_OBJ_ID,
    .create = create_location_obj,
    .read = read_location_obj,
    .del = del_location_obj,
    .resources_count = 6
};

static const struct sol_lwm2m_object security_object = {
    SOL_SET_API_VERSION(.api_version = SOL_LWM2M_OBJECT_API_VERSION, )
    .id = SECURITY_SERVER_OBJ_ID,
    .resources_count = 12,
    .read = read_security_obj,
    .create = create_security_obj,
    .del = del_security_obj,
    .write_resource = write_security_res,
    .write_tlv = write_security_tlv
};

static const struct sol_lwm2m_object server_object = {
    SOL_SET_API_VERSION(.api_version = SOL_LWM2M_OBJECT_API_VERSION, )
    .id = SERVER_OBJ_ID,
    .resources_count = 9,
    .read = read_server_obj,
    .create = create_server_obj,
    .del = del_server_obj,
    .write_resource = write_server_res,
    .write_tlv = write_server_tlv,
    .execute = execute_server_obj
};

int
main(int argc, char *argv[])
{
    struct sol_lwm2m_client *client;
    static const struct sol_lwm2m_object *objects[] =
    { &security_object, &server_object, &location_object, NULL };
    struct client_data_ctx data_ctx = { 0 };
    struct security_obj_instance_ctx *security_data;
    struct server_obj_instance_ctx *server_data;
    int r;

    srand(time(NULL));
    if (argc < 2) {
        fprintf(stderr, "Usage: ./lwm2m-sample-client <client-name> [bootstrap]\n");
        return -1;
    }

    sol_init();

    if (argv[2])
        data_ctx.is_bootstrap = true;

    client = sol_lwm2m_client_new(argv[1], NULL, NULL, objects,
        &data_ctx);

    if (!client) {
        r = -1;
        fprintf(stderr, "Could not the create the LWM2M client\n");
        goto exit;
    }

    security_data = calloc(1, sizeof(struct security_obj_instance_ctx));
    if (!security_data) {
        fprintf(stderr, "Could not alloc memory for security object context\n");
        return -ENOMEM;
    }

    security_data->client = client;

    if (!data_ctx.is_bootstrap) {
        server_data = calloc(1, sizeof(struct server_obj_instance_ctx));
        if (!server_data) {
            fprintf(stderr, "Could not alloc memory for server object context\n");
            return -ENOMEM;
        }

        server_data->client = client;
        server_data->binding = &binding;
        server_data->server_id = 101;
        server_data->lifetime = LIFETIME;

        r = sol_lwm2m_client_add_object_instance(client, &server_object, server_data);

        if (r < 0) {
            fprintf(stderr, "Could not add a server object instance\n");
            goto exit_del;
        }

        security_data->server_uri = &server_addr;
        security_data->is_bootstrap = false;
        security_data->server_id = 101;
    } else {
        r = sol_lwm2m_client_add_bootstrap_finish_monitor(client, bootstrap_cb,
            NULL);

        if (r < 0) {
            fprintf(stderr, "Could not add a bootstrap monitor\n");
            goto exit_del;
        }

        security_data->server_uri = &bootstrap_server_addr;
        security_data->is_bootstrap = true;
        security_data->client_hold_off_time = 5;
        security_data->bootstrap_server_account_timeout = 0;
    }

    r = sol_lwm2m_client_add_object_instance(client, &security_object, security_data);

    if (r < 0) {
        fprintf(stderr, "Could not add a security object instance\n");
        goto exit_del;
    }

    sol_lwm2m_client_start(client);

    sol_run();
    r = 0;

    sol_lwm2m_client_stop(client);

exit_del:
    sol_lwm2m_client_del(client);
exit:
    sol_shutdown();
    return r;
}
