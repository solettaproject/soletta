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
 * Testing Scenario:
 *
 * +----------------+                +---------------+
 * |                |                |               |
 * |            [5693\CoAP]  (4)     |  sec_client   |
 * | sec_server [5684\DTLS]-----[*\DTLS] w/ Access   |
 * |                |        +--[*\DTLS]  Control [*\CoAP]-+
 * +----------------+        |       |               |  (3)|   +--------------+
 *                           |(2)    +---------------+     |   |              |
 *                           |                             |   | nosec_server |
 * +----------------+        |       +----------------+  [5683\CoAP]          |
 * |                |        |       |                |    |   |              |
 * |            [5784\DTLS]--+       |  nosec_client  |    |   +--------------+
 * |  bs_server     |                |   w/o Access   | (1)|
 * |                |                |    Control [*\CoAP]-+
 * +----------------+                |                |
 *                                   +----------------+
 *
 * (1)        1:1 Client-Server communication through CoAP
 * (2)        1:1 Client-BootstrapServer communication through DTLS (w/ PSK)
 * (3)[+(1)]  2:1 Client-Server communication through CoAP
 * (4)[+(3)]  1:2 Client-Server communication through CoAP and DTLS (w/ PSK)
 *
 * (1) nosec_server ('NoSec') <-----------------> nosec_client ('Soletta client test') [Supports Objects /0, /1 and /999]
 * A. 'nosec_server' -------[Create /999]-------> 'Soletta client test'
 * B. 'nosec_server' -------[Read /999/0]-------> 'Soletta client test'
 * C. 'nosec_server' ----[Observe /999/0/2]-----> 'Soletta client test'
 * D. 'nosec_server' -----[Write /999/0/2]------> 'Soletta client test'
 * E. 'nosec_server' ----[Execute /999/0/8]-----> 'Soletta client test'
 * F. 'nosec_server' ---[Unobserve /999/0/2]----> 'Soletta client test'
 * G. 'nosec_server' ------[Delete /999/0]------> 'Soletta client test'
 *
 * (2) bs_server ('RPK-Secured') <--------------> sec_client ('cli1') [Supports Objects /0, /1, /2 and /999]
 * client@client_start:                            Access Control object {Obj:999, Inst: 65535, ACL: {0: 16 (CREATE)} Owner: 65535} created at /2/0
 * A. -----------[Bootstrap Delete /]-----------> 'cli1'
 * client@handle_delete:                           Access Control object {Obj:999, Inst: 65535, ACL: {0: 16 (CREATE)} Owner: 65535} created at /2/0
 * B. ----------[Bootstrap Write /0/0]----------> 'cli1'
 * client@security_object_create:                  Security object created at /0/0
 * C. -----------[Bootstrap Write /1]-----------> 'cli1'
 * client@server_object_create:                    Server object created at /1/0
 * client@handle_create:                           Access Control object {Obj:1, Inst: 0, Owner: 65535} created at /2/1
 * client@server_object_create:                    Server object created at /1/4
 * client@handle_create:                           Access Control object {Obj:1, Inst: 4, Owner: 65535} created at /2/2
 * D. ----------[Bootstrap Write /0/1]----------> 'cli1'
 * client@security_object_create:                  Security object created at /0/1
 * E. ------------[Bootstrap Finish]------------> 'cli1'
 *
 * (3) nosec_server <---------------------------> sec_client ('cli1') [Supports Objects /0, /1, /2 and /999]
 * A. 'nosec_server' -------[Create /999]-------> 'cli1'
 * client@handle_create:                           Access Control object {Obj:999, Inst: 0, Owner: 101} created at /2/3
 * B. 'nosec_server' ------[Write /2/3/2]-------> 'cli1'
 * C. 'nosec_server' -------[Read /999/0]-------> 'cli1'
 * D. 'nosec_server' ----[Observe /999/0/2]-----> 'cli1'
 * client@access_control_object_write_tlv:         ACL: {101: 3 (READ | WRITE)} TLV written to Access Control object at /2/3
 * E. 'nosec_server' -----[Write /999/0/2]------> 'cli1'
 * #F. 'nosec_server' ----[Execute /999/0/8]----> 'cli1'
 * client@handle_execute:                          Server ID 101 is not authorized for E on Object Instance /999/0
 * G. 'nosec_server' ---[Unobserve /999/0/2]----> 'cli1'
 * #H. 'nosec_server' ------[Delete /999/0]-----> 'cli1'
 * client@handle_delete:                           Server ID 101 is not authorized for D on Object Instance /999/0
 *
 * (4) sec_server ('PSK-Secured') <-------------> sec_client ('cli1') [Supports Objects /0, /1, /2 and /999]
 * #A. 'sec_server' --------[Write /999]--------> 'cli1'
 * server@sol_lwm2m_server_write:                  (props < PATH_HAS_INSTANCE) is true
 * #B. 'sec_server' --------[Observe /1]--------> 'cli1'
 * client@handle_resource:                         Server ID 102 is not authorized for Observe [R] on Object Instance /1/4294967295
 * #C. 'sec_server' --------[Read /999]---------> 'cli1'
 * client@handle_read:                             Server ID 102 is not authorized for R on Object Instance /999/0
 * #D. 'sec_server' -------[Write /2/3/2]-------> 'cli1'
 * client@write_instance_tlv_or_resource:          Server ID 102 is not authorized for W on Object Instance /2/3
 * client@handle_write:                            Bootstrap/Management Write on Resource /2/3/2 failed!
 * #E. 'sec_server' ---------[Delete /]---------> 'cli1'
 * server@sol_lwm2m_server_delete_object_instance: (props != PATH_HAS_INSTANCE) is true
 *
 * Operations marked with '#' are negative/corner-cases.
 */

#include <time.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include "test.h"
#include "sol-util.h"
#include "sol-lwm2m.h"
#include "sol-lwm2m-client.h"
#include "sol-lwm2m-server.h"
#include "sol-lwm2m-bs-server.h"
#include "sol-mainloop.h"
#include "sol-coap.h"
#include "sol-util.h"

#define CLIENT_NAME ("Soletta client test")
#define SMS_NUMBER ("+5545646")
#define OBJ_PATH ("my_path")
#define LIFETIME (100)
#define STR ("Str1")
#define OPAQUE_STR ("Opaque")
#define INT_VALUE (-255)
#define FLOAT_VALUE (-2.3)
#define OBJ_VALUE (INT16_MAX)
#define INSTANCE_VALUE (UINT8_MAX)
#define INT_REPLACE_VALUE (-586954)
#define EXECUTE_ARGS ("1='23',2='http://www.soletta.org'")
#define ARRAY_VALUE_ONE (INT64_MAX)
#define ARRAY_VALUE_TWO (INT64_MIN)

#define SECURITY_OBJECT_ID (0)
#define SECURITY_OBJECT_SERVER_URI (0)
#define SECURITY_OBJECT_IS_BOOTSTRAP (1)
#define SECURITY_OBJECT_SECURITY_MODE (2)
#define SECURITY_OBJECT_PUBLIC_KEY_OR_IDENTITY (3)
#define SECURITY_OBJECT_SERVER_PUBLIC_KEY (4)
#define SECURITY_OBJECT_SECRET_KEY (5)
#define SECURITY_OBJECT_SERVER_ID (10)
#define SECURITY_OBJECT_CLIENT_HOLD_OFF_TIME (11)
#define SECURITY_OBJECT_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT (12)

#define SERVER_OBJECT_ID (1)
#define SERVER_OBJECT_SERVER_ID (0)
#define SERVER_OBJECT_LIFETIME (1)
#define SERVER_OBJECT_BINDING (7)

#define ACCESS_CONTROL_OBJECT_ID (2)
#define ACCESS_CONTROL_OBJECT_OBJECT_ID (0)
#define ACCESS_CONTROL_OBJECT_INSTANCE_ID (1)
#define ACCESS_CONTROL_OBJECT_ACL (2)
#define ACCESS_CONTROL_OBJECT_OWNER_ID (3)

