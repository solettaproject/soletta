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
   To run: ./lwm2m-sample-bs-server [-p PORT] [-s SEC_MODE]
   For every LWM2M client that connects with the bootstrap server, the bootstrap
   server will send bootstrap information in order for that client to connect
   with the lwm2m-sample-server (through DTLS).
 */

#include "sol-lwm2m-bs-server.h"
#include "sol-mainloop.h"
#include "sol-vector.h"
#include "sol-util.h"

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

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

#define CLIENT_PUBLIC_KEY ("D055EE14084D6E0615599DB583913E4A3E4526A2704D61F27A4CCFBA9758EF9A" \
    "B418B64AFE8030DA1DDCF4F42E2F2631D043B1FB03E22F4D17DE43F9F9ADEE70")
#define BS_SERVER_PRIVATE_KEY ("9b7dfec20e49fe2cacf23fb21d06a8dc496530c695ec24cdf6c002ce44afa5fb")
#define BS_SERVER_PUBLIC_KEY ("cd4110e97bbd6e7e5a800028079d02915c70b915ea4596402098deea585eb7ad" \
    "f3e080487327f70758b13bc0583f4293d13288a0164a8e324779aa4f7ada26c1")

const char *known_clients[] = { "cli1", "cli2", NULL };

static struct sol_blob server_one_addr = {
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

static struct sol_blob psk_id_0 = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)CLIENT_SERVER_PSK_ID,
    .size = sizeof(CLIENT_SERVER_PSK_ID) - 1,
    .refcnt = 1
};

static struct sol_blob psk_key_0 = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)CLIENT_SERVER_PSK_KEY,
    .size = sizeof(CLIENT_SERVER_PSK_KEY) - 1,
    .refcnt = 1
};

static void
write_resource_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo, const char *path,
    enum sol_coap_response_code response_code)
{
    const char *name = sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo);
    int r;

    if (response_code != SOL_COAP_RESPONSE_CODE_CHANGED) {
        fprintf(stderr, "The client %s could not write the object(s)/resource at %s.\n",
            name, path);
        return;
    }

    printf("The client %s wrote the object(s)/resource at %s.\n", name, path);

    printf("Sending Bootstrap Finish to %s!\n", name);
    r = sol_lwm2m_bootstrap_server_send_finish(server, bs_cinfo);
    if (r < 0)
        fprintf(stderr, "Could not send Bootstrap Finish\n");
}

static void
write_servers_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo, const char *path,
    enum sol_coap_response_code response_code)
{
    const char *name = sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo);
    int r;
    struct sol_lwm2m_resource server_two_lifetime;

    if (response_code != SOL_COAP_RESPONSE_CODE_CHANGED) {
        fprintf(stderr, "The client %s could not write the object(s)/resource at %s.\n",
            name, path);
        return;
    }

    printf("The client %s wrote the object(s)/resource at %s.\n", name, path);

    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &server_two_lifetime, SERVER_OBJ_LIFETIME_RES_ID, LIFETIME * 2);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Lifetime] resource\n");
        return;
    }

    r = sol_lwm2m_bootstrap_server_write(server, bs_cinfo, "/1/0/1",
        &server_two_lifetime, 1, write_resource_cb, NULL);
    if (r < 0) {
        fprintf(stderr, "Could not send Bootstrap Write to /1/0/1\n");
    }

    sol_lwm2m_resource_clear(&server_two_lifetime);
}

static void
write_server_one_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo, const char *path,
    enum sol_coap_response_code response_code)
{
    const char *name = sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo);
    int r;
    uint16_t i, j;
    struct sol_lwm2m_resource server_two[3], server_three[3];
    struct sol_lwm2m_resource *servers[2] = {
        server_two, server_three
    };
    size_t servers_len[2] = {
        sol_util_array_size(server_two), sol_util_array_size(server_three)
    };
    uint16_t servers_ids[2] = {
        0, 4
    };

    if (response_code != SOL_COAP_RESPONSE_CODE_CHANGED) {
        fprintf(stderr, "The client %s could not write the object(s)/resource at %s.\n",
            name, path);
        return;
    }

    printf("The client %s wrote the object(s)/resource at %s.\n", name, path);

    // Server Two's Server Object
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &server_two[0], SERVER_OBJ_SHORT_RES_ID, 102);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Short Server ID] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &server_two[1], SERVER_OBJ_LIFETIME_RES_ID, LIFETIME);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Lifetime] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &server_two[2], SERVER_OBJ_BINDING_RES_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &binding);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Binding] resource\n");
        return;
    }

    // Server Three's Server Object
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &server_three[0], SERVER_OBJ_SHORT_RES_ID, 103);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Short Server ID] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &server_three[1], SERVER_OBJ_LIFETIME_RES_ID, LIFETIME);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Lifetime] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &server_three[2], SERVER_OBJ_BINDING_RES_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &binding);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Binding] resource\n");
        return;
    }

    r = sol_lwm2m_bootstrap_server_write_object(server, bs_cinfo, "/1",
        servers, servers_len, servers_ids, sol_util_array_size(servers), write_servers_cb, NULL);
    if (r < 0) {
        fprintf(stderr, "Could not send Bootstrap Write to /1\n");
    }

    for (i = 0; i < sol_util_array_size(servers); i++)
        for (j = 0; j < sol_util_array_size(server_two); j++)
            sol_lwm2m_resource_clear(&servers[i][j]);
}

