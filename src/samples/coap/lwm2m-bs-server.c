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
   To run: ./lwm2m-sample-bs-server
   For every LWM2M client that connects with the bootstrap server, the bootstrap
   server will send bootstrap information in order for that client to connect
   with the lwm2m-sample-server.
 */

#include "sol-lwm2m.h"
#include "sol-mainloop.h"
#include "sol-vector.h"
#include "sol-util.h"

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>

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

const char *known_clients[] = { "cli1", "cli2", NULL };

static struct sol_blob server_one_addr = {
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

static void
write_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo, const char *path,
    enum sol_coap_response_code response_code)
{
    const char *name = sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo);

    if (response_code != SOL_COAP_RESPONSE_CODE_CHANGED) {
        fprintf(stderr, "The client %s could not write the object/resource at %s.\n",
            name, path);
        return;
    }

    printf("The client %s wrote the object/resource at %s.\n", name, path);

    sol_lwm2m_bootstrap_server_send_finish(server, bs_cinfo);
}

static void
delete_all_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo, const char *path,
    enum sol_coap_response_code response_code)
{
    const char *name = sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo);
    int r;
    uint16_t i;
    struct sol_lwm2m_resource sec_server_one[3];
    struct sol_lwm2m_resource server_one[3];

    if (response_code != SOL_COAP_RESPONSE_CODE_DELETED) {
        fprintf(stderr, "The client %s could not delete the object at %s.\n",
            name, path);
        return;
    }

    printf("The client %s deleted the object at %s.\n", name, path);

    // Server One's Security Object
    SOL_LWM2M_RESOURCE_INIT(r, &sec_server_one[0], SECURITY_SERVER_SERVER_URI_RES_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &server_one_addr);
    if (r < 0) {
        fprintf(stderr, "Could not init Security Object's [Server URI] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_INIT(r, &sec_server_one[1], SECURITY_SERVER_IS_BOOTSTRAP_RES_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, false);
    if (r < 0) {
        fprintf(stderr, "Could not init Security Object's [Bootstrap Server] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_INT_INIT(r, &sec_server_one[2], SECURITY_SERVER_SERVER_ID_RES_ID, 101);
    if (r < 0) {
        fprintf(stderr, "Could not init Security Object's [Short Server ID] resource\n");
        return;
    }

    sol_lwm2m_bootstrap_server_write(server, bs_cinfo, "/0/0",
        sec_server_one, sol_util_array_size(sec_server_one), NULL, NULL);

    // Server One's Server Object
    SOL_LWM2M_RESOURCE_INT_INIT(r, &server_one[0], SERVER_OBJ_SHORT_RES_ID, 101);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Short Server ID] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_INT_INIT(r, &server_one[1], SERVER_OBJ_LIFETIME_RES_ID, LIFETIME);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Lifetime] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_INIT(r, &server_one[2], SERVER_OBJ_BINDING_RES_ID, 1,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &binding);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Binding] resource\n");
        return;
    }

    sol_lwm2m_bootstrap_server_write(server, bs_cinfo, "/1/0",
        server_one, sol_util_array_size(server_one), write_cb, NULL);

    for (i = 0; i < sol_util_array_size(sec_server_one); i++)
        sol_lwm2m_resource_clear(&sec_server_one[i]);

    for (i = 0; i < sol_util_array_size(server_one); i++)
        sol_lwm2m_resource_clear(&server_one[i]);
}

static void
bootstrap_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo)
{
    const char *name = sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo);

    printf("Client-initiated Bootstrap from %s starting!\n", name);

    sol_lwm2m_bootstrap_server_delete_object_instance(server, bs_cinfo, "/",
        delete_all_cb, NULL);
}

int
main(int argc, char *argv[])
{
    struct sol_lwm2m_bootstrap_server *server;
    uint16_t port = 5783;
    int r;

    printf("Using port %" PRIu16 "\n", port);
    sol_init();

    server = sol_lwm2m_bootstrap_server_new(port, known_clients);
    if (!server) {
        r = -1;
        fprintf(stderr, "Could not create the LWM2M bootstrap server\n");
        goto exit;
    }

    r = sol_lwm2m_bootstrap_server_add_request_monitor(server, bootstrap_cb,
        NULL);
    if (r < 0) {
        fprintf(stderr, "Could not add a bootstrap monitor\n");
        goto exit_del;
    }

    sol_run();

    r = 0;
exit_del:
    sol_lwm2m_bootstrap_server_del(server);
exit:
    sol_shutdown();
    return r;
}
