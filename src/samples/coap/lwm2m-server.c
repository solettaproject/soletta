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
   To run: ./lwm2m-sample-server [-c PORT] [-d PORT] [-s SEC_MODE]
   For every LWM2M client that connects with the server, the server will
   try to create an LWM2M location object if it does not exist.
   After that, it will observe the location object.
 */

#include "sol-lwm2m-server.h"
#include "sol-mainloop.h"
#include "sol-vector.h"
#include "sol-util.h"

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#define LOCATION_OBJ_ID (6)
#define LONGITUDE_ID (1)
#define LATITUDE_ID (0)
#define TIMESTAMP_ID (5)

#define PSK_KEY_LEN 16
#define RPK_PRIVATE_KEY_LEN 32
#define RPK_PUBLIC_KEY_LEN (2 * RPK_PRIVATE_KEY_LEN)

//FIXME: UNSEC: Hardcoded Crypto Keys
#define CLIENT_SERVER_PSK_ID ("cli1")
#define CLIENT_SERVER_PSK_KEY ("0123456789ABCDEF")

#define CLIENT_PUBLIC_KEY ("D055EE14084D6E0615599DB583913E4A3E4526A2704D61F27A4CCFBA9758EF9A" \
    "B418B64AFE8030DA1DDCF4F42E2F2631D043B1FB03E22F4D17DE43F9F9ADEE70")
#define SERVER_PRIVATE_KEY ("65c5e815d0c40e8f99143e5c905cbd9026444395af207a914063d8f0a7e63f22")
#define SERVER_PUBLIC_KEY ("3b88c213ca5ccfd9c5a7f73715760d7d9a5220768f2992d2628ae1389cbca4c6" \
    "d1b73cc6d61ae58783135749fb03eaaa64a7a1adab8062ed5fc0d7b86ba2d5ca")

enum location_object_status {
    LOCATION_OBJECT_NOT_FOUND,
    LOCATION_OBJECT_WITH_NO_INSTANCES,
    LOCATION_OBJECT_WITH_INSTANCES
};

static struct sol_blob lat = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"48.858093",
    .size = sizeof("48.858093") - 1,
    .refcnt = 1
};

static struct sol_blob longi = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"2.294694",
    .size = sizeof("2.294694") - 1,
    .refcnt = 1
};

static enum location_object_status
get_location_object_status(const struct sol_lwm2m_client_info *cinfo)
{
    uint16_t i;
    struct sol_lwm2m_client_object *object;
    const struct sol_ptr_vector *objects =
        sol_lwm2m_client_info_get_objects(cinfo);

    SOL_PTR_VECTOR_FOREACH_IDX (objects, object, i) {
        const struct sol_ptr_vector *instances;
        uint16_t id;
        int r;

        r = sol_lwm2m_client_object_get_id(object, &id);
        if (r < 0) {
            fprintf(stderr, "Could not fetch the object id from %p\n", object);
            return LOCATION_OBJECT_NOT_FOUND;
        }

        if (id != LOCATION_OBJ_ID)
            continue;

        instances = sol_lwm2m_client_object_get_instances(object);

        if (sol_ptr_vector_get_len(instances))
            return LOCATION_OBJECT_WITH_INSTANCES;
        return LOCATION_OBJECT_WITH_NO_INSTANCES;
    }

    return LOCATION_OBJECT_NOT_FOUND;
}

static void
location_changed_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content)
{
    struct sol_vector tlvs;
    const char *name = sol_lwm2m_client_info_get_name(cinfo);
    int r;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;

    if (response_code != SOL_COAP_RESPONSE_CODE_CHANGED &&
        response_code != SOL_COAP_RESPONSE_CODE_CONTENT) {
        fprintf(stderr, "Could not get the location object value from"
            " client %s\n", name);
        return;
    }

    if (content_type != SOL_LWM2M_CONTENT_TYPE_TLV) {
        fprintf(stderr, "The location object content from client %s is not"
            " in TLV format. Received format: %d\n", name, content_type);
        return;
    }

    r = sol_lwm2m_parse_tlv(content, &tlvs);

    if (r < 0) {
        fprintf(stderr, "Could not parse the tlv from client: %s\n", name);
        return;
    }

    SOL_VECTOR_FOREACH_IDX (&tlvs, tlv, i) {
        const char *prop;
        SOL_BUFFER_DECLARE_STATIC(buf, 32);

        if (tlv->id == LATITUDE_ID)
            prop = "latitude";
        else if (tlv->id == LONGITUDE_ID)
            prop = "longitude";
        else
            continue;

        r = sol_lwm2m_tlv_get_bytes(tlv, &buf);
        if (r < 0) {
            fprintf(stderr, "Could not get the %s value from client %s\n",
                prop, name);
            break;
        }

        printf("Client %s %s is %.*s\n", name, prop, (int)buf.used, (char *)buf.data);
        sol_buffer_fini(&buf);
    }

    sol_lwm2m_tlv_list_clear(&tlvs);
}

