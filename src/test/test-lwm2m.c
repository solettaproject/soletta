/*
 * This file is part of the Soletta Project
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

#include <time.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include "test.h"
#include "sol-util.h"
#include "sol-lwm2m.h"
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

#define SECURITY_SERVER_OBJECT_ID (0)
#define SECURITY_SERVER_URI (0)
#define SECURITY_SERVER_IS_BOOTSTRAP (1)
#define SECURITY_SERVER_ID (10)

#define SERVER_OBJECT_ID (1)
#define SERVER_OBJECT_SERVER_ID (0)
#define SERVER_OBJECT_LIFETIME (1)
#define SERVER_OBJECT_BINDING (7)

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

static int
security_object_read(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, struct sol_lwm2m_resource *res)
{
    int r;

    switch (res_id) {
    case SECURITY_SERVER_URI:
        SOL_LWM2M_RESOURCE_INIT(r, res, 0, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING,
            sol_str_slice_from_str("coap://localhost:5683"));
        break;
    case SECURITY_SERVER_IS_BOOTSTRAP:
        SOL_LWM2M_RESOURCE_INIT(r, res, 1, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_BOOLEAN, false);
        break;
    case SECURITY_SERVER_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, 10, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_INT, 101);
        break;
    default:
        r = -EINVAL;
    }

    return r;
}

static int
server_object_read(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client,
    uint16_t instance_id, uint16_t res_id, struct sol_lwm2m_resource *res)
{
    int r;

    switch (res_id) {
    case SERVER_OBJECT_SERVER_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, 0, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_INT, 101);
        break;
    case SERVER_OBJECT_LIFETIME:
        SOL_LWM2M_RESOURCE_INIT(r, res, 1, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_INT, LIFETIME);
        break;
    case SERVER_OBJECT_BINDING:
        SOL_LWM2M_RESOURCE_INIT(r, res, 7, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING,
            sol_str_slice_from_str("U"));
        break;
    default:
        r = -EINVAL;
    }

    return r;
}

/*
   This function is used by the lwm2m client and server to check if the tlv
   are its values are valid. However the lwm2m server will pass NULL as
   the second argument.
 */
static void
check_tlv_and_save(struct sol_vector *tlvs, struct dummy_ctx *ctx)
{
    static bool first = true;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;
    int r;

    SOL_VECTOR_FOREACH_IDX (tlvs, tlv, i) {
        uint8_t *buf;
        uint16_t len, obj, instance;
        bool b;
        int64_t int64;
        double fp;

        if (tlv->type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE) {
            switch (tlv->id) {
            case DUMMY_OBJECT_STRING_ID:
                r = sol_lwm2m_tlv_get_bytes(tlv, &buf, &len);
                ASSERT(r == 0);
                ASSERT(len == strlen(STR));
                ASSERT(!memcmp(STR, buf, len));
                if (ctx) {
                    ctx->str1 = strndup((const char *)buf, len);
                    ASSERT(ctx->str1 != NULL);
                }
                break;
            case DUMMY_OBJECT_OPAQUE_ID:
                r = sol_lwm2m_tlv_get_bytes(tlv, &buf, &len);
                ASSERT(r == 0);
                ASSERT(len == strlen(OPAQUE_STR));
                ASSERT(!memcmp(OPAQUE_STR, buf, len));
                if (ctx) {
                    ctx->opaque = strndup((const char *)buf, len);
                    ASSERT(ctx->opaque != NULL);
                }
                break;
            case DUMMY_OBJECT_INT_ID:
                r = sol_lwm2m_tlv_to_int(tlv, &int64);
                ASSERT(r == 0);
                if (first || !ctx)
                    ASSERT(int64 == INT_VALUE);
                else
                    ASSERT(int64 == INT_REPLACE_VALUE);
                if (ctx)
                    ctx->i = int64;
                break;
            case DUMMY_OBJECT_BOOLEAN_FALSE_ID:
                r = sol_lwm2m_tlv_to_bool(tlv, &b);
                ASSERT(r == 0);
                ASSERT(!b);
                if (ctx)
                    ctx->f = b;
                break;
            case DUMMY_OBJECT_BOOLEAN_TRUE_ID:
                r = sol_lwm2m_tlv_to_bool(tlv, &b);
                ASSERT(r == 0);
                ASSERT(b);
                if (ctx)
                    ctx->t = b;
                break;
            case DUMMY_OBJECT_FLOAT_ID:
                r = sol_lwm2m_tlv_to_float(tlv, &fp);
                ASSERT(r == 0);
                ASSERT(fp - FLOAT_VALUE <= 0.00);
                if (ctx)
                    ctx->fp = fp;
                break;
            case DUMMY_OBJECT_OBJ_LINK_ID:
                r = sol_lwm2m_tlv_to_obj_link(tlv, &obj, &instance);
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
            r = sol_lwm2m_tlv_to_int(tlv, &int64);
            ASSERT(r == 0);
            if (tlv->id == 0)
                ASSERT(int64 == ARRAY_VALUE_ONE);
            else
                ASSERT(int64 == ARRAY_VALUE_TWO);
            if (ctx)
                ctx->array[tlv->id] = int64;
        }
    }
    first = false;
}

static int
create_dummy(void *user_data, struct sol_lwm2m_client *client,
    uint16_t instance_id, void **instance_data,
    enum sol_lwm2m_content_type content_type,
    const struct sol_str_slice content)
{
    struct dummy_ctx *ctx = calloc(1, sizeof(struct dummy_ctx));
    struct sol_vector tlvs;
    int r;

    ASSERT(ctx);
    *instance_data = ctx;
    ctx->id = instance_id;

    ASSERT(content_type == SOL_LWM2M_CONTENT_TYPE_TLV);
    r = sol_lwm2m_parse_tlv(content, &tlvs);
    ASSERT(r == 0);
    check_tlv_and_save(&tlvs, ctx);
    sol_lwm2m_tlv_array_clear(&tlvs);
    return 0;
}

static int
write_dummy_tlv(void *instance_data, void *user_data,
    struct sol_lwm2m_client *client, uint16_t instance_id,
    struct sol_vector *tlvs)
{
    struct dummy_ctx *ctx = instance_data;

    check_tlv_and_save(tlvs, ctx);
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
    int r;

    switch (res_id) {
    case DUMMY_OBJECT_STRING_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, res_id, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_STRING,
            sol_str_slice_from_str(ctx->str1));
        break;
    case DUMMY_OBJECT_OPAQUE_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, res_id, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE,
            sol_str_slice_from_str(ctx->opaque));
        break;
    case DUMMY_OBJECT_INT_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, res_id, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_INT, ctx->i);
        break;
    case DUMMY_OBJECT_BOOLEAN_FALSE_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, res_id, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_BOOLEAN, ctx->f);
        break;
    case DUMMY_OBJECT_BOOLEAN_TRUE_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, res_id, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_BOOLEAN, ctx->t);
        break;
    case DUMMY_OBJECT_FLOAT_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, res_id, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT, ctx->fp);
        break;
    case DUMMY_OBJECT_OBJ_LINK_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, res_id, 1,
            SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK, ctx->obj, ctx->instance);
        break;
    case DUMMY_OBJECT_ARRAY_ID:
        SOL_LWM2M_RESOURCE_INIT(r, res, res_id, 2,
            SOL_LWM2M_RESOURCE_DATA_TYPE_INT, ctx->array[0], ctx->array[1]);
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

    r = sol_lwm2m_send_update(client);
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
    .id = SECURITY_SERVER_OBJECT_ID,
    .resources_count = 12,
    .read = security_object_read
};