static void
write_sec_one_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo, const char *path,
    enum sol_coap_response_code response_code)
{
    const char *name = sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo);
    int r;
    uint16_t i;
    struct sol_lwm2m_resource server_one[3];

    if (response_code != SOL_COAP_RESPONSE_CODE_CHANGED) {
        fprintf(stderr, "The client %s could not write the object(s)/resource at %s.\n",
            name, path);
        return;
    }

    printf("The client %s wrote the object(s)/resource at %s.\n", name, path);

    // Server One's Server Object
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &server_one[0], SERVER_OBJ_SHORT_RES_ID, 101);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Short Server ID] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &server_one[1], SERVER_OBJ_LIFETIME_RES_ID, LIFETIME);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Lifetime] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &server_one[2], SERVER_OBJ_BINDING_RES_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &binding);
    if (r < 0) {
        fprintf(stderr, "Could not init Server Object's [Binding] resource\n");
        return;
    }

    r = sol_lwm2m_bootstrap_server_write(server, bs_cinfo, "/1/0",
        server_one, sol_util_array_size(server_one), write_server_one_cb, NULL);
    if (r < 0) {
        fprintf(stderr, "Could not send Bootstrap Write to /1/0\n");
    }

    for (i = 0; i < sol_util_array_size(server_one); i++)
        sol_lwm2m_resource_clear(&server_one[i]);
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
    struct sol_lwm2m_resource sec_server_one[6];

    if (response_code != SOL_COAP_RESPONSE_CODE_DELETED) {
        fprintf(stderr, "The client %s could not delete the object at %s.\n",
            name, path);
        return;
    }

    printf("The client %s deleted the object at %s.\n", name, path);

    // Server One's Security Object
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &sec_server_one[0], SECURITY_SERVER_URI_RES_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &server_one_addr);
    if (r < 0) {
        fprintf(stderr, "Could not init Security Object's [Server URI] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &sec_server_one[1], SECURITY_IS_BOOTSTRAP_RES_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL, false);
    if (r < 0) {
        fprintf(stderr, "Could not init Security Object's [Bootstrap Server] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &sec_server_one[2],
        SECURITY_SECURITY_MODE_RES_ID, SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY);
    if (r < 0) {
        fprintf(stderr, "Could not init Security Object's [Security Mode] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &sec_server_one[3], SECURITY_PUBLIC_KEY_OR_IDENTITY_RES_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &psk_id_0);
    if (r < 0) {
        fprintf(stderr, "Could not init Security Object's [Public Key or Identity] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INIT(r, &sec_server_one[4], SECURITY_SECRET_KEY_RES_ID,
        SOL_LWM2M_RESOURCE_DATA_TYPE_STRING, &psk_key_0);
    if (r < 0) {
        fprintf(stderr, "Could not init Security Object's [Secret Key] resource\n");
        return;
    }
    SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(r, &sec_server_one[5], SECURITY_SERVER_ID_RES_ID, 102);
    if (r < 0) {
        fprintf(stderr, "Could not init Security Object's [Short Server ID] resource\n");
        return;
    }

    r = sol_lwm2m_bootstrap_server_write(server, bs_cinfo, "/0/0",
        sec_server_one, sol_util_array_size(sec_server_one), write_sec_one_cb, NULL);
    if (r < 0) {
        fprintf(stderr, "Could not send Bootstrap Write to /0/0\n");
    }

    for (i = 0; i < sol_util_array_size(sec_server_one); i++)
        sol_lwm2m_resource_clear(&sec_server_one[i]);
}

static void
bootstrap_cb(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo)
{
    const char *name = sol_lwm2m_bootstrap_client_info_get_name(bs_cinfo);
    int r;

    printf("Client-initiated Bootstrap from %s starting!\n", name);

    r = sol_lwm2m_bootstrap_server_delete_object_instance(server, bs_cinfo, "/",
        delete_all_cb, NULL);
    if (r < 0) {
        fprintf(stderr, "Could not send Bootstrap Delete to /\n");
    }
}

int
main(int argc, char *argv[])
{
    struct sol_lwm2m_bootstrap_server *server;
    uint16_t port = 5783, i;
    enum sol_lwm2m_security_mode sec_modes[2] = { };
    int r, sec_modes_len = 0;
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

    snprintf(usage, sizeof(usage), "Usage: ./lwm2m-sample-bs-server [-p PORT] -s SEC_MODE...\n"
        "Where default PORT=%" PRIu16 " and SEC_MODE is an integer as per:\n"
        "\tPRE_SHARED_KEY=%d\n"
        "\tRAW_PUBLIC_KEY=%d\n"
        "\tCERTIFICATE=%d\n\n"
        "For multiple SEC_MODEs, they must be passed in ascending order.\n",
        port,
        SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY,
        SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY,
        SOL_LWM2M_SECURITY_MODE_CERTIFICATE);

    while ((r = getopt(argc, argv, "p:s:")) != -1) {
        switch (r) {
        case 'p':
            port = atoi(optarg);
            break;
        case 's':
            if (sec_modes_len < 2) {
                sec_modes[sec_modes_len] = atoi(optarg);
                if (sec_modes[sec_modes_len] < 0 || sec_modes[sec_modes_len] > 2) {
                    fprintf(stderr, "%s", usage);
                    return -1;
                }
                sec_modes_len++;
            } else {
                fprintf(stderr, "%s", usage);
                return -1;
            }
            break;
        default:
            fprintf(stderr, "%s", usage);
            return -1;
        }
    }

    printf("Using port %" PRIu16 " for DTLS\n", port);
    sol_init();

    for (i = 0; i < sec_modes_len; i++) {
        if (sec_modes[i] == SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY) {
            known_keys[0]->id = sol_blob_new_dup(CLIENT_BS_PSK_ID, sizeof(CLIENT_BS_PSK_ID) - 1);
            known_keys[0]->key = sol_blob_new_dup(CLIENT_BS_PSK_KEY, PSK_KEY_LEN);
        } else if (sec_modes[i] == SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY) {
            r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
                sol_str_slice_from_str(CLIENT_PUBLIC_KEY), SOL_DECODE_BOTH);
            known_pub_keys[0] = sol_blob_new_dup(buf_aux, RPK_PUBLIC_KEY_LEN);

            r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
                sol_str_slice_from_str(BS_SERVER_PRIVATE_KEY), SOL_DECODE_BOTH);
            my_rpk.private_key = sol_blob_new_dup(buf_aux, RPK_PRIVATE_KEY_LEN);
            r = sol_util_base16_decode(buf_aux, sizeof(buf_aux),
                sol_str_slice_from_str(BS_SERVER_PUBLIC_KEY), SOL_DECODE_BOTH);
            my_rpk.public_key = sol_blob_new_dup(buf_aux, RPK_PUBLIC_KEY_LEN);
        }
    }

    if (sec_modes_len == 1) {
        if (sec_modes[0] == SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY)
            server = sol_lwm2m_bootstrap_server_new(port, known_clients,
                sec_modes_len, sec_modes[0], known_keys);
        else if (sec_modes[0] == SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY)
            server = sol_lwm2m_bootstrap_server_new(port, known_clients,
                sec_modes_len, sec_modes[0], &my_rpk, known_pub_keys);
        else {
            fprintf(stderr, "%s", usage);
            return -1;
        }
    } else if (sec_modes_len == 2) {
        server = sol_lwm2m_bootstrap_server_new(port, known_clients,
            sec_modes_len, sec_modes[0], known_keys,
            sec_modes[1], &my_rpk, known_pub_keys);
    } else {
        fprintf(stderr, "%s", usage);
        return -1;
    }
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
    for (i = 0; i < sec_modes_len; i++) {
        if (sec_modes[i] == SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY) {
            sol_blob_unref(known_keys[0]->id);
            sol_blob_unref(known_keys[0]->key);
        } else if (sec_modes[i] == SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY) {
            sol_blob_unref(known_pub_keys[0]);
            sol_blob_unref(my_rpk.private_key);
            sol_blob_unref(my_rpk.public_key);
        }
    }
    sol_shutdown();
    return r;
}