#define DUMMY_OBJECT_ID (999)
#define DUMMY_OBJECT_STRING_ID (0)
#define DUMMY_OBJECT_OPAQUE_ID (1)
#define DUMMY_OBJECT_INT_ID (2)
#define DUMMY_OBJECT_BOOLEAN_FALSE_ID (3)
#define DUMMY_OBJECT_BOOLEAN_TRUE_ID (4)
#define DUMMY_OBJECT_FLOAT_ID (5)
#define DUMMY_OBJECT_OBJ_LINK_ID (6)
#define DUMMY_OBJECT_ARRAY_ID (7)
#define DUMMY_OBJECT_EXECUTE_ID (8)

#define PSK_KEY_LEN 16
#define RPK_PRIVATE_KEY_LEN 32
#define RPK_PUBLIC_KEY_LEN (2 * RPK_PRIVATE_KEY_LEN)

#define CLIENT_BS_PSK_ID ("cli1-bs")
#define CLIENT_BS_PSK_KEY ("FEDCBA9876543210")
#define CLIENT_SERVER_PSK_ID ("cli1")
#define CLIENT_SERVER_PSK_KEY ("0123456789ABCDEF")

#define SEC_CLIENT_PRIVATE_KEY ("D9E2707A72DA6A0504995C86EDDBE3EFC7F1CD74838F7570C8072D0A76261BD4")
#define SEC_CLIENT_PUBLIC_KEY ("D055EE14084D6E0615599DB583913E4A3E4526A2704D61F27A4CCFBA9758EF9A" \
    "B418B64AFE8030DA1DDCF4F42E2F2631D043B1FB03E22F4D17DE43F9F9ADEE70")
#define BS_SERVER_PRIVATE_KEY ("9b7dfec20e49fe2cacf23fb21d06a8dc496530c695ec24cdf6c002ce44afa5fb")
#define BS_SERVER_PUBLIC_KEY ("cd4110e97bbd6e7e5a800028079d02915c70b915ea4596402098deea585eb7ad" \
    "f3e080487327f70758b13bc0583f4293d13288a0164a8e324779aa4f7ada26c1")

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

struct dummy_ctx {
    uint16_t id;
    char *str1;
    char *opaque;
    bool f;
    bool t;
    int64_t i;
    double fp;
    uint16_t obj;
    uint16_t instance;
    int64_t array[2];
};

//LWM2M Server ID=101 will be listening @ localhost:5683
// using NoSec mode only
static struct sol_blob nosec_server_coap_addr = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"coap://localhost:5683",
    .size = sizeof("coap://localhost:5683") - 1,
    .refcnt = 1
};

#ifdef DTLS
//LWM2M Server ID=102 will be listening @ localhost:5684
// using PSK security mode,
// with known_psks = { .id: "cli1"; .key: "0123456789ABCDEF" }
static struct sol_blob sec_server_psk_id = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)CLIENT_SERVER_PSK_ID,
    .size = sizeof(CLIENT_SERVER_PSK_ID) - 1,
    .refcnt = 1
};

static struct sol_blob sec_server_psk_key = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)CLIENT_SERVER_PSK_KEY,
    .size = sizeof(CLIENT_SERVER_PSK_KEY) - 1,
    .refcnt = 1
};

const struct sol_lwm2m_security_psk *sec_server_known_keys[] = {
    &((struct sol_lwm2m_security_psk) {
        .id = &sec_server_psk_id,
        .key = &sec_server_psk_key
    }),
    NULL
};

static struct sol_blob sec_server_dtls_addr = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"coaps://localhost:5684",
    .size = sizeof("coaps://localhost:5684") - 1,
    .refcnt = 1
};

//LWM2M Bootstrap Server will be listening @ localhost:5784
// using PSK security mode, with
// known_psks = { .id: "cli1-bs"; .key: "FEDCBA9876543210" }
const char *known_clients[] = { "cli1", NULL };

static struct sol_blob bs_server_psk_id = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)CLIENT_BS_PSK_ID,
    .size = sizeof(CLIENT_BS_PSK_ID) - 1,
    .refcnt = 1
};

static struct sol_blob bs_server_psk_key = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)CLIENT_BS_PSK_KEY,
    .size = sizeof(CLIENT_BS_PSK_KEY) - 1,
    .refcnt = 1
};

const struct sol_lwm2m_security_psk *bs_server_known_keys[] = {
    &((struct sol_lwm2m_security_psk) {
        .id = &bs_server_psk_id,
        .key = &bs_server_psk_key
    }),
    NULL
};

static struct sol_blob bs_server_addr = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"coaps://localhost:5784",
    .size = sizeof("coaps://localhost:5784") - 1,
    .refcnt = 1
};
#endif

static struct sol_blob binding = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"U",
    .size = sizeof("U") - 1,
    .refcnt = 1
};

// ============================================================== Security Object
static int
security_object_read(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, struct sol_lwm2m_resource *res)
{
    struct security_obj_instance_ctx *ctx = instance_data;
    int r;

    switch (res_id) {
    case SECURITY_OBJECT_SERVER_URI:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, ctx->server_uri);
        ASSERT(r == 0);
        break;
    case SECURITY_OBJECT_IS_BOOTSTRAP:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, ctx->is_bootstrap);
        ASSERT(r == 0);
        break;
    case SECURITY_OBJECT_SECURITY_MODE:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->security_mode);
        ASSERT(r == 0);
        break;
    case SECURITY_OBJECT_PUBLIC_KEY_OR_IDENTITY:
        if (!ctx->public_key_or_id)
            r = -ENOENT;
        else {
            SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
                SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, ctx->public_key_or_id);
            ASSERT(r == 0);
        }
        break;
    case SECURITY_OBJECT_SERVER_PUBLIC_KEY:
        if (!ctx->server_public_key)
            r = -ENOENT;
        else {
            SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
                SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, ctx->server_public_key);
            ASSERT(r == 0);
        }
        break;
    case SECURITY_OBJECT_SECRET_KEY:
        if (!ctx->secret_key)
            r = -ENOENT;
        else {
            SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
                SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, ctx->secret_key);
            ASSERT(r == 0);
        }
        break;
    case SECURITY_OBJECT_SERVER_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->server_id);
        ASSERT(r == 0);
        break;
    case SECURITY_OBJECT_CLIENT_HOLD_OFF_TIME:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->client_hold_off_time);
        ASSERT(r == 0);
        break;
    case SECURITY_OBJECT_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->bootstrap_server_account_timeout);
        ASSERT(r == 0);
        break;
    default:
        if (res_id >= 6 && res_id <= 9)
            r = -ENOENT;
        else
            ASSERT(1 == 2); //MUST NOT HAPPEN!
    }

    return r;
}

static int
security_object_write_res(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, const struct sol_lwm2m_resource *res)
{
    return 0;
}

static int
security_object_write_tlv(void *instance_data, void *user_data,
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
        case SECURITY_OBJECT_SERVER_URI:
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            ASSERT(r == 0);
            sol_blob_unref(instance_ctx->server_uri);
            instance_ctx->server_uri = sol_buffer_to_blob(&buf);
            ASSERT(instance_ctx->server_uri != NULL);
            break;
        case SECURITY_OBJECT_IS_BOOTSTRAP:
            r = sol_lwm2m_tlv_get_bool(tlv, &instance_ctx->is_bootstrap);
            ASSERT(r == 0);
            break;
        case SECURITY_OBJECT_SECURITY_MODE:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->security_mode);
            ASSERT(r == 0);
            break;
        case SECURITY_OBJECT_PUBLIC_KEY_OR_IDENTITY:
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            ASSERT(r == 0);
            sol_blob_unref(instance_ctx->public_key_or_id);
            instance_ctx->public_key_or_id = sol_buffer_to_blob(&buf);
            ASSERT(instance_ctx->public_key_or_id != NULL);
            break;
        case SECURITY_OBJECT_SERVER_PUBLIC_KEY:
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            ASSERT(r == 0);
            sol_blob_unref(instance_ctx->server_public_key);
            instance_ctx->server_public_key = sol_buffer_to_blob(&buf);
            ASSERT(instance_ctx->server_public_key != NULL);
            break;
        case SECURITY_OBJECT_SECRET_KEY:
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            ASSERT(r == 0);
            sol_blob_unref(instance_ctx->secret_key);
            instance_ctx->secret_key = sol_buffer_to_blob(&buf);
            ASSERT(instance_ctx->secret_key != NULL);
            break;
        case SECURITY_OBJECT_SERVER_ID:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->server_id);
            ASSERT(r == 0);
            break;
        case SECURITY_OBJECT_CLIENT_HOLD_OFF_TIME:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->client_hold_off_time);
            ASSERT(r == 0);
            break;
        case SECURITY_OBJECT_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->bootstrap_server_account_timeout);
            ASSERT(r == 0);
            break;
        default:
            ASSERT(1 == 2); //MUST NOT HAPPEN!
        }
    }

    if (tlvs->len == 1 && r >= 0)
        printf("DBG: TLV written to Security object at /1/%" PRIu16 "/%" PRIu16 "\n",
            instance_id, tlv->id);
    else
        printf("DBG: TLV written to Security object at /1/%" PRIu16 "\n", instance_id);

    return r;
}

