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
   To run: ./lwm2m-sample-client <client name> [-b] [-s SEC_MODE]
   If [-b] argument is not given:
   This LWM2M client sample will try to connect to a LWM2M server @ locahost:5683
   (or @ localhost:5684 if -s is given).
   If a location object is created by the LWM2M server, it will report its location
   every one second.

   If [-b] argument is given:
   This LWM2M client sample expect a server-initiated bootstrap.
   If it doesn't happen in 5s, it will try to connect to a LWM2M bootstrap server
   @ localhost:5783 and perform client-initiated bootstrap.
   If the bootstrap is successful, it will try to connect and register with the
   server(s) received in the bootstrap information.
 */

#include "sol-lwm2m-client.h"
#include "sol-mainloop.h"
#include "sol-vector.h"
#include "sol-util.h"

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

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

#define ACCESS_CONTROL_OBJ_ID (2)
#define ACCESS_CONTROL_OBJ_OBJECT_RES_ID (0)
#define ACCESS_CONTROL_OBJ_INSTANCE_RES_ID (1)
#define ACCESS_CONTROL_OBJ_ACL_RES_ID (2)
#define ACCESS_CONTROL_OBJ_OWNER_RES_ID (3)

#define SECURITY_OBJ_ID (0)
#define SECURITY_SERVER_URI_RES_ID (0)
#define SECURITY_IS_BOOTSTRAP_RES_ID (1)
#define SECURITY_SECURITY_MODE_RES_ID (2)
#define SECURITY_PUBLIC_KEY_OR_IDENTITY_RES_ID (3)
#define SECURITY_SERVER_PUBLIC_KEY_RES_ID (4)
#define SECURITY_SECRET_KEY_RES_ID (5)
#define SECURITY_SERVER_ID_RES_ID (10)
#define SECURITY_CLIENT_HOLD_OFF_TIME_RES_ID (11)
#define SECURITY_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT_RES_ID (12)

#define PSK_KEY_LEN 16
#define RPK_PRIVATE_KEY_LEN 32
#define RPK_PUBLIC_KEY_LEN (2 * RPK_PRIVATE_KEY_LEN)

//FIXME: UNSEC: Hardcoded Crypto Keys
#define CLIENT_BS_PSK_ID ("cli1-bs")
#define CLIENT_BS_PSK_KEY ("FEDCBA9876543210")
#define CLIENT_SERVER_PSK_ID ("cli1")
#define CLIENT_SERVER_PSK_KEY ("0123456789ABCDEF")

#define CLIENT_PRIVATE_KEY ("D9E2707A72DA6A0504995C86EDDBE3EFC7F1CD74838F7570C8072D0A76261BD4")
#define CLIENT_PUBLIC_KEY ("D055EE14084D6E0615599DB583913E4A3E4526A2704D61F27A4CCFBA9758EF9A" \
    "B418B64AFE8030DA1DDCF4F42E2F2631D043B1FB03E22F4D17DE43F9F9ADEE70")
#define BS_SERVER_PUBLIC_KEY ("cd4110e97bbd6e7e5a800028079d02915c70b915ea4596402098deea585eb7ad" \
    "f3e080487327f70758b13bc0583f4293d13288a0164a8e324779aa4f7ada26c1")
#define SERVER_PUBLIC_KEY ("3b88c213ca5ccfd9c5a7f73715760d7d9a5220768f2992d2628ae1389cbca4c6" \
    "d1b73cc6d61ae58783135749fb03eaaa64a7a1adab8062ed5fc0d7b86ba2d5ca")

struct client_data_ctx {
    bool has_location_instance;
    bool is_bootstrap;
};

