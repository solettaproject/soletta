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

/**
 * This sample will wait for connections of LWM2M clients.
 * After a LWM2M client is connected, it will create a server instance in it,
 * write some data, observe it and delete the created instance.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "sol-lwm2m.h"
#include "sol-mainloop.h"
#include "sol-coap.h"

#define TEN_SECONDS (10000)

static struct sol_timeout *timeout = NULL;

static void
management_reply_cb(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    sol_coap_responsecode_t code,
    void *data);

static const char *
binding_as_string(enum sol_lwm2m_binding_mode mode)
{
    switch (mode) {
    case SOL_LWM2M_BINDING_MODE_U:
        return "U";
    case SOL_LWM2M_BINDING_MODE_UQ:
        return "UQ";
    case SOL_LWM2M_BINDING_MODE_S:
        return "S";
    case SOL_LWM2M_BINDING_MODE_SQ:
        return "SQ";
    case SOL_LWM2M_BINDING_MODE_US:
        return "US";
    case SOL_LWM2M_BINDING_MODE_UQS:
        return "UQS";
    default:
        return "Unknown";
    }
}

static void
show_client_info(const struct sol_lwm2m_server *server,
    const struct sol_lwm2m_client_info *client)
{
    const struct sol_vector *objects, *instances;
    uint16_t *instance;
    struct sol_lwm2m_client_object *object;
    uint16_t i, j, obj_id;
    uint32_t lf;
    const char *name;

    name = sol_lwm2m_client_info_get_name(client);
    sol_lwm2m_client_info_get_lifetime(client, &lf);

    printf("--- LWM2M Client info --- \n");
    printf("Name: %s\n", name);
    printf("SMS: %s\n", sol_lwm2m_client_info_get_sms(client));
    printf("Location-path:%s\n", sol_lwm2m_client_info_get_location(client));
    printf("Objects path:%s\n", sol_lwm2m_client_info_get_objects_path(client));
    printf("Lifetime:%" PRIu32 "\n", lf);
    printf("Binding: %s\n",
        binding_as_string(sol_lwm2m_client_info_get_binding_mode(client)));

    objects = sol_lwm2m_client_info_get_objects(client);

    SOL_VECTOR_FOREACH_IDX (objects, object, i) {
        instances = sol_lwm2m_client_object_get_instances(object);
        sol_lwm2m_client_object_get_id(object, &obj_id);
        printf("Object ID (%" PRIu16 ") instances\n", obj_id);
        SOL_VECTOR_FOREACH_IDX (instances, instance, j) {
            printf("Instance ID: %" PRIu16 "\n", *instance);
        }
    }
}

static void
delete_instance(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client)
{
    int r;

    r = sol_lwm2m_server_management_delete(server, client, "/1/10",
        management_reply_cb, NULL);

    printf("Deleting server instance at path '/1/10 on '%s'\n",
        sol_lwm2m_client_info_get_name(client));

    if (r < 0) {
        fprintf(stderr, "Could not send a message to delete object '/0/0' to client '%s'\n",
            sol_lwm2m_client_info_get_name(client));
    }
}

static void
execute_resource(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client)
{

    int r;

    r = sol_lwm2m_server_management_execute(server, client, "/1/10/8", NULL,
        management_reply_cb, NULL);

    printf("Executing resource '/1/10/8' on '%s'\n",
        sol_lwm2m_client_info_get_name(client));

    if (r < 0) {
        fprintf(stderr, "Could not send a message to obserse '/3/0' to client '%s'\n",
            sol_lwm2m_client_info_get_name(client));
    }
}

static void
write_resource(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client)
{
    struct sol_lwm2m_resource resource;
    int r;

    if (sol_lwm2m_resource_init(&resource, 1, SOL_LWM2M_RESOURCE_TYPE_INT, 20) < 0) {
        fprintf(stderr, "Could not init the resource\n");
        return;
    }

    r = sol_lwl2m_server_management_write(server, client, "/1/10", &resource, 1,
        false, management_reply_cb, NULL);

    printf("Writing a new server id at '/1/10' for '%s'\n",
        sol_lwm2m_client_info_get_name(client));

    if (r < 0) {
        fprintf(stderr, "Could not send a message to write at path '/1/10' on client '%s'\n",
            sol_lwm2m_client_info_get_name(client));
    }
}

static void
content_cb(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    sol_coap_responsecode_t response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content, void *data)
{
    struct sol_lwm2m_tlv *tlv;
    uint16_t i;
    struct sol_vector tlvs;
    const char *name;
    int r;

    name = sol_lwm2m_client_info_get_name(client);

    printf("Client '%s' Path '%s' content\n", name, path);

    if (response_code != SOL_COAP_RSPCODE_CONTENT) {
        fprintf(stderr, "Invalid content response '%d'\n", response_code);
        return;
    }

    printf("Content type: %d\n", content_type);
    if (content_type == SOL_LWM2M_CONTENT_TYPE_TLV) {
        printf("Content is TLV type\n");
        r = sol_lwm2m_parse_tlv(content, &tlvs);
        if (r < 0)
            fprintf(stderr, "Could not parse tlv. Path '%s' Client '%s'", path, name);
        else {
            SOL_VECTOR_FOREACH_IDX (&tlvs, tlv, i) {
                if (tlv->id == 7)
                    printf("Binding: %.*s\n", SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&tlv->content)));
                else if (tlv->id == 1) {
                    int64_t t;
                    r = sol_lwl2m_tlv_to_int(tlv, &t);
                    if (r < 0)
                        fprintf(stderr, "Could not extract int from the TLV\n");
                    else
                        printf("Lifetime: %" PRId64 "\n", t);
                    break;
                }
            }
            sol_lwm2m_tlv_array_clear(&tlvs);
            write_resource(server, client);
        }
    } else {
        printf("Update method: %.*s\n", SOL_STR_SLICE_PRINT(content));
        execute_resource(server, client);
    }
}

static void
read_resource(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client)
{
    int r;

    r = sol_lwm2m_server_management_read(server, client, "/1/10/8",
        content_cb, NULL);

    printf("Reading resource /1/10/8 on '%s'\n",
        sol_lwm2m_client_info_get_name(client));

    if (r < 0) {
        fprintf(stderr, "Could not send a message to read resource '/3/0/0' to client '%s'\n",
            sol_lwm2m_client_info_get_name(client));
    }
}

static void
observe_object(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client)
{
    int r;

    r = sol_lwm2m_server_add_observer(server, client, "/1/10",
        content_cb, NULL);

    printf("Observing object instance /1/10 on '%s'\n",
        sol_lwm2m_client_info_get_name(client));

    if (r < 0) {
        fprintf(stderr, "Could not send a message to observe '/3/0' to client '%s'\n",
            sol_lwm2m_client_info_get_name(client));
    }
}

static void
management_reply_cb(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    sol_coap_responsecode_t code,
    void *data)
{
    const char *name;

    name = sol_lwm2m_client_info_get_name(client);

    if (code == SOL_COAP_RSPCODE_DELETED) {
        printf("Object instance '%s' deleted for client '%s'\n", path, name);
        sol_quit();
    } else if (code == SOL_COAP_RSPCODE_CREATED) {
        printf("Path Object instance of '%s' for client '%s' created\n", path, name);
        read_resource(server, client);
    } else if (code == SOL_COAP_RSPCODE_CHANGED && !strcmp(path, "/1/10/8")) {
        printf("The path '%s' in client '%s' has been executed\n", path, name);
        observe_object(server, client);
    } else if (code == SOL_COAP_RSPCODE_CHANGED && !strcmp(path, "/1/10")) {
        printf("The path '%s' in client '%s' has been written\n", path, name);
        delete_instance(server, client);
    } else
        fprintf(stderr, "Code: '%d' not expected for path '%s' and client '%s'\n", code, path, name);
}

static void
create_server_instance(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client)
{
    int r;
    struct sol_lwm2m_resource resources[5];

    if (sol_lwm2m_resource_init(&resources[0], 0, SOL_LWM2M_RESOURCE_TYPE_INT, 10) < 0 ||
        sol_lwm2m_resource_init(&resources[1], 1, SOL_LWM2M_RESOURCE_TYPE_INT, 2) < 0 ||
        sol_lwm2m_resource_init(&resources[2], 6, SOL_LWM2M_RESOURCE_TYPE_BOOLEAN, false) < 0 ||
        sol_lwm2m_resource_init(&resources[3], 7, SOL_LWM2M_RESOURCE_TYPE_STRING, "U") < 0 ||
        sol_lwm2m_resource_init(&resources[4], 8, SOL_LWM2M_RESOURCE_TYPE_STRING, "Lwm2mDevKit.Registration.update();") < 0) {
        fprintf(stderr, "Could not add the string resources\n");
        return;
    }

    r = sol_lwm2m_server_management_create(server, client, "/1/10",
        resources, 5, management_reply_cb, NULL);

    printf("Creating server object instance in '%s' with path '/1/10'\n",
        sol_lwm2m_client_info_get_name(client));

    if (r < 0) {
        fprintf(stderr, "Could not send a message to create an instance object '/1' to client '%s'\n",
            sol_lwm2m_client_info_get_name(client));
    }
}

static bool
show_all_clients(void *data)
{
    struct sol_lwm2m_server *server = data;
    uint16_t i;
    struct sol_lwm2m_client_info *client;
    const struct sol_ptr_vector *clients;

    printf("Displaying all connected clients\n");
    clients = sol_lwm2m_server_get_clients(server);

    SOL_PTR_VECTOR_FOREACH_IDX (clients, client, i)
        create_server_instance(server, client);

    return true;
}

static void
registration_event_cb(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    enum sol_lwm2m_registration_event event, void *data)
{
    if (event == SOL_LWM2M_REGISTRATION_EVENT_REGISTER) {
        printf("Client '%s' registered\n",
            sol_lwm2m_client_info_get_name(client));
        show_client_info(server, client);
    } else if (event == SOL_LWM2M_REGISTRATION_EVENT_UPDATE) {
        printf("Client '%s' updated\n", sol_lwm2m_client_info_get_name(client));
    } else if (event == SOL_LWM2M_REGISTRATION_EVENT_UNREGISTER)
        printf("Client '%s' was unregistered\n",
            sol_lwm2m_client_info_get_name(client));
    else
        printf("Client '%s' timeout!\n", sol_lwm2m_client_info_get_name(client));
}

int
main(int argc, char *argv[])
{
    struct sol_lwm2m_server *server;
    int r = -1;

    sol_init();
    server = sol_lwm2m_server_new(SOL_LWM2M_DEFAULT_SERVER_PORT);

    if (!server) {
        fprintf(stderr, "Could not create the server\n");
        goto err_exit;
    }

    timeout = sol_timeout_add(TEN_SECONDS, show_all_clients, server);

    if (!timeout) {
        fprintf(stderr, "Could not create a timeout\n");
        sol_lwm2m_server_del(server);
        goto err_timeout;
    }

    r = sol_lwm2m_server_add_registration_monitor(server,
        registration_event_cb, NULL);

    if (r < 0) {
        fprintf(stderr, "Could not add a registration monitor\n");
        goto err_registration;
    }

    sol_run();
    r = 0;

err_registration:
    sol_timeout_del(timeout);
err_timeout:
    sol_lwm2m_server_del(server);
err_exit:
    sol_shutdown();
    return r;
}