static int
security_object_create(void *user_data, struct sol_lwm2m_client *client,
    uint16_t instance_id, void **instance_data,
    struct sol_lwm2m_payload payload)
{
    struct security_obj_instance_ctx *instance_ctx;
    int r;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;

    ASSERT(payload.type == SOL_LWM2M_CONTENT_TYPE_TLV);

    instance_ctx = calloc(1, sizeof(struct security_obj_instance_ctx));
    ASSERT(instance_ctx != NULL);

    SOL_VECTOR_FOREACH_IDX (&payload.payload.tlv_content, tlv, i) {
        SOL_BUFFER_DECLARE_STATIC(buf, 64);

        if (tlv->id == SECURITY_OBJECT_SERVER_URI) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            ASSERT(r == 0);
            instance_ctx->server_uri = sol_buffer_to_blob(&buf);
            ASSERT(instance_ctx->server_uri != NULL);
        } else if (tlv->id == SECURITY_OBJECT_IS_BOOTSTRAP) {
            r = sol_lwm2m_tlv_get_bool(tlv, &instance_ctx->is_bootstrap);
            ASSERT(r == 0);
        } else if (tlv->id == SECURITY_OBJECT_SECURITY_MODE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->security_mode);
            ASSERT(r == 0);
        } else if (tlv->id == SECURITY_OBJECT_PUBLIC_KEY_OR_IDENTITY) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            ASSERT(r == 0);
            instance_ctx->public_key_or_id = sol_buffer_to_blob(&buf);
            ASSERT(instance_ctx->public_key_or_id != NULL);
        } else if (tlv->id == SECURITY_OBJECT_SERVER_PUBLIC_KEY) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            ASSERT(r == 0);
            instance_ctx->server_public_key = sol_buffer_to_blob(&buf);
            ASSERT(instance_ctx->server_public_key != NULL);
        } else if (tlv->id == SECURITY_OBJECT_SECRET_KEY) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            ASSERT(r == 0);
            instance_ctx->secret_key = sol_buffer_to_blob(&buf);
            ASSERT(instance_ctx->secret_key != NULL);
        } else if (tlv->id == SECURITY_OBJECT_SERVER_ID) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->server_id);
            ASSERT(r == 0);
        } else if (tlv->id == SECURITY_OBJECT_CLIENT_HOLD_OFF_TIME) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->client_hold_off_time);
            ASSERT(r == 0);
        } else if (tlv->id == SECURITY_OBJECT_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->bootstrap_server_account_timeout);
            ASSERT(r == 0);
        } else
            ASSERT(1 == 2); //MUST NOT HAPPEN!
    }

    instance_ctx->client = client;
    *instance_data = instance_ctx;
    printf("DBG: Security object created at /0/%" PRIu16 "\n", instance_id);

    return 0;
}

static int
security_object_delete(void *instance_data, void *user_data,
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

// ================================================================ Server Object
static int
server_object_read(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, struct sol_lwm2m_resource *res)
{
    struct server_obj_instance_ctx *ctx = instance_data;
    int r;

    switch (res_id) {
    case SERVER_OBJECT_SERVER_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->server_id);
        ASSERT(r == 0);
        break;
    case SERVER_OBJECT_LIFETIME:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->lifetime);
        ASSERT(r == 0);
        break;
    case SERVER_OBJECT_BINDING:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, ctx->binding);
        ASSERT(r == 0);
        break;
    default:
        if (res_id >= 2 && res_id <= 6)
            r = -ENOENT;
        else
            ASSERT(1 == 2); //MUST NOT HAPPEN!
    }

    return r;
}

static int
server_object_write_res(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, const struct sol_lwm2m_resource *res)
{
    return 0;
}

static int
server_object_write_tlv(void *instance_data, void *user_data,
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
        case SERVER_OBJECT_SERVER_ID:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->server_id);
            ASSERT(r == 0);
            break;
        case SERVER_OBJECT_LIFETIME:
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->lifetime);
            ASSERT(r == 0);
            break;
        case SERVER_OBJECT_BINDING:
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            ASSERT(r == 0);
            sol_blob_unref(instance_ctx->binding);
            instance_ctx->binding = sol_buffer_to_blob(&buf);
            ASSERT(instance_ctx->binding != NULL);
            break;
        default:
            ASSERT(1 == 2); //MUST NOT HAPPEN!
        }
    }

    if (tlvs->len == 1 && r >= 0)
        printf("DBG: TLV written to Server object at /1/%" PRIu16 "/%" PRIu16 "\n",
            instance_id, tlv->id);
    else
        printf("DBG: TLV written to Server object at /1/%" PRIu16 "\n", instance_id);

    return r;
}

static int
server_object_create(void *user_data, struct sol_lwm2m_client *client,
    uint16_t instance_id, void **instance_data,
    struct sol_lwm2m_payload payload)
{
    struct server_obj_instance_ctx *instance_ctx;
    int r;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;

    ASSERT(payload.type == SOL_LWM2M_CONTENT_TYPE_TLV);

    instance_ctx = calloc(1, sizeof(struct server_obj_instance_ctx));
    ASSERT(instance_ctx != NULL);

    SOL_VECTOR_FOREACH_IDX (&payload.payload.tlv_content, tlv, i) {
        SOL_BUFFER_DECLARE_STATIC(buf, 64);

        if (tlv->id == SERVER_OBJECT_SERVER_ID) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->server_id);
            ASSERT(r == 0);
        } else if (tlv->id == SERVER_OBJECT_LIFETIME) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->lifetime);
            ASSERT(r == 0);
        } else if (tlv->id == SERVER_OBJECT_BINDING) {
            r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
            ASSERT(r == 0);
            instance_ctx->binding = sol_buffer_to_blob(&buf);
            ASSERT(instance_ctx->binding != NULL);
        } else
            ASSERT(1 == 2); //MUST NOT HAPPEN!
    }

    instance_ctx->client = client;
    *instance_data = instance_ctx;
    printf("DBG: Server object created at /1/%" PRIu16 "\n", instance_id);

    return 0;
}

static int
server_object_delete(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id)
{
    struct server_obj_instance_ctx *instance_ctx = instance_data;

    sol_blob_unref(instance_ctx->binding);
    free(instance_ctx);
    return 0;
}

#ifdef DTLS
// ======================================================== Access Control Object
static int
access_control_object_read(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, struct sol_lwm2m_resource *res)
{
    struct access_control_obj_instance_ctx *ctx = instance_data;
    int r;

    if (res_id == ACCESS_CONTROL_OBJECT_OBJECT_ID) {
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->object_id);
        ASSERT(r == 0);
    } else if (res_id == ACCESS_CONTROL_OBJECT_INSTANCE_ID) {
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->instance_id);
        ASSERT(r == 0);
    } else if (res_id == ACCESS_CONTROL_OBJECT_ACL) {
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
        r = sol_lwm2m_resource_init_vector(res, ACCESS_CONTROL_OBJECT_ACL,
            SOL_LWM2M_RESOURCE_DATA_TYPE_INT, &acl_instances);
        ASSERT(r == 0);

        sol_vector_clear(&acl_instances);
    } else if (res_id == ACCESS_CONTROL_OBJECT_OWNER_ID) {
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->owner_id);
        ASSERT(r == 0);
    } else {
        ASSERT(1 == 2); //MUST NOT HAPPEN!
    }

    return r;
}