struct security_obj_instance_ctx {
    struct sol_lwm2m_client *client;
    struct sol_blob *server_uri;
    bool is_bootstrap;
    int64_t security_mode;
    struct sol_blob *public_key_or_id;
    struct sol_blob *server_public_key;
    struct sol_blob *secret_key;
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

struct acl_instance {
    uint16_t key;
    int64_t value;
};

struct access_control_obj_instance_ctx {
    struct sol_lwm2m_client *client;
    int64_t owner_id;
    int64_t object_id;
    int64_t instance_id;
    struct sol_vector acl;
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
    .mem = (void *)"coaps://localhost:5783",
    .size = sizeof("coaps://localhost:5783") - 1,
    .refcnt = 1
};

static struct sol_blob server_addr_coap = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"coap://localhost:5683",
    .size = sizeof("coap://localhost:5683") - 1,
    .refcnt = 1
};

static struct sol_blob server_addr_dtls = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"coaps://localhost:5684",
    .size = sizeof("coaps://localhost:5684") - 1,
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

    switch (res_id) {
    case SECURITY_SERVER_URI_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, ctx->server_uri);
        break;
    case SECURITY_IS_BOOTSTRAP_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, ctx->is_bootstrap);
        break;
    case SECURITY_SECURITY_MODE_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->security_mode);
        break;
    case SECURITY_PUBLIC_KEY_OR_IDENTITY_RES_ID:
        if (!ctx->public_key_or_id)
            r = -ENOENT;
        else
            SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
                SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, ctx->public_key_or_id);
        break;
    case SECURITY_SERVER_PUBLIC_KEY_RES_ID:
        if (!ctx->server_public_key)
            r = -ENOENT;
        else
            SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
                SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, ctx->server_public_key);
        break;
    case SECURITY_SECRET_KEY_RES_ID:
        if (!ctx->secret_key)
            r = -ENOENT;
        else
            SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
                SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, ctx->secret_key);
        break;
    case SECURITY_SERVER_ID_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->server_id);
        break;
    case SECURITY_CLIENT_HOLD_OFF_TIME_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->client_hold_off_time);
        break;
    case SECURITY_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT_RES_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->bootstrap_server_account_timeout);
        break;
    default:
        if (res_id >= 6 && res_id <= 9)
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
    case SECURITY_SERVER_URI_RES_ID:
        sol_blob_unref(instance_ctx->server_uri);
        instance_ctx->server_uri = sol_blob_ref(res->data->content.blob);
        break;
    case SECURITY_IS_BOOTSTRAP_RES_ID:
        instance_ctx->is_bootstrap = res->data->content.b;
        break;
    case SECURITY_SECURITY_MODE_RES_ID:
        instance_ctx->security_mode = res->data->content.integer;
        break;
    case SECURITY_PUBLIC_KEY_OR_IDENTITY_RES_ID:
        sol_blob_unref(instance_ctx->public_key_or_id);
        instance_ctx->public_key_or_id = sol_blob_ref(res->data->content.blob);
        break;
    case SECURITY_SERVER_PUBLIC_KEY_RES_ID:
        sol_blob_unref(instance_ctx->server_public_key);
        instance_ctx->server_public_key = sol_blob_ref(res->data->content.blob);
        break;
    case SECURITY_SECRET_KEY_RES_ID:
        sol_blob_unref(instance_ctx->secret_key);
        instance_ctx->secret_key = sol_blob_ref(res->data->content.blob);
        break;
    case SECURITY_SERVER_ID_RES_ID:
        instance_ctx->server_id = res->data->content.integer;
        break;
    case SECURITY_CLIENT_HOLD_OFF_TIME_RES_ID:
        instance_ctx->client_hold_off_time = res->data->content.integer;
        break;
    case SECURITY_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT_RES_ID:
        instance_ctx->bootstrap_server_account_timeout = res->data->content.integer;
        break;
    default:
        if (res->id >= 6 && res->id <= 9)
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
        case SECURITY_SERVER_URI_RES_ID:
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                return r;
            sol_blob_unref(instance_ctx->server_uri);
            instance_ctx->server_uri = sol_buffer_to_blob(&buf);
            if (!instance_ctx->server_uri)
                return -EINVAL;
            break;
        case SECURITY_IS_BOOTSTRAP_RES_ID:
            r = sol_lwm2m_tlv_get_bool(tlv, &instance_ctx->is_bootstrap);
            if (r < 0)
                return r;
            break;
        case SECURITY_SECURITY_MODE_RES_ID:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->security_mode);
            if (r < 0)
                return r;
            break;
        case SECURITY_PUBLIC_KEY_OR_IDENTITY_RES_ID:
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                return r;
            sol_blob_unref(instance_ctx->public_key_or_id);
            instance_ctx->public_key_or_id = sol_buffer_to_blob(&buf);
            if (!instance_ctx->public_key_or_id)
                return -EINVAL;
            break;
        case SECURITY_SERVER_PUBLIC_KEY_RES_ID:
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                return r;
            sol_blob_unref(instance_ctx->server_public_key);
            instance_ctx->server_public_key = sol_buffer_to_blob(&buf);
            if (!instance_ctx->server_public_key)
                return -EINVAL;
            break;
        case SECURITY_SECRET_KEY_RES_ID:
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                return r;
            sol_blob_unref(instance_ctx->secret_key);
            instance_ctx->secret_key = sol_buffer_to_blob(&buf);
            if (!instance_ctx->secret_key)
                return -EINVAL;
            break;
        case SECURITY_SERVER_ID_RES_ID:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->server_id);
            if (r < 0)
                return r;
            break;
        case SECURITY_CLIENT_HOLD_OFF_TIME_RES_ID:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->client_hold_off_time);
            if (r < 0)
                return r;
            break;
        case SECURITY_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT_RES_ID:
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

        if (tlv->id == SECURITY_SERVER_URI_RES_ID) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                goto err_free_instance;
            instance_ctx->server_uri = sol_buffer_to_blob(&buf);
            if (!instance_ctx->server_uri) {
                fprintf(stderr, "Could not set server_uri resource\n");
                goto err_free_instance;
            }
        } else if (tlv->id == SECURITY_IS_BOOTSTRAP_RES_ID) {
            r = sol_lwm2m_tlv_get_bool(tlv, &instance_ctx->is_bootstrap);
        } else if (tlv->id == SECURITY_SECURITY_MODE_RES_ID) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->security_mode);
        } else if (tlv->id == SECURITY_PUBLIC_KEY_OR_IDENTITY_RES_ID) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                goto err_free_instance;
            instance_ctx->public_key_or_id = sol_buffer_to_blob(&buf);
            if (!instance_ctx->public_key_or_id) {
                fprintf(stderr, "Could not set public_key_or_id resource\n");
                goto err_free_instance;
            }
        } else if (tlv->id == SECURITY_SERVER_PUBLIC_KEY_RES_ID) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                goto err_free_instance;
            instance_ctx->server_public_key = sol_buffer_to_blob(&buf);
            if (!instance_ctx->server_public_key) {
                fprintf(stderr, "Could not set server_public_key resource\n");
                goto err_free_instance;
            }
        } else if (tlv->id == SECURITY_SECRET_KEY_RES_ID) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            if (r < 0)
                goto err_free_instance;
            instance_ctx->secret_key = sol_buffer_to_blob(&buf);
            if (!instance_ctx->secret_key) {
                fprintf(stderr, "Could not set secret_key resource\n");
                goto err_free_instance;
            }
        } else if (tlv->id == SECURITY_SERVER_ID_RES_ID) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->server_id);
        } else if (tlv->id == SECURITY_CLIENT_HOLD_OFF_TIME_RES_ID) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->client_hold_off_time);
        } else if (tlv->id == SECURITY_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT_RES_ID) {
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
    sol_blob_unref(instance_ctx->server_uri);
    if (instance_ctx->public_key_or_id)
        sol_blob_unref(instance_ctx->public_key_or_id);
    if (instance_ctx->server_public_key)
        sol_blob_unref(instance_ctx->server_public_key);
    if (instance_ctx->secret_key)
        sol_blob_unref(instance_ctx->secret_key);
    free(instance_ctx);
    return r;
}

