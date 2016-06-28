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

/**
 * @file
 * @brief HTTP Static files server
 *
 * Sample static files server. The path used to look for the files should be given
 * as argument when launching this sample. To see the usage help, -h or --help.
 */

#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "soletta.h"
#include "sol-http.h"
#include "sol-http-server.h"

static struct sol_http_server *server;

static void
startup_server(void)
{
    char **argv = sol_argv();
    char *dir = NULL;
    int port = 8080, c, opt_idx,  argc = sol_argc();
    static const struct option opts[] = {
        { "port", required_argument, NULL, 'p' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(argc, argv, "p:h", opts, &opt_idx)) != -1) {
        switch (c) {
        case 'p':
            port = atoi(optarg);
            break;
        case 'h':
        default:
            fprintf(stderr,
                "Usage:\n\t%s [-p <port >] <directory>\n", argv[0]);
            sol_quit_with_code(EXIT_SUCCESS);
            return;
        }
    }

    dir = argv[optind];
    if (!dir) {
        fprintf(stderr, "ERROR: missing directory, use -h for help\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    server = sol_http_server_new(&(struct sol_http_server_config) {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_SERVER_CONFIG_API_VERSION, )
        .port = port,
    });
    if (!server) {
        fprintf(stderr, "ERROR: Failed to create the server\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (sol_http_server_add_dir(server, "/", dir) < 0) {
        fprintf(stderr, "ERROR: Failed to add directory %s\n", dir);
        sol_quit_with_code(EXIT_FAILURE);
    }
}


static void
shutdown_server(void)
{
    if (server)
        sol_http_server_del(server);
}

SOL_MAIN_DEFAULT(startup_server, shutdown_server);