static int
access_control_object_write_res(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, const struct sol_lwm2m_resource *res)
{
    return 0;
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
        ASSERT(r == 0);

        acl_item = sol_vector_append(acl);
        ASSERT(acl_item != NULL);

        acl_item->key = res_tlv->id;
        acl_item->value = res_val;
        if (is_create)
            printf("DBG: <<[CREATE]<< acl[%" PRIu16 "]=%" PRId64
                " >>>> | ", acl_item->key, acl_item->value);
        else
            printf("DBG: <<[WRITE_TLV]<< acl[%" PRIu16 "]=%" PRId64
                " >>>> | ", acl_item->key, acl_item->value);
        (*j)++;
    }

    return 0;
}

static int
access_control_object_write_tlv(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, struct sol_vector *tlvs)
{
    int r = -EINVAL;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;
    struct access_control_obj_instance_ctx *instance_ctx = instance_data;

    SOL_VECTOR_FOREACH_IDX (tlvs, tlv, i) {
        if (tlv->id == ACCESS_CONTROL_OBJECT_OBJECT_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->object_id);
            ASSERT(r == 0);
        } else if (tlv->id == ACCESS_CONTROL_OBJECT_INSTANCE_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->instance_id);
            ASSERT(r == 0);
        } else if (tlv->id == ACCESS_CONTROL_OBJECT_ACL &&
            tlv->type == SOL_LWM2M_TLV_TYPE_MULTIPLE_RESOURCES) {
            uint16_t j = i + 1;

            sol_vector_clear(&instance_ctx->acl);

            r = write_or_create_acl(&instance_ctx->acl, tlvs, &j, false);
            ASSERT(r == 0);

            i = j - 1;
        } else if (tlv->id == ACCESS_CONTROL_OBJECT_OWNER_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->owner_id);
            ASSERT(r == 0);
        } else {
            ASSERT(1 == 2); //MUST NOT HAPPEN!
        }
    }

    if (tlvs->len == 1 && r >= 0)
        printf("DBG: TLV written to Access Control object at /2/%" PRIu16 "/%" PRIu16 "\n",
            instance_id, tlv->id);
    else
        printf("DBG: TLV written to Access Control object at /2/%" PRIu16 "\n", instance_id);

    return r;
}

static int
access_control_object_create(void *user_data, struct sol_lwm2m_client *client,
    uint16_t instance_id, void **instance_data,
    struct sol_lwm2m_payload payload)
{
    struct access_control_obj_instance_ctx *instance_ctx;
    int r;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;

    ASSERT(payload.type == SOL_LWM2M_CONTENT_TYPE_TLV);

    instance_ctx = calloc(1, sizeof(struct access_control_obj_instance_ctx));
    ASSERT(instance_ctx != NULL);

    sol_vector_init(&instance_ctx->acl, sizeof(struct acl_instance));

    SOL_VECTOR_FOREACH_IDX (&payload.payload.tlv_content, tlv, i) {
        if (tlv->id == ACCESS_CONTROL_OBJECT_OBJECT_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->object_id);
            ASSERT(r == 0);
        } else if (tlv->id == ACCESS_CONTROL_OBJECT_INSTANCE_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->instance_id);
            ASSERT(r == 0);
        } else if (tlv->id == ACCESS_CONTROL_OBJECT_ACL &&
            tlv->type == SOL_LWM2M_TLV_TYPE_MULTIPLE_RESOURCES) {
            uint16_t j = i + 1;

            sol_vector_clear(&instance_ctx->acl);

            r = write_or_create_acl(&instance_ctx->acl, &payload.payload.tlv_content, &j, true);
            ASSERT(r == 0);

            i = j - 1;
        } else if (tlv->id == ACCESS_CONTROL_OBJECT_OWNER_ID &&
            tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            r = sol_lwm2m_tlv_get_int(tlv, &instance_ctx->owner_id);
            ASSERT(r == 0);
        } else
            ASSERT(1 == 2); //MUST NOT HAPPEN!
    }

    instance_ctx->client = client;
    *instance_data = instance_ctx;
    printf("DBG: Access Control object {Obj:%" PRId64 ", Inst: %"
        PRId64 ", Owner: %" PRId64 "} created at /2/%" PRIu16 "\n",
        instance_ctx->object_id, instance_ctx->instance_id,
        instance_ctx->owner_id, instance_id);

    return 0;
}

static int
access_control_object_delete(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id)
{
    struct access_control_obj_instance_ctx *instance_ctx = instance_data;

    sol_vector_clear(&instance_ctx->acl);
    free(instance_ctx);
    return 0;
}
#endif

// ================================================================= Dummy Object
/*
   This function is used by the lwm2m client and server to check if the tlv
   and its values are valid. However the lwm2m server will pass NULL as
   the second argument.
 */
static void
check_tlv_and_save(struct sol_vector *tlvs, struct dummy_ctx *ctx, bool *first)
{
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;
    int r;

    SOL_VECTOR_FOREACH_IDX (tlvs, tlv, i) {
        SOL_BUFFER_DECLARE_STATIC(buf, 32); // watch sizes of STR and OPAQUE_STR
        uint16_t obj, instance;
        bool b;
        int64_t int64;
        double fp;

        if (tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            switch (tlv->id) {
            case DUMMY_OBJECT_STRING_ID:
                r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
                ASSERT(r == 0);
                ASSERT(buf.used == strlen(STR));
                ASSERT(!memcmp(STR, buf.data, buf.used));
                if (ctx) {
                    ctx->str1 = strndup((const char *)buf.data, buf.used);
                    ASSERT(ctx->str1 != NULL);
                }
                break;
            case DUMMY_OBJECT_OPAQUE_ID:
                r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
                ASSERT(r == 0);
                ASSERT(buf.used == strlen(OPAQUE_STR));
                ASSERT(!memcmp(OPAQUE_STR, buf.data, buf.used));
                if (ctx) {
                    ctx->opaque = strndup((const char *)buf.data, buf.used);
                    ASSERT(ctx->opaque != NULL);
                }
                break;
            case DUMMY_OBJECT_INT_ID:
                r = sol_lwm2m_tlv_get_int(tlv, &int64);
                ASSERT(r == 0);
                if (*first || !ctx)
                    ASSERT(int64 == INT_VALUE);
                else
                    ASSERT(int64 == INT_REPLACE_VALUE);
                if (ctx)
                    ctx->i = int64;
                break;
            case DUMMY_OBJECT_BOOLEAN_FALSE_ID:
                r = sol_lwm2m_tlv_get_bool(tlv, &b);
                ASSERT(r == 0);
                ASSERT(!b);
                if (ctx)
                    ctx->f = b;
                break;
            case DUMMY_OBJECT_BOOLEAN_TRUE_ID:
                r = sol_lwm2m_tlv_get_bool(tlv, &b);
                ASSERT(r == 0);
                ASSERT(b);
                if (ctx)
                    ctx->t = b;
                break;
            case DUMMY_OBJECT_FLOAT_ID:
                r = sol_lwm2m_tlv_get_float(tlv, &fp);
                ASSERT(r == 0);
                ASSERT(fp - FLOAT_VALUE <= 0.00);
                if (ctx)
                    ctx->fp = fp;
                break;
            case DUMMY_OBJECT_OBJ_LINK_ID:
                r = sol_lwm2m_tlv_get_obj_link(tlv, &obj, &instance);
                ASSERT(r == 0);
                ASSERT(obj == OBJ_VALUE);
                ASSERT(instance == INSTANCE_VALUE);
                if (ctx) {
                    ctx->obj = obj;
                    ctx->instance = instance;
                }
                break;
            default:
                ASSERT(1 == 2); //This must never happen!
            }
        } else if (tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_INSTANCE) {
            //This must be an array with two elements, so the ids must be 0 and 1
            if (tlv->id != 0 && tlv->id != 1)
                ASSERT(1 == 2);
            r = sol_lwm2m_tlv_get_int(tlv, &int64);
            ASSERT(r == 0);
            if (tlv->id == 0)
                ASSERT(int64 == ARRAY_VALUE_ONE);
            else
                ASSERT(int64 == ARRAY_VALUE_TWO);
            if (ctx)
                ctx->array[tlv->id] = int64;
        }
        sol_buffer_fini(&buf);
    }
    *first = false;
}

static int
create_dummy(void *user_data, struct sol_lwm2m_client *client,
    uint16_t instance_id, void **instance_data,
    struct sol_lwm2m_payload payload)
{
    struct dummy_ctx *ctx = calloc(1, sizeof(struct dummy_ctx));
    bool *first = user_data;