static int
del_security_obj(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id)
{
    struct security_obj_instance_ctx *instance_ctx = instance_data;

    sol_blob_unref(instance_ctx->server_uri);
    if (instance_ctx->public_key_or_id)
        sol_blob_unref(instance_ctx->public_key_or_id);
    if (instance_ctx->server_public_key)
        sol_blob_unref(instance_ctx->server_public_key);
    if (instance_ctx->secret_key)
        sol_blob_unref(instance_ctx->secret_key);
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

static int
read_access_control_obj(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, struct sol_lwm2m_resource *res)
{
    struct access_control_obj_instance_ctx *ctx = instance_data;
    int r;

    if (res_id == ACCESS_CONTROL_OBJ_OBJECT_RES_ID) {
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->object_id);
    } else if (res_id == ACCESS_CONTROL_OBJ_INSTANCE_RES_ID) {
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->instance_id);
    } else if (res_id == ACCESS_CONTROL_OBJ_ACL_RES_ID) {
        struct acl_instance *acl_item;
        uint16_t i;
        struct sol_vector acl_instances;
        struct sol_lwm2m_resource_data *res_data;

        if (ctx->acl.len == 0)
            return -ENOENT;

        sol_vector_init(&acl_instances, sizeof(struct sol_lwm2m_resource_data));

        SOL_VECTOR_FOREACH_IDX (&ctx->acl, acl_item, i) {
            res_data = sol_vector_append(&acl_instances);
            res_data->id = acl_item->key;
            res_data->content.integer = acl_item->value;
        }

        SOL_SET_API_VERSION(res->api_version = SOL_LWM2M_RESOURCE_API_VERSION; )
        r = sol_lwm2m_resource_init_vector(res, ACCESS_CONTROL_OBJ_ACL_RES_ID,
            SOL_LWM2M_RESOURCE_DATA_TYPE_INT, &acl_instances);

        sol_vector_clear(&acl_instances);
    } else if (res_id == ACCESS_CONTROL_OBJ_OWNER_RES_ID) {
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->owner_id);
    } else {
        r = -EINVAL;
    }

    return r;
}