static const struct sol_lwm2m_object server_object = {
    SOL_SET_API_VERSION(.api_version = SOL_LWM2M_OBJECT_API_VERSION, )
    .id = SERVER_OBJECT_ID,
    .resources_count = 9,
    .read = server_object_read
};

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
execute_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    sol_coap_responsecode_t response_code)
{
    ASSERT(response_code == SOL_COAP_RSPCODE_CHANGED);
}

static void
write_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    sol_coap_responsecode_t response_code)
{
    int r;

    ASSERT(response_code == SOL_COAP_RSPCODE_CHANGED);

    r = sol_lwm2m_server_management_execute(server, client, "/999/0/8",
        EXECUTE_ARGS, execute_cb, NULL);
    ASSERT(r == 0);
}

static void
read_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    sol_coap_responsecode_t response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content)
{
    struct sol_vector tlvs;
    int r;
    struct sol_lwm2m_resource res;

    ASSERT(response_code == SOL_COAP_RSPCODE_CONTENT);
    ASSERT(content_type == SOL_LWM2M_CONTENT_TYPE_TLV);

    r = sol_lwm2m_parse_tlv(content, &tlvs);
    ASSERT(r == 0);

    check_tlv_and_save(&tlvs, NULL);

    SOL_LWM2M_RESOURCE_INIT(r, &res, DUMMY_OBJECT_INT_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_INT,
        (int64_t)INT_REPLACE_VALUE);
    ASSERT(r == 0);
    r = sol_lwm2m_server_management_write(server, client, "/999/0/2",
        &res, 1, write_cb, NULL);
    ASSERT(r == 0);
    sol_lwm2m_resource_clear(&res);
    sol_lwm2m_tlv_array_clear(&tlvs);
}

static void
observe_res_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    sol_coap_responsecode_t response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content)
{
    static int i = 0;
    struct sol_vector tlvs;
    struct sol_lwm2m_tlv *tlv;
    int64_t v;
    int r;

    ASSERT(response_code == SOL_COAP_RSPCODE_CHANGED ||
        response_code == SOL_COAP_RSPCODE_CONTENT);

    r = sol_lwm2m_parse_tlv(content, &tlvs);
    ASSERT(r == 0);
    ASSERT(tlvs.len == 1);
    tlv = sol_vector_get_nocheck(&tlvs, 0);
    r = sol_lwm2m_tlv_to_int(tlv, &v);
    ASSERT(r == 0);

    if (i == 0)
        ASSERT(v == INT_VALUE);
    else if (i == 1)
        ASSERT(v == INT_REPLACE_VALUE);
    else
        ASSERT(1 == 2); //MUST NOT HAPPEN!

    i++;
    sol_lwm2m_tlv_clear(tlv);
    sol_vector_clear(&tlvs);
}