    ASSERT(ctx);
    *instance_data = ctx;
    ctx->id = instance_id;

    ASSERT(payload.type == SOL_LWM2M_CONTENT_TYPE_TLV);
    check_tlv_and_save(&payload.payload.tlv_content, ctx, first);
    return 0;
}

static int
write_dummy_tlv(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id,
    struct sol_vector *tlvs)
{
    struct dummy_ctx *ctx = instance_data;
    bool *first = user_data;

    check_tlv_and_save(tlvs, ctx, first);
    return 0;
}

static int
write_dummy_resource(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id, uint16_t res_id,
    const struct sol_lwm2m_resource *res)
{
    return 0;
}

static int
read_dummy_resource(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id,
    uint16_t res_id, struct sol_lwm2m_resource *res)
{
    struct dummy_ctx *ctx = instance_data;
    struct sol_blob *blob;
    int r;

    switch (res_id) {
    case DUMMY_OBJECT_STRING_ID:
        blob = sol_blob_new(&SOL_BLOB_TYPE_NO_FREE_DATA, NULL,
            ctx->str1, strlen(ctx->str1));
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, blob);
        sol_blob_unref(blob);
        break;
    case DUMMY_OBJECT_OPAQUE_ID:
        blob = sol_blob_new(&SOL_BLOB_TYPE_NO_FREE_DATA, NULL,
            ctx->opaque, strlen(ctx->opaque));
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE, blob);
        sol_blob_unref(blob);
        break;
    case DUMMY_OBJECT_INT_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, res, res_id, ctx->i);
        break;
    case DUMMY_OBJECT_BOOLEAN_FALSE_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, ctx->f);
        break;
    case DUMMY_OBJECT_BOOLEAN_TRUE_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, ctx->t);
        break;
    case DUMMY_OBJECT_FLOAT_ID:
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, res, res_id,
            SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT, ctx->fp);
        break;
    case DUMMY_OBJECT_OBJ_LINK_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, res_id, SOL_LWM2M_RESOURCE_TYPE_SINGLE, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK, ctx->obj, ctx->instance);
        break;
    case DUMMY_OBJECT_ARRAY_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, res_id, SOL_LWM2M_RESOURCE_TYPE_MULTIPLE, 2,
            SOL_LWM2M_RESOURCE_DATA_TYPE_INT, 0, ctx->array[0], 1, ctx->array[1]);
        break;
    default:
        r = -EINVAL;
    }

    return r;
}

static int
execute_dummy(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id,
    uint16_t res_id, const struct sol_str_slice args)
{
    int r;

    ASSERT(res_id == DUMMY_OBJECT_EXECUTE_ID);
    ASSERT(sol_str_slice_str_eq(args, EXECUTE_ARGS));

    r = sol_lwm2m_client_send_update(client);
    ASSERT(r == 0);
    return 0;
}

static int
del_dummy(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id)
{
    struct dummy_ctx *ctx = instance_data;

    free(ctx->str1);
    free(ctx->opaque);
    free(ctx);
    return 0;
}

static const struct sol_lwm2m_object security_object = {
    SOL_SET_API_VERSION(.api_version = SOL_LWM2M_OBJECT_API_VERSION, )
    .id = SECURITY_OBJECT_ID,
    .resources_count = 12,
    .read = security_object_read,
    .write_resource = security_object_write_res,
    .write_tlv = security_object_write_tlv,
    .create = security_object_create,
    .del = security_object_delete
};

static const struct sol_lwm2m_object server_object = {
    SOL_SET_API_VERSION(.api_version = SOL_LWM2M_OBJECT_API_VERSION, )
    .id = SERVER_OBJECT_ID,
    .resources_count = 9,
    .read = server_object_read,
    .write_resource = server_object_write_res,
    .write_tlv = server_object_write_tlv,
    .create = server_object_create,
    .del = server_object_delete
};

#ifdef DTLS
static const struct sol_lwm2m_object access_control_object = {
    SOL_SET_API_VERSION(.api_version = SOL_LWM2M_OBJECT_API_VERSION, )
    .id = ACCESS_CONTROL_OBJECT_ID,
    .resources_count = 4,
    .read = access_control_object_read,
    .write_resource = access_control_object_write_res,
    .write_tlv = access_control_object_write_tlv,
    .create = access_control_object_create,
    .del = access_control_object_delete
};
#endif

//This is a dummy object, it's not defined by OMA!
static const struct sol_lwm2m_object dummy_object = {
    SOL_SET_API_VERSION(.api_version = SOL_LWM2M_OBJECT_API_VERSION, )
    .id = DUMMY_OBJECT_ID,
    .resources_count = 9,
    .create = create_dummy,
    .read = read_dummy_resource,
    .write_resource = write_dummy_resource,
    .write_tlv = write_dummy_tlv,
    .del = del_dummy,
    .execute = execute_dummy
};

static void
observe_res_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content);

static void
delete_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code);

static void
execute_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code)
{
    int r;
    char *server_type = data;

    if (!strcmp("cli1", sol_lwm2m_client_info_get_name(client))) {
        ASSERT(response_code == SOL_COAP_RESPONSE_CODE_UNAUTHORIZED);

        printf("DBG: '%s' ---[Unobserve /999/0/2]---> '%s'\n",
            server_type, sol_lwm2m_client_info_get_name(client));
        r = sol_lwm2m_server_del_observer(server, client, "/999/0/2",
            observe_res_cb, data);
        ASSERT(r == 0);

        printf("DBG: '%s' ---[Delete /999/0]---> '%s'\n",
            server_type, sol_lwm2m_client_info_get_name(client));
        r = sol_lwm2m_server_delete_object_instance(server, client, "/999/0",
            delete_cb, data);
        ASSERT(r == 0);
    } else
        ASSERT(response_code == SOL_COAP_RESPONSE_CODE_CHANGED);
}

static void
write_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code)
{
    int r;
    char *server_type = data;

    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_CHANGED);

    printf("DBG: '%s' ---[Execute /999/0/8]---> '%s'\n",
        server_type, sol_lwm2m_client_info_get_name(client));
    r = sol_lwm2m_server_execute_resource(server, client, "/999/0/8",
        EXECUTE_ARGS, execute_cb, data);
    ASSERT(r == 0);
}

static void
read_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content)
{
    struct sol_vector tlvs;
    int r;
    struct sol_lwm2m_resource res;
    static bool first = true;
    char *server_type = data;

    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_CONTENT);
    ASSERT(content_type == SOL_LWM2M_CONTENT_TYPE_TLV);

    r = sol_lwm2m_parse_tlv(content, &tlvs);
    ASSERT(r == 0);

    check_tlv_and_save(&tlvs, NULL, &first);

    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &res, DUMMY_OBJECT_INT_ID, INT_REPLACE_VALUE);
    ASSERT(r == 0);
    printf("DBG: '%s' ---[Write /999/0/2]---> '%s'\n",
        server_type, sol_lwm2m_client_info_get_name(client));
    r = sol_lwm2m_server_write(server, client, "/999/0/2",
        &res, 1, write_cb, data);
    ASSERT(r == 0);
    sol_lwm2m_resource_clear(&res);
    sol_lwm2m_tlv_list_clear(&tlvs);
}

static void
observe_res_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content)
{
    static enum {
        INT_VALUE_IS_SET = (1 << 0),
        INT_VALUE_REPLACED = (1 << 1)
    } nosec_state = INT_VALUE_IS_SET, sec_state = INT_VALUE_IS_SET;
    struct sol_vector tlvs;
    struct sol_lwm2m_tlv *tlv;
    int64_t v;
    int r;

    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_CHANGED ||
        response_code == SOL_COAP_RESPONSE_CODE_CONTENT);

    r = sol_lwm2m_parse_tlv(content, &tlvs);
    ASSERT(r == 0);
    ASSERT(tlvs.len == 1);
    tlv = sol_vector_get_no_check(&tlvs, 0);
    r = sol_lwm2m_tlv_get_int(tlv, &v);
    ASSERT(r == 0);

    if (!strcmp(CLIENT_NAME, sol_lwm2m_client_info_get_name(client))) {
        if (nosec_state == INT_VALUE_IS_SET)
            ASSERT(v == INT_VALUE);
        else if (nosec_state == INT_VALUE_REPLACED)
            ASSERT(v == INT_REPLACE_VALUE);
        else
            ASSERT(1 == 2); //MUST NOT HAPPEN!

        nosec_state = nosec_state << 1;
    } else {
        if (sec_state == INT_VALUE_IS_SET)
            ASSERT(v == INT_VALUE);
        else if (sec_state == INT_VALUE_REPLACED)
            ASSERT(v == INT_REPLACE_VALUE);
        else
            ASSERT(1 == 2); //MUST NOT HAPPEN!

        sec_state = sec_state << 1;
    }

    sol_lwm2m_tlv_clear(tlv);
    sol_vector_clear(&tlvs);
}