static int
write_access_control_res(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, const struct sol_lwm2m_resource *res)
{
    struct access_control_obj_instance_ctx *instance_ctx = instance_data;
    int r = 0;
    struct acl_instance *acl_item;
    uint16_t i;

    switch (res->id) {
    case ACCESS_CONTROL_OBJ_OBJECT_RES_ID:
        instance_ctx->object_id = res->data->content.integer;
        break;
    case ACCESS_CONTROL_OBJ_INSTANCE_RES_ID:
        instance_ctx->instance_id = res->data->content.integer;
        break;
    case ACCESS_CONTROL_OBJ_ACL_RES_ID:
        if (res->type == SOL_LWM2M_RESOURCE_TYPE_MULTIPLE) {
            sol_vector_clear(&instance_ctx->acl);

            for (i = 0; i < res->data_len; i++) {
                acl_item = sol_vector_append(&instance_ctx->acl);
                if (!acl_item) {
                    fprintf(stderr, "Could not alloc memory for access control list Resource Instance\n");
                    r = -ENOMEM;
                    goto err_free_acl;
                }

                acl_item->key = res->data[i].id;
                acl_item->value = res->data[i].content.integer;
                printf("<<[WRITE_RES]<< acl[%" PRIu16 "]=%" PRId64 ">>>>\n", acl_item->key, acl_item->value);
            }
        } else {
            r = -EINVAL;
        }
        break;
    case ACCESS_CONTROL_OBJ_OWNER_RES_ID:
        instance_ctx->owner_id = res->data->content.integer;
        break;
    default:
        r = -EINVAL;
    }

    if (r >= 0)
        printf("Resource written to Access Control object at /2/%" PRIu16 "/%" PRIu16 "\n",
            instance_id, res->id);

    return r;

err_free_acl:
    sol_vector_clear(&instance_ctx->acl);
    return r;
}