static void
create_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    sol_coap_responsecode_t response_code)
{
    int r;

    ASSERT(response_code == SOL_COAP_RSPCODE_CREATED);

    r = sol_lwm2m_server_management_read(server, client, "/999/0",
        read_cb, NULL);
    ASSERT(r == 0);

    r = sol_lwm2m_server_add_observer(server, client, "/999/0/2",
        observe_res_cb, NULL);
    ASSERT(r == 0);
}

static void
create_obj(struct sol_lwm2m_server *server, struct sol_lwm2m_client_info *cinfo)
{
    int r;
    size_t i;
    struct sol_lwm2m_resource res[8];

    SOL_LWM2M_RESOURCE_INIT(r, &res[0], DUMMY_OBJECT_STRING_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING,
        sol_str_slice_from_str(STR));
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_INIT(r, &res[1], DUMMY_OBJECT_OPAQUE_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE,
        sol_str_slice_from_str(OPAQUE_STR));
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_INIT(r, &res[2], DUMMY_OBJECT_INT_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_INT, (int64_t)INT_VALUE);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_INIT(r, &res[3], DUMMY_OBJECT_BOOLEAN_FALSE_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_BOOLEAN, false);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_INIT(r, &res[4], DUMMY_OBJECT_BOOLEAN_TRUE_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_BOOLEAN, true);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_INIT(r, &res[5], DUMMY_OBJECT_FLOAT_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT, FLOAT_VALUE);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_INIT(r, &res[6], DUMMY_OBJECT_OBJ_LINK_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK, OBJ_VALUE, INSTANCE_VALUE);
    ASSERT(r == 0);
    SOL_LWM2M_RESOURCE_INIT(r, &res[7], DUMMY_OBJECT_ARRAY_ID, 2,
        SOL_LWM2M_RESOURCE_DATA_TYPE_INT, ARRAY_VALUE_ONE, ARRAY_VALUE_TWO);
    ASSERT(r == 0);
    r = sol_lwm2m_server_management_create(server, cinfo, "/999", res,
        sol_util_array_size(res), create_cb, NULL);
    ASSERT(r == 0);

    for (i = 0; i < sol_util_array_size(res); i++)
        sol_lwm2m_resource_clear(&res[i]);
}

static void
delete_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    sol_coap_responsecode_t response_code)
{
    ASSERT(response_code == SOL_COAP_RSPCODE_DELETED);
    sol_quit();
}

static void
registration_event_cb(void *data, struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event)
{
    int r;

    if (event == SOL_LWM2M_REGISTRATION_EVENT_REGISTER) {
        uint32_t lf;
        const struct sol_ptr_vector *objects;
        struct sol_lwm2m_client_object *object;
        uint16_t i, objects_found = 0;

        ASSERT(!strcmp(CLIENT_NAME, sol_lwm2m_client_info_get_name(cinfo)));
        ASSERT(!strcmp(SMS_NUMBER, sol_lwm2m_client_info_get_sms(cinfo)));
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
            if (obj_id == SECURITY_SERVER_OBJECT_ID ||
                obj_id == SERVER_OBJECT_ID || obj_id == DUMMY_OBJECT_ID)
                objects_found++;
        }

        ASSERT(objects_found == 3);
        create_obj(server, cinfo);

    } else if (event == SOL_LWM2M_REGISTRATION_EVENT_UPDATE) {
        r = sol_lwm2m_server_del_observer(server, cinfo, "/999/0/2",
            observe_res_cb, NULL);
        ASSERT(r == 0);
        r = sol_lwm2m_server_management_delete(server, cinfo, "/999/0",
            delete_cb, NULL);
        ASSERT(r == 0);
    } else {
        ASSERT(1 == 2); //TIMEOUT/UNREGISTER, this must not happen!
    }
}

int
main(int argc, char *argv[])
{
    struct sol_lwm2m_server *server;
    struct sol_lwm2m_client *client;
    static const struct sol_lwm2m_object *objects[] =
    { &security_object, &server_object, &dummy_object, NULL };
    int r;

    r = sol_init();
    ASSERT(!r);

    server = sol_lwm2m_server_new(SOL_LWM2M_DEFAULT_SERVER_PORT);
    ASSERT(server != NULL);

    r = sol_lwm2m_server_add_registration_monitor(server, registration_event_cb,
        NULL);
    ASSERT_INT_EQ(r, 0);

    client = sol_lwm2m_client_new(CLIENT_NAME, OBJ_PATH, SMS_NUMBER, objects, NULL);
    ASSERT(client != NULL);

    r = sol_lwm2m_add_object_instance(client, &server_object, NULL);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_add_object_instance(client, &security_object, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_lwm2m_client_start(client);
    ASSERT_INT_EQ(r, 0);

    sol_run();
    sol_lwm2m_client_del(client);
    sol_lwm2m_server_del(server);
    sol_shutdown();
    return 0;
}