#ifdef DTLS
static void
write_acl_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code)
{
    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_CHANGED);
}
#endif

static void
create_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code)
{
    int r;
    char *server_type = data;

#ifdef DTLS
    struct sol_lwm2m_resource res;
#endif

    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_CREATED);

#ifdef DTLS
    if (!strcmp("cli1", sol_lwm2m_client_info_get_name(client))) {
        SOL_LWM2M_RESOURCE_INIT(r, &res, ACCESS_CONTROL_OBJECT_ACL,
            SOL_LWM2M_RESOURCE_TYPE_MULTIPLE, 1, SOL_LWM2M_RESOURCE_DATA_TYPE_INT,
            101, SOL_TYPE_CHECK(int64_t, SOL_LWM2M_ACL_READ | SOL_LWM2M_ACL_WRITE));
        ASSERT(r == 0);
        printf("DBG: '%s' ---[Write /2/3/2]---> '%s'\n",
            server_type, sol_lwm2m_client_info_get_name(client));
        r = sol_lwm2m_server_write(server, client, "/2/3/2",
            &res, 1, write_acl_cb, data);
        ASSERT(r == 0);
        sol_lwm2m_resource_clear(&res);
    }
#endif

    printf("DBG: '%s' ---[Read /999/0]---> '%s'\n",
        server_type, sol_lwm2m_client_info_get_name(client));
    r = sol_lwm2m_server_read(server, client, "/999/0",
        read_cb, data);
    ASSERT(r == 0);

    printf("DBG: '%s' ---[Observe /999/0/2]---> '%s'\n",
        server_type, sol_lwm2m_client_info_get_name(client));
    r = sol_lwm2m_server_add_observer(server, client, "/999/0/2",
        observe_res_cb, data);
    ASSERT(r == 0);
}

static void
create_obj(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo, void *data)
{
    int r;
    size_t i;
    struct sol_lwm2m_resource res[8];
    struct sol_blob *blob;
    char *server_type = data;

    blob = sol_blob_new(&SOL_BLOB_TYPE_NO_FREE_DATA, NULL,
        STR, strlen(STR));

    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &res[0], DUMMY_OBJECT_STRING_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, blob);
    sol_blob_unref(blob);
    ASSERT(r == 0);

    blob = sol_blob_new(&SOL_BLOB_TYPE_NO_FREE_DATA, NULL,
        OPAQUE_STR, strlen(OPAQUE_STR));
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &res[1], DUMMY_OBJECT_OPAQUE_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE, blob);
    sol_blob_unref(blob);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &res[2], DUMMY_OBJECT_INT_ID, INT_VALUE);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &res[3], DUMMY_OBJECT_BOOLEAN_FALSE_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, false);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &res[4], DUMMY_OBJECT_BOOLEAN_TRUE_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, true);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &res[5], DUMMY_OBJECT_FLOAT_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT, FLOAT_VALUE);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_INIT(r, &res[6], DUMMY_OBJECT_OBJ_LINK_ID,
        SOL_LWM2M_RESOURCE_TYPE_SINGLE, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK, OBJ_VALUE, INSTANCE_VALUE);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_INIT(r, &res[7], DUMMY_OBJECT_ARRAY_ID,
        SOL_LWM2M_RESOURCE_TYPE_MULTIPLE, 2,
        SOL_LWM2M_RESOURCE_DATA_TYPE_INT, 0, ARRAY_VALUE_ONE, 1, ARRAY_VALUE_TWO);
    ASSERT(r == 0);
    printf("DBG: '%s' ---[Create /999]---> '%s'\n",
        server_type, sol_lwm2m_client_info_get_name(cinfo));
    r = sol_lwm2m_server_create_object_instance(server, cinfo, "/999", res,
        sol_util_array_size(res), create_cb, data);
    ASSERT(r == 0);

    for (i = 0; i < sol_util_array_size(res); i++)
        sol_lwm2m_resource_clear(&res[i]);
}

static void
delete_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code)
{
    static int finished_connections = 0;
    char *server_type = data;

    if (!strcmp("cli1", sol_lwm2m_client_info_get_name(client)))
        ASSERT(response_code == SOL_COAP_RESPONSE_CODE_UNAUTHORIZED);
    else
        ASSERT(response_code == SOL_COAP_RESPONSE_CODE_DELETED);

    finished_connections++;

    printf("DBG: ======== [%d] Client '%s' finished with '%s' server\n",
        finished_connections, sol_lwm2m_client_info_get_name(client),
        server_type);

#ifdef DTLS
    if (finished_connections == 3)
        sol_quit();
#else
    if (finished_connections == 1)
        sol_quit();
#endif
}

static void
check_cinfo(struct sol_lwm2m_client_info *cinfo, const char *name,
    const char *sms_number, const char *objects_path,
    enum sol_lwm2m_binding_mode binding_mode, bool access_control)
{
    int r;
    uint32_t lf;
    const struct sol_ptr_vector *objects;
    struct sol_lwm2m_client_object *object;
    uint16_t i, objects_found = 0;

    ASSERT(!strcmp(name, sol_lwm2m_client_info_get_name(cinfo)));
    if (sms_number)
        ASSERT(!strcmp(SMS_NUMBER, sol_lwm2m_client_info_get_sms_number(cinfo)));
    if (objects_path)
        ASSERT(!strcmp("/my_path",
            sol_lwm2m_client_info_get_objects_path(cinfo)));
    r = sol_lwm2m_client_info_get_lifetime(cinfo, &lf);
    ASSERT(r == 0);
    ASSERT(lf == LIFETIME);
    ASSERT(sol_lwm2m_client_info_get_binding_mode(cinfo) ==
        SOL_LWM2M_BINDING_MODE_U);

    objects = sol_lwm2m_client_info_get_objects(cinfo);

    SOL_PTR_VECTOR_FOREACH_IDX (objects, object, i) {
        uint16_t obj_id;
        r = sol_lwm2m_client_object_get_id(object, &obj_id);
        ASSERT(r == 0);
        if (obj_id == SECURITY_OBJECT_ID || obj_id == ACCESS_CONTROL_OBJECT_ID ||
            obj_id == SERVER_OBJECT_ID || obj_id == DUMMY_OBJECT_ID)
            objects_found++;
    }

    ASSERT(objects_found == (access_control ? 4 : 3));
}

static void
nosec_registration_event_cb(void *data, struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event)
{
    int r;
    char *server_type = data;

    if (event == SOL_LWM2M_REGISTRATION_EVENT_REGISTER &&
        !strcmp("cli1", sol_lwm2m_client_info_get_name(cinfo))) {
        check_cinfo(cinfo, "cli1", NULL, NULL, SOL_LWM2M_BINDING_MODE_U, true);

        create_obj(server, cinfo, data);

    } else if (event == SOL_LWM2M_REGISTRATION_EVENT_REGISTER) {
        check_cinfo(cinfo, CLIENT_NAME, SMS_NUMBER, "/my_path", SOL_LWM2M_BINDING_MODE_U, false);

        create_obj(server, cinfo, data);

    } else if (event == SOL_LWM2M_REGISTRATION_EVENT_UPDATE) {
        printf("DBG: '%s' ---[Unobserve /999/0/2]---> '%s'\n",
            server_type, sol_lwm2m_client_info_get_name(cinfo));
        r = sol_lwm2m_server_del_observer(server, cinfo, "/999/0/2",
            observe_res_cb, data);
        ASSERT(r == 0);

        printf("DBG: '%s' ---[Delete /999/0]---> %s\n",
            server_type, sol_lwm2m_client_info_get_name(cinfo));
        r = sol_lwm2m_server_delete_object_instance(server, cinfo, "/999/0",
            delete_cb, data);
        ASSERT(r == 0);
    } else {
        ASSERT(1 == 2); //TIMEOUT/UNREGISTER, this must not happen!
    }
}