static int
write_or_create_acl(struct sol_vector *acl,
    struct sol_vector *tlvs, uint16_t *j, bool is_create)
{
    struct acl_instance *acl_item;
    struct sol_lwm2m_tlv *res_tlv;
    int64_t res_val;
    int r;

    while ((res_tlv = sol_vector_get(tlvs, *j)) &&
        res_tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_INSTANCE) {
        r = sol_lwm2m_tlv_get_int(res_tlv, &res_val);
        if (r < 0)
            return r;

        acl_item = sol_vector_append(acl);
        if (!acl_item) {
            fprintf(stderr, "Could not alloc memory for access control list Resource Instance\n");
            return -ENOMEM;
        }

        acl_item->key = res_tlv->id;
        acl_item->value = res_val;
        if (is_create)
            printf("<<[CREATE]<< acl[%" PRIu16 "]=%" PRId64 ">>>>\n", acl_item->key, acl_item->value);
        else
            printf("<<[WRITE_TLV]<< acl[%" PRIu16 "]=%" PRId64 ">>>>\n", acl_item->key, acl_item->value);
        (*j)++;
    }

    return 0;
}

static int
write_access_control_tlv(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, struct sol_vector *tlvs)
{
    int r = -EINVAL;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;
    struct access_control_obj_instance_ctx *instance_ctx = instance_data;

    SOL_VECTOR_FOREACH_IDX (tlvs, tlv, i) {
        if (tlv->id == ACCESS_CONTROL_OBJ_OBJECT_RES_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->object_id);
            if (r < 0)
                return r;
        } else if (tlv->id == ACCESS_CONTROL_OBJ_INSTANCE_RES_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->instance_id);
            if (r < 0)
                return r;
        } else if (tlv->id == ACCESS_CONTROL_OBJ_ACL_RES_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_MULTIPLE_RESOURCES) {
            uint16_t j = i + 1;

            sol_vector_clear(&instance_ctx->acl);

            r = write_or_create_acl(&instance_ctx->acl, tlvs, &j, false);
            if (r < 0)
                goto err_free_acl;

            i = j - 1;
        } else if (tlv->id == ACCESS_CONTROL_OBJ_OWNER_RES_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->owner_id);
            if (r < 0)
                return r;
        } else {
            fprintf(stderr, "tlv type: %u, ID: %" PRIu16 ", Size: %zu, Content: %.*s"
                " could not be written to Access Control Object at /2/%" PRIu16,
                tlv->type, tlv->id, tlv->content.used,
                SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&tlv->content)), instance_id);
            return -EINVAL;
        }
    }

    if (tlvs->len == 1 && r >= 0)
        printf("TLV written to Access Control object at /2/%" PRIu16 "/%" PRIu16 "\n",
            instance_id, tlv->id);
    else
        printf("TLV written to Access Control object at /2/%" PRIu16 "\n", instance_id);

    return r;

err_free_acl:
    sol_vector_clear(&instance_ctx->acl);
    return r;
}

static int
create_access_control_obj(void *user_data, struct sol_lwm2m_client *client,
    uint16_t instance_id, void **instance_data,
    struct sol_lwm2m_payload payload)
{
    struct access_control_obj_instance_ctx *instance_ctx;
    int r;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;

    if (payload.type != SOL_LWM2M_CONTENT_TYPE_TLV) {
        fprintf(stderr, "Content type is not in TLV format\n");
        return -EINVAL;
    }

    instance_ctx = calloc(1, sizeof(struct access_control_obj_instance_ctx));
    if (!instance_ctx) {
        fprintf(stderr, "Could not alloc memory for access control object context\n");
        return -ENOMEM;
    }