static void
observe_location(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo)
{
    int r;

    r = sol_lwm2m_server_add_observer(server, cinfo, "/6",
        location_changed_cb, NULL);

    if (r < 0)
        fprintf(stderr, "Could not send an observe request to the location"
            " object\n");
    else
        printf("Observe request to the location object sent\n");
}

static void
create_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo, const char *path,
    enum sol_coap_response_code response_code)
{
    const char *name = sol_lwm2m_client_info_get_name(cinfo);

    if (response_code != SOL_COAP_RESPONSE_CODE_CREATED) {
        fprintf(stderr, "The client %s could not create the location object.\n",
            name);
        return;
    }

    printf("The client %s created the location object."
        " Observing it now.\n", name);
    observe_location(server, cinfo);
}

static void
create_location_obj(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo)
{
    int r;
    struct sol_lwm2m_resource res[3];
    size_t i;

    /*
       Send a request the create a location object instance.
       It sets only the mandatory fields.
       The coordinates are the position of the Eiffel tower.
     */
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &res[0], LATITUDE_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &lat);

    if (r < 0) {
        fprintf(stderr, "Could init the latitude resource\n");
        return;
    }

    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &res[1], LONGITUDE_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &longi);

    if (r < 0) {
        fprintf(stderr, "Could not init the longitude resource\n");
        return;
    }

    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &res[2], TIMESTAMP_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_TIME,
        (int64_t)time(NULL));

    if (r < 0) {
        fprintf(stderr, "Could not init the timestamp resource\n");
        return;
    }

    r = sol_lwm2m_server_create_object_instance(server, cinfo, "/6", res,
        sol_util_array_size(res), create_cb, NULL);

    for (i = 0; i < sol_util_array_size(res); i++)
        sol_lwm2m_resource_clear(&res[i]);

    if (r < 0)
        fprintf(stderr, "Could not send a request to create a"
            " location object\n");
    else
        printf("Creation request sent\n");
}

static void
registration_cb(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event)
{
    const char *name;
    enum location_object_status status;

    name = sol_lwm2m_client_info_get_name(cinfo);

    if (event == SOL_LWM2M_REGISTRATION_EVENT_UPDATE) {
        printf("Client %s updated\n", name);
        return;
    } else if (event == SOL_LWM2M_REGISTRATION_EVENT_UNREGISTER) {
        printf("Client %s unregistered\n", name);
        return;
    } else if (event == SOL_LWM2M_REGISTRATION_EVENT_TIMEOUT) {
        printf("Client %s timeout\n", name);
        return;
    }

    printf("Client %s registered\n", name);
    status = get_location_object_status(cinfo);

    if (status == LOCATION_OBJECT_NOT_FOUND) {
        fprintf(stderr,
            "The client %s does not implement the location object!\n",
            name);
    } else if (status == LOCATION_OBJECT_WITH_NO_INSTANCES) {
        printf("The client %s does not have an instance of the location"
            " object. Creating one.\n", name);
        create_location_obj(server, cinfo);
    } else {
        printf("The client %s have an location object instance,"
            " observing\n", name);
        observe_location(server, cinfo);
    }
}

