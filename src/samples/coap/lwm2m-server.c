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

/*
   To run: ./lwm2m-sample-server
   For every LWM2M client that connects with the server, the server will
   try to create an LWM2M location object if it does not exist.
   After that, it will observe the location object.
 */

#include "sol-lwm2m.h"
#include "sol-mainloop.h"
#include "sol-vector.h"
#include "sol-util.h"

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>

#define LOCATION_OBJ_ID (6)
#define LONGITUDE_ID (1)
#define LATITUDE_ID (0)
#define TIMESTAMP_ID (5)

enum location_object_status {
    LOCATION_OBJECT_NOT_FOUND,
    LOCATION_OBJECT_WITH_NO_INSTANCES,
    LOCATION_OBJECT_WITH_INSTANCES
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
    sol_coap_responsecode_t response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content)
{
    struct sol_vector tlvs;
    const char *name = sol_lwm2m_client_info_get_name(cinfo);
    int r;
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;

    if (response_code != SOL_COAP_RSPCODE_CHANGED &&
        response_code != SOL_COAP_RSPCODE_CONTENT) {
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
        uint8_t *bytes;
        uint16_t len;

        if (tlv->id == LATITUDE_ID)
            prop = "latitude";
        else if (tlv->id == LONGITUDE_ID)
            prop = "longitude";
        else
            continue;

        r = sol_lwm2m_tlv_get_bytes(tlv, &bytes, &len);
        if (r < 0) {
            fprintf(stderr, "Could not the %s value from client %s\n",
                prop, name);
            break;
        }

        printf("Client %s %s is %.*s\n", name, prop, (int)len, bytes);
    }

    sol_lwm2m_tlv_array_clear(&tlvs);
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
    sol_coap_responsecode_t response_code)
{
    const char *name = sol_lwm2m_client_info_get_name(cinfo);

    if (response_code != SOL_COAP_RSPCODE_CREATED) {
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
    SOL_LWM2M_RESOURCE_INIT(r, &res[0], LATITUDE_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING,
        sol_str_slice_from_str("48.858093"));

    if (r < 0) {
        fprintf(stderr, "Could init the latitude resource\n");
        return;
    }

    SOL_LWM2M_RESOURCE_INIT(r, &res[1], LONGITUDE_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING,
        sol_str_slice_from_str("2.294694"));

    if (r < 0) {
        fprintf(stderr, "Could not init the longitude resource\n");
        return;
    }

    SOL_LWM2M_RESOURCE_INIT(r, &res[2], TIMESTAMP_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_TIME,
        (int64_t)time(NULL));

    if (r < 0) {
        fprintf(stderr, "Could not init the longitude resource\n");
        return;
    }

    r = sol_lwm2m_server_management_create(server, cinfo, "/6", res,
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
    uint16_t port = SOL_LWM2M_DEFAULT_SERVER_PORT;
    int r;

    printf("Using the default LWM2M port (%" PRIu16 ")\n", port);
    sol_init();

    server = sol_lwm2m_server_new(port);
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
    sol_shutdown();
    return r;
}