    sol_vector_init(&instance_ctx->acl, sizeof(struct acl_instance));

    SOL_VECTOR_FOREACH_IDX (&payload.payload.tlv_content, tlv, i) {
        if (tlv->id == ACCESS_CONTROL_OBJ_OBJECT_RES_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->object_id);
        } else if (tlv->id == ACCESS_CONTROL_OBJ_INSTANCE_RES_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->instance_id);
        } else if (tlv->id == ACCESS_CONTROL_OBJ_ACL_RES_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_MULTIPLE_RESOURCES) {
            uint16_t j = i + 1;

            sol_vector_clear(&instance_ctx->acl);

            r = write_or_create_acl(&instance_ctx->acl, &payload.payload.tlv_content, &j, true);
            if (r < 0)
                goto err_free_acl;

            i = j - 1;
        } else if (tlv->id == ACCESS_CONTROL_OBJ_OWNER_RES_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->owner_id);
        }

        if (r < 0) {
            fprintf(stderr, "Could not get the tlv value for resource %"
                PRIu16 "\n", tlv->id);
            goto err_free_acl;
        }
    }

    instance_ctx->client = client;
    *instance_data = instance_ctx;
    printf("Access Control object created at /2/%" PRIu16 "\n\n", instance_id);

    return 0;

err_free_acl:
    sol_vector_clear(&instance_ctx->acl);
    free(instance_ctx);
    return r;
}