#ifdef DTLS
static void
write_acl_unauthorized_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code)
{
    int r;
    char *server_type = data;

    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_UNAUTHORIZED);

    printf("DBG: '%s' ---[Delete /]---> %s\n",
        server_type, sol_lwm2m_client_info_get_name(client));
    r = sol_lwm2m_server_delete_object_instance(server, client, "/",
        delete_cb, data);
    ASSERT(r == -EINVAL);

    delete_cb(data, server, client, NULL, SOL_COAP_RESPONSE_CODE_UNAUTHORIZED);
}

static void
read_unauthorized_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content)
{
    int r;
    char *server_type = data;
    struct sol_lwm2m_resource res;

    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_UNAUTHORIZED);

    SOL_LWM2M_RESOURCE_INIT(r, &res, ACCESS_CONTROL_OBJECT_ACL,
        SOL_LWM2M_RESOURCE_TYPE_MULTIPLE, 1, SOL_LWM2M_RESOURCE_DATA_TYPE_INT,
        0, SOL_TYPE_CHECK(int64_t, SOL_LWM2M_ACL_READ | SOL_LWM2M_ACL_WRITE));
    ASSERT(r == 0);

    printf("DBG: '%s' ---[Write /2/3/2]---> '%s'\n",
        server_type, sol_lwm2m_client_info_get_name(client));
    r = sol_lwm2m_server_write(server, client, "/2/3/2",
        &res, 1, write_acl_unauthorized_cb, data);
    ASSERT(r == 0);

    sol_lwm2m_resource_clear(&res);
}

static void
observe_unauthorized_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content)
{
    int r;
    char *server_type = data;

    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_UNAUTHORIZED);

    printf("DBG: '%s' ---[Read /999]---> '%s'\n",
        server_type, sol_lwm2m_client_info_get_name(client));
    r = sol_lwm2m_server_read(server, client, "/999",
        read_unauthorized_cb, data);
    ASSERT(r == 0);
}

static void
sec_registration_event_cb(void *data, struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event)
{
    int r;
    uint16_t i;
    char *server_type = data;
    struct sol_lwm2m_resource res[2];

    if (event == SOL_LWM2M_REGISTRATION_EVENT_REGISTER) {
        check_cinfo(cinfo, "cli1", NULL, NULL, SOL_LWM2M_BINDING_MODE_U, true);

        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &res[0], DUMMY_OBJECT_BOOLEAN_FALSE_ID,
            SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, false);
        ASSERT(r == 0);
        SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &res[1], DUMMY_OBJECT_FLOAT_ID,
            SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT, FLOAT_VALUE);
        ASSERT(r == 0);

        printf("DBG: '%s' ---[Write /999]---> '%s'\n",
            server_type, sol_lwm2m_client_info_get_name(cinfo));
        r = sol_lwm2m_server_write(server, cinfo, "/999",
            res, sol_util_array_size(res), NULL, data);
        ASSERT(r == -EINVAL);

        for (i = 0; i < sol_util_array_size(res); i++)
            sol_lwm2m_resource_clear(&res[i]);

        printf("DBG: '%s' ---[Observe /1]---> '%s'\n",
            server_type, sol_lwm2m_client_info_get_name(cinfo));
        r = sol_lwm2m_server_add_observer(server, cinfo, "/1",
            observe_unauthorized_cb, data);
        ASSERT(r == 0);
    } else {
        ASSERT(1 == 2); //TIMEOUT/UNREGISTER, this must not happen!
    }
}

static void
write_nosec_server_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo, const char *path,
    enum sol_coap_response_code response_code)
{
    int r;
    char *server_type = data;

    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_CHANGED);
    ASSERT(!strcmp("/0/1", path));

    printf("DBG: '%s' ---[Bootstrap Finish]---> '%s'\n",
        server_type, sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo));
    r = sol_lwm2m_bootstrap_server_send_finish(server, bs_cinfo);
    ASSERT(r == 0);
}

static void
write_servers_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo, const char *path,
    enum sol_coap_response_code response_code)
{
    int r;
    uint16_t i;
    struct sol_lwm2m_resource nosec_server[4];
    char *server_type = data;

    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_CHANGED);
    ASSERT(!strcmp("/1", path));

    // NoSec Server's Security Object
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &nosec_server[0], SECURITY_OBJECT_SERVER_URI,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &nosec_server_coap_addr);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &nosec_server[1], SECURITY_OBJECT_IS_BOOTSTRAP,
        SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, false);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &nosec_server[2], SECURITY_OBJECT_SECURITY_MODE,
        SOL_LWM2M_SECURITY_MODE_NO_SEC);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &nosec_server[3], SECURITY_OBJECT_SERVER_ID, 101);
    ASSERT(r == 0);

    printf("DBG: '%s' ---[Bootstrap Write /0/1]---> '%s'\n",
        server_type, sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo));
    r = sol_lwm2m_bootstrap_server_write(server, bs_cinfo, "/0/1",
        nosec_server, sol_util_array_size(nosec_server), write_nosec_server_cb, data);
    ASSERT(r == 0);

    for (i = 0; i < sol_util_array_size(nosec_server); i++)
        sol_lwm2m_resource_clear(&nosec_server[i]);
}

static void
write_sec_server_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo, const char *path,
    enum sol_coap_response_code response_code)
{
    int r;
    uint16_t i, j;
    struct sol_lwm2m_resource nosec_server[3], sec_server[3];
    struct sol_lwm2m_resource *servers[2] = {
        nosec_server, sec_server
    };
    size_t servers_len[2] = {
        sol_util_array_size(nosec_server), sol_util_array_size(sec_server)
    };
    uint16_t servers_ids[2] = {
        0, 4
    };
    char *server_type = data;

    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_CHANGED);
    ASSERT(!strcmp("/0/0", path));

    // NoSec Server's Server Object
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &nosec_server[0], SERVER_OBJECT_SERVER_ID, 101);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &nosec_server[1], SERVER_OBJECT_LIFETIME, LIFETIME);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &nosec_server[2], SERVER_OBJECT_BINDING,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &binding);
    ASSERT(r == 0);

    // PSK-secured Server's Server Object
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &sec_server[0], SERVER_OBJECT_SERVER_ID, 102);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &sec_server[1], SERVER_OBJECT_LIFETIME, LIFETIME);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &sec_server[2], SERVER_OBJECT_BINDING,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &binding);
    ASSERT(r == 0);

    printf("DBG: '%s' ---[Bootstrap Write /1]---> '%s'\n",
        server_type, sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo));
    r = sol_lwm2m_bootstrap_server_write_object(server, bs_cinfo, "/1",
        servers, servers_len, servers_ids, sol_util_array_size(servers), write_servers_cb, data);
    ASSERT(r == 0);

    for (i = 0; i < sol_util_array_size(servers); i++)
        for (j = 0; j < sol_util_array_size(nosec_server); j++)
            sol_lwm2m_resource_clear(&servers[i][j]);
}

static void
delete_all_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo, const char *path,
    enum sol_coap_response_code response_code)
{
    int r;
    uint16_t i;
    struct sol_lwm2m_resource sec_server[6];
    char *server_type = data;

    ASSERT(response_code == SOL_COAP_RESPONSE_CODE_DELETED);
    ASSERT(!strcmp("/", path));

    // PSK-secured Server's Security Object
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &sec_server[0], SECURITY_OBJECT_SERVER_URI,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &sec_server_dtls_addr);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &sec_server[1], SECURITY_OBJECT_IS_BOOTSTRAP,
        SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, false);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &sec_server[2], SECURITY_OBJECT_SECURITY_MODE,
        SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &sec_server[3], SECURITY_OBJECT_PUBLIC_KEY_OR_IDENTITY,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &sec_server_psk_id);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &sec_server[4], SECURITY_OBJECT_SECRET_KEY,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &sec_server_psk_key);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &sec_server[5], SECURITY_OBJECT_SERVER_ID, 102);
    ASSERT(r == 0);

    printf("DBG: '%s' ---[Bootstrap Write /0/0]---> '%s'\n",
        server_type, sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo));
    r = sol_lwm2m_bootstrap_server_write(server, bs_cinfo, "/0/0",
        sec_server, sol_util_array_size(sec_server), write_sec_server_cb, data);
    ASSERT(r == 0);

    for (i = 0; i < sol_util_array_size(sec_server); i++)
        sol_lwm2m_resource_clear(&sec_server[i]);
}