int
main(int argc, char *argv[])
{
    struct sol_lwm2m_server *server;
    uint16_t coap_port = SOL_LWM2M_DEFAULT_SERVER_PORT_COAP;
    uint16_t dtls_port = SOL_LWM2M_DEFAULT_SERVER_PORT_DTLS;
    int r;
    enum sol_lwm2m_security_mode sec_mode = SOL_LWM2M_SECURITY_MODE_NO_SEC;
    char usage[256];
    struct sol_lwm2m_security_rpk my_rpk;
    unsigned char buf_aux[RPK_PUBLIC_KEY_LEN];
    struct sol_lwm2m_security_psk *known_keys[] = {
        &((struct sol_lwm2m_security_psk) {
            .id = NULL,
            .key = NULL
        }),
        NULL
    };
    struct sol_blob *known_pub_keys[] = { NULL, NULL };

    snprintf(usage, sizeof(usage),
        "Usage: ./lwm2m-sample-server [-c PORT] [-d PORT] [-s SEC_MODE]\n"
        "Where default CoAP PORT=%d, default DTLS PORT=%d"
        " and SEC_MODE is an integer as per:\n"
        "\tPRE_SHARED_KEY=%d\n"
        "\tRAW_PUBLIC_KEY=%d\n"
        "\tCERTIFICATE=%d\n"
        "\tNO_SEC=%d (default)\n",
        SOL_LWM2M_DEFAULT_SERVER_PORT_COAP,
        SOL_LWM2M_DEFAULT_SERVER_PORT_DTLS,
        SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY,
        SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY,
        SOL_LWM2M_SECURITY_MODE_CERTIFICATE,
        SOL_LWM2M_SECURITY_MODE_NO_SEC);

    while ((r = getopt(argc, argv, "c:d:s:")) != -1) {
        switch (r) {
        case 'c':
            coap_port = atoi(optarg);
            break;
        case 'd':
            dtls_port = atoi(optarg);
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

    printf("Using LWM2M port %" PRIu16 " for CoAP", coap_port);
    if (sec_mode != SOL_LWM2M_SECURITY_MODE_NO_SEC)
        printf(" and port %" PRIu16 " for DTLS", dtls_port);
    printf("\n");
    sol_init();

    if (sec_mode == SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY) {
        known_keys[0]->id = sol_blob_new_dup(CLIENT_SERVER_PSK_ID, sizeof(CLIENT_SERVER_PSK_ID) - 1);
        known_keys[0]->key = sol_blob_new_dup(CLIENT_SERVER_PSK_KEY, PSK_KEY_LEN);

        server = sol_lwm2m_server_new(coap_port, 1, dtls_port, sec_mode, known_keys);
    } else if (sec_mode == SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY) {
        r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
            sol_str_slice_from_str(CLIENT_PUBLIC_KEY), SOL_DECODE_BOTH);
        known_pub_keys[0] = sol_blob_new_dup(buf_aux, RPK_PUBLIC_KEY_LEN);

        r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
            sol_str_slice_from_str(SERVER_PRIVATE_KEY), SOL_DECODE_BOTH);
        my_rpk.private_key = sol_blob_new_dup(buf_aux, RPK_PRIVATE_KEY_LEN);
        r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
            sol_str_slice_from_str(SERVER_PUBLIC_KEY), SOL_DECODE_BOTH);
        my_rpk.public_key = sol_blob_new_dup(buf_aux, RPK_PUBLIC_KEY_LEN);

        server = sol_lwm2m_server_new(coap_port, 1, dtls_port, sec_mode, &my_rpk, known_pub_keys);
    } else { /* SOL_LWM2M_SECURITY_MODE_NO_SEC */
        server = sol_lwm2m_server_new(coap_port, 0);
    }
    if (!server) {
        r = -1;
        fprintf(stderr, "Could not create the LWM2M server\n");
        goto exit;
    }

    r = sol_lwm2m_server_add_registration_monitor(server, registration_cb,
        NULL);
    if (r < 0) {
        fprintf(stderr, "Could not add a registration monitor\n");
        goto exit_del;
    }

    sol_run();

    r = 0;
exit_del:
    sol_lwm2m_server_del(server);
exit:
    if (sec_mode == SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY) {
        sol_blob_unref(known_keys[0]->id);
        sol_blob_unref(known_keys[0]->key);
    } else if (sec_mode == SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY) {
        sol_blob_unref(known_pub_keys[0]);
        sol_blob_unref(my_rpk.private_key);
        sol_blob_unref(my_rpk.public_key);
    }
    sol_shutdown();
    return r;
}