static int
del_access_control_obj(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id)
{
    struct access_control_obj_instance_ctx *instance_ctx = instance_data;

    sol_vector_clear(&instance_ctx->acl);
    free(instance_ctx);
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
    .id = SECURITY_OBJ_ID,
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

static const struct sol_lwm2m_object access_control_object = {
    SOL_SET_API_VERSION(.api_version = SOL_LWM2M_OBJECT_API_VERSION, )
    .id = ACCESS_CONTROL_OBJ_ID,
    .resources_count = 4,
    .read = read_access_control_obj,
    .create = create_access_control_obj,
    .del = del_access_control_obj,
    .write_resource = write_access_control_res,
    .write_tlv = write_access_control_tlv,
};

int
main(int argc, char *argv[])
{
    struct sol_lwm2m_client *client;
    static const struct sol_lwm2m_object *objects[] =
    { &security_object, &server_object, &access_control_object, &location_object, NULL };
    struct client_data_ctx data_ctx = { 0 };
    struct security_obj_instance_ctx *security_data = NULL;
    struct server_obj_instance_ctx *server_data = NULL;
    int r;
    enum sol_lwm2m_security_mode sec_mode = SOL_LWM2M_SECURITY_MODE_NO_SEC;
    unsigned char buf_aux[RPK_PUBLIC_KEY_LEN];
    struct sol_blob *public_key_or_id, *server_public_key, *secret_key;
    char *cli_name = NULL;
    char usage[256];

    srand(time(NULL));

    snprintf(usage, sizeof(usage), "Usage: ./lwm2m-sample-client <client-name> [-b] [-s SEC_MODE]\n"
        "Where Factory Bootstrap is default and SEC_MODE is an integer as per:\n"
        "\tPRE_SHARED_KEY=%d\n"
        "\tRAW_PUBLIC_KEY=%d\n"
        "\tCERTIFICATE=%d\n"
        "\tNO_SEC=%d (default)\n",
        SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY,
        SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY,
        SOL_LWM2M_SECURITY_MODE_CERTIFICATE,
        SOL_LWM2M_SECURITY_MODE_NO_SEC);

    while ((r = getopt(argc, argv, "bs:")) != -1) {
        switch (r) {
        case 'b':
            data_ctx.is_bootstrap = true;
            break;
        case 's':
            sec_mode = atoi(optarg);
            if (sec_mode < 0 || sec_mode > 3) {
                fprintf(stderr, "%s", usage);
                return -1;
            }
            break;
        default:
            fprintf(stderr, "%s", usage);
            return -1;
        }
    }

    cli_name = argv[optind];
    if (!cli_name) {
        fprintf(stderr, "%s", usage);
        return -1;
    }

    if (data_ctx.is_bootstrap && (sec_mode == SOL_LWM2M_SECURITY_MODE_NO_SEC)) {
        fprintf(stderr, "Non-Factory Bootstrap Mode needs DTLS security enabled\n");
        return -1;
    }

    switch (sec_mode) {
    case SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY:
        if (data_ctx.is_bootstrap) {
            public_key_or_id = sol_blob_new_dup(CLIENT_BS_PSK_ID,
                sizeof(CLIENT_BS_PSK_ID) - 1);
            secret_key = sol_blob_new_dup(CLIENT_BS_PSK_KEY, PSK_KEY_LEN);
        } else {
            public_key_or_id = sol_blob_new_dup(CLIENT_SERVER_PSK_ID,
                sizeof(CLIENT_SERVER_PSK_ID) - 1);
            secret_key = sol_blob_new_dup(CLIENT_SERVER_PSK_KEY, PSK_KEY_LEN);
        }
        server_public_key = NULL;
        break;
    case SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY:
        r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
            sol_str_slice_from_str(CLIENT_PRIVATE_KEY), SOL_DECODE_BOTH);
        secret_key = sol_blob_new_dup(buf_aux, RPK_PRIVATE_KEY_LEN);
        r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
            sol_str_slice_from_str(CLIENT_PUBLIC_KEY), SOL_DECODE_BOTH);
        public_key_or_id = sol_blob_new_dup(buf_aux, RPK_PUBLIC_KEY_LEN);

        if (data_ctx.is_bootstrap) {
            r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
                sol_str_slice_from_str(BS_SERVER_PUBLIC_KEY), SOL_DECODE_BOTH);
            server_public_key = sol_blob_new_dup(buf_aux, RPK_PUBLIC_KEY_LEN);
        } else {
            r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
                sol_str_slice_from_str(SERVER_PUBLIC_KEY), SOL_DECODE_BOTH);
            server_public_key = sol_blob_new_dup(buf_aux, RPK_PUBLIC_KEY_LEN);
        }
        break;
    case SOL_LWM2M_SECURITY_MODE_CERTIFICATE:
        fprintf(stderr, "Certificate security mode is not supported yet.\n");
        return -1;
    case SOL_LWM2M_SECURITY_MODE_NO_SEC:
    default:
        public_key_or_id = NULL;
        server_public_key = NULL;
        secret_key = NULL;
        break;
    }

    sol_init();

    client = sol_lwm2m_client_new(cli_name, NULL, NULL, objects, &data_ctx);

    if (!client) {
        r = -1;
        fprintf(stderr, "Could not the create the LWM2M client\n");
        goto exit;
    }

    security_data = calloc(1, sizeof(struct security_obj_instance_ctx));
    if (!security_data) {
        fprintf(stderr, "Could not alloc memory for security object context\n");
        r = -ENOMEM;
        goto exit_del;
    }

    security_data->client = client;
    security_data->security_mode = sec_mode;
    security_data->public_key_or_id = public_key_or_id;
    security_data->server_public_key = server_public_key;
    security_data->secret_key = secret_key;

    if (!data_ctx.is_bootstrap) {
        server_data = calloc(1, sizeof(struct server_obj_instance_ctx));
        if (!server_data) {
            fprintf(stderr, "Could not alloc memory for server object context\n");
            r = -ENOMEM;
            goto exit_del;
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

        security_data->server_uri = (sec_mode == SOL_LWM2M_SECURITY_MODE_NO_SEC) ?
            &server_addr_coap : &server_addr_dtls;
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
        security_data->client_hold_off_time = 0;
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
    free(security_data);
    free(server_data);
exit:
    sol_shutdown();
    return r;
}