static void
bootstrap_request_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo)
{
    int r;
    char *server_type = data;

    ASSERT(!strcmp("cli1", sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo)));

    printf("DBG: '%s' ---[Bootstrap Delete /]---> '%s'\n",
        server_type, sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo));
    r = sol_lwm2m_bootstrap_server_delete_object_instance(server, bs_cinfo, "/",
        delete_all_cb, data);
    ASSERT(r == 0);
}

static void
bootstrap_finish_cb(void *data,
    struct sol_lwm2m_client *client,
    enum sol_lwm2m_bootstrap_event event)
{
    int r;

    ASSERT(event == SOL_LWM2M_BOOTSTRAP_EVENT_FINISHED);

    r = sol_lwm2m_client_start(client);
    ASSERT(r == 0);
}
#endif

int
main(int argc, char *argv[])
{
    struct sol_lwm2m_server *nosec_server;
    struct sol_lwm2m_client *nosec_client;
    static const struct sol_lwm2m_object *nosec_objects[] =
    { &security_object, &server_object, &dummy_object, NULL };
    struct security_obj_instance_ctx *nosec_security_data;
    struct server_obj_instance_ctx *nosec_server_data;
    bool nosec_first = true;
    int r;

#ifdef DTLS
    struct sol_lwm2m_bootstrap_server *bs_server;
    struct sol_lwm2m_server *sec_server;
    struct sol_lwm2m_client *sec_client;
    static const struct sol_lwm2m_object *sec_objects[] =
    { &security_object, &server_object,
      &access_control_object, &dummy_object, NULL };
    struct security_obj_instance_ctx *sec_security_data;
    bool sec_first = true;
    struct sol_lwm2m_security_rpk bs_server_rpk;
    unsigned char buf_aux[RPK_PUBLIC_KEY_LEN];
    struct sol_blob *bs_server_known_pub_keys[] = { NULL, NULL };
#endif

    r = sol_init();
    ASSERT(!r);

    // ============================================== NoSec Server Initialization
    nosec_server = sol_lwm2m_server_new(SOL_LWM2M_DEFAULT_SERVER_PORT_COAP, 0);
    ASSERT(nosec_server != NULL);

    r = sol_lwm2m_server_add_registration_monitor(nosec_server,
        nosec_registration_event_cb, "NoSec");
    ASSERT_INT_EQ(r, 0);

    // ============================================== NoSec Client Initialization
    nosec_client = sol_lwm2m_client_new(CLIENT_NAME, OBJ_PATH,
        SMS_NUMBER, nosec_objects, &nosec_first);
    ASSERT(nosec_client != NULL);

    nosec_server_data = calloc(1, sizeof(struct server_obj_instance_ctx));
    ASSERT(nosec_server_data != NULL);

    nosec_server_data->client = nosec_client;
    nosec_server_data->binding = &binding;
    nosec_server_data->server_id = 103;
    nosec_server_data->lifetime = LIFETIME;

    r = sol_lwm2m_client_add_object_instance(nosec_client,
        &server_object, nosec_server_data);
    ASSERT_INT_EQ(r, 0);

    nosec_security_data = calloc(1, sizeof(struct security_obj_instance_ctx));
    ASSERT(nosec_security_data != NULL);

    nosec_security_data->client = nosec_client;
    nosec_security_data->security_mode = SOL_LWM2M_SECURITY_MODE_NO_SEC;
    nosec_security_data->server_uri = &nosec_server_coap_addr;
    nosec_security_data->server_id = 103;

    r = sol_lwm2m_client_add_object_instance(nosec_client,
        &security_object, nosec_security_data);
    ASSERT_INT_EQ(r, 0);

    r = sol_lwm2m_client_start(nosec_client);
    ASSERT_INT_EQ(r, 0);

#ifdef DTLS
    // ======================================== PSK-Secured Server Initialization
    sec_server = sol_lwm2m_server_new(5693,
        1, SOL_LWM2M_DEFAULT_SERVER_PORT_DTLS,
        SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY, sec_server_known_keys);
    ASSERT(sec_server != NULL);

    r = sol_lwm2m_server_add_registration_monitor(sec_server,
        sec_registration_event_cb, "PSK-Secured");
    ASSERT_INT_EQ(r, 0);

    // ========================================== Bootstrap Server Initialization
    r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
        sol_str_slice_from_str(SEC_CLIENT_PUBLIC_KEY), SOL_DECODE_BOTH);
    bs_server_known_pub_keys[0] = sol_blob_new_dup(buf_aux, RPK_PUBLIC_KEY_LEN);

    r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
        sol_str_slice_from_str(BS_SERVER_PRIVATE_KEY), SOL_DECODE_BOTH);
    bs_server_rpk.private_key = sol_blob_new_dup(buf_aux, RPK_PRIVATE_KEY_LEN);
    r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
        sol_str_slice_from_str(BS_SERVER_PUBLIC_KEY), SOL_DECODE_BOTH);
    bs_server_rpk.public_key = sol_blob_new_dup(buf_aux, RPK_PUBLIC_KEY_LEN);

    bs_server = sol_lwm2m_bootstrap_server_new(5784, known_clients,
        1, SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY,
        &bs_server_rpk, bs_server_known_pub_keys);
    ASSERT(bs_server != NULL);

    r = sol_lwm2m_bootstrap_server_add_request_monitor(bs_server,
        bootstrap_request_cb, "RPK-Secured");
    ASSERT_INT_EQ(r, 0);

    // ====================== PSK-Secured (+Access Control) Client Initialization
    sec_client = sol_lwm2m_client_new("cli1", NULL, NULL,
        sec_objects, &sec_first);
    ASSERT(sec_client != NULL);

    r = sol_lwm2m_client_add_bootstrap_finish_monitor(sec_client,
        bootstrap_finish_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    sec_security_data = calloc(1, sizeof(struct security_obj_instance_ctx));
    ASSERT(sec_security_data != NULL);

    sec_security_data->client = sec_client;
    sec_security_data->server_uri = &bs_server_addr;
    sec_security_data->is_bootstrap = true;
    sec_security_data->security_mode = SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY;
    r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
        sol_str_slice_from_str(SEC_CLIENT_PRIVATE_KEY), SOL_DECODE_BOTH);
    sec_security_data->secret_key = sol_blob_new_dup(buf_aux, RPK_PRIVATE_KEY_LEN);
    r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
        sol_str_slice_from_str(SEC_CLIENT_PUBLIC_KEY), SOL_DECODE_BOTH);
    sec_security_data->public_key_or_id = sol_blob_new_dup(buf_aux, RPK_PUBLIC_KEY_LEN);
    r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
        sol_str_slice_from_str(BS_SERVER_PUBLIC_KEY), SOL_DECODE_BOTH);
    sec_security_data->server_public_key = sol_blob_new_dup(buf_aux, RPK_PUBLIC_KEY_LEN);
    sec_security_data->client_hold_off_time = 0;

    r = sol_lwm2m_client_add_object_instance(sec_client,
        &security_object, sec_security_data);
    ASSERT_INT_EQ(r, 0);

    r = sol_lwm2m_client_start(sec_client);
    ASSERT_INT_EQ(r, 0);
#endif

    sol_run();

    sol_lwm2m_client_stop(nosec_client);
    sol_lwm2m_client_del(nosec_client);
    sol_lwm2m_server_del(nosec_server);
#ifdef DTLS
    sol_lwm2m_client_stop(sec_client);
    sol_lwm2m_client_del(sec_client);
    sol_lwm2m_server_del(sec_server);
    sol_lwm2m_bootstrap_server_del(bs_server);

    sol_blob_unref(bs_server_known_pub_keys[0]);
    sol_blob_unref(bs_server_rpk.private_key);
    sol_blob_unref(bs_server_rpk.public_key);
#endif
    sol_shutdown();
    return 0;
}
