/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

/**
 * @file
 * @brief HTTPS sample
 *
 * Basic HTTPS server. It uses a given certificate to crypto its contents.
 *
 * How to generate a certificate:
 *
 * openssl req -new > new.cert.csr
 * openssl rsa -in privkey.pem -out new.cert.key
 * openssl x509 -in new.cert.csr -out new.cert.cert -req -signkey new.cert.key -days 3652 -sha1
 *
 * To test it:
 *
 * run ./server-https -p 8080 -d "Hello HTTPS" -c new.cert.cert -k new,.cert.key
 *
 * open the browser and go to address https://your-ip:the-given-port
 * The browser will complaint about the certificate, it's ok add it as exception.
 *
 * To see the usage help, -h or --help.
 */


#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "soletta.h"
#include "sol-http.h"
#include "sol-http-server.h"


static struct sol_cert *cert, *key;
static struct sol_http_server *server;
static char *server_data;

static int
request_cb(void *data, struct sol_http_request *request)
{
    int r;
    struct sol_http_response response = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
        .param = SOL_HTTP_REQUEST_PARAMS_INIT,
        .response_code = SOL_HTTP_STATUS_OK,
        .content = SOL_BUFFER_INIT_CONST(server_data, strlen(server_data)),
    };

    r = sol_http_server_send_response(request, &response);
    sol_buffer_fini(&response.content);

    return r;
}

static void
startup_server(void)
{
    char **argv = sol_argv();
    int port = 8080;
    int c, opt_idx,  argc = sol_argc();
    static const struct option opts[] = {
        { "port", required_argument, NULL, 'p' },
        { "certificate", required_argument, NULL, 'c' },
        { "key", required_argument, NULL, 'k' },
        { "data", required_argument, NULL, 'd' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(argc, argv, "p:c:k:d:h", opts,
            &opt_idx)) != -1) {
        switch (c) {
        case 'p':
            port = atoi(optarg);
            break;
        case 'c':
            cert = sol_cert_load_from_id(optarg);
            if (!cert)
                goto err;
            break;
        case 'k':
            key = sol_cert_load_from_id(optarg);
            if (!key)
                goto err;
            break;
        case 'd':
            server_data = optarg;
            break;
        case 'h':
        default:
            fprintf(stderr,
                "Usage:\n\t%s [-p <port >]\n"
                "\t [-c <certificate to use>]\n"
                "\t [-k <certificate key>]\n"
                "\t [-d <data to serve>]\n",
                argv[0]);
            sol_quit_with_code(EXIT_SUCCESS);
            return;
        }
    }

    if (!server_data) {
        fprintf(stderr, "ERROR: No data was given. Run with -h or --help for help\n");
        sol_cert_unref(cert);
        sol_cert_unref(key);
        goto err;
    }

    server = sol_http_server_new(&(struct sol_http_server_config) {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_SERVER_CONFIG_API_VERSION, )
        .port = port,
        .security = {
            .cert = cert,
            .key = key,
        }
    });

    sol_cert_unref(cert);
    sol_cert_unref(key);

    if (!server) {
        fprintf(stderr, "ERROR: Failed to create the server\n");
        goto err;
    }

    if (sol_http_server_register_handler(server, "/",
        request_cb, NULL) < 0) {
        fprintf(stderr, "ERROR: Failed to register the handler\n");
        goto err;
    }

    return;

err:
    sol_quit_with_code(EXIT_FAILURE);
}

static void
shutdown_server(void)
{
    if (server)
        sol_http_server_del(server);
}

SOL_MAIN_DEFAULT(startup_server, shutdown_server);
