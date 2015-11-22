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

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sol-mainloop.h"
#include "sol-http.h"
#include "sol-http-server.h"

static struct sol_http_server *server;

static void
startup_server(void)
{
    char **argv = sol_argv();
    char *dir = NULL;
    int port = 80, i, argc = sol_argc();

    if (argc < 2) {
        fprintf(stderr,
            "Usage:\n\t%s [-p <port>] <directory>\n", argv[0]);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'p') {
                if (i + 1 < argc) {
                    port = atoi(argv[i + 1]);
                    i++;
                    continue;
                } else {
                    fprintf(stderr, "ERROR: argument -p missing value.\n");
                }
            } else {
                fprintf(stderr, "ERROR: unknown option %s.\n", argv[i]);
            }

            sol_quit_with_code(EXIT_FAILURE);
            return;
        } else {
            dir = argv[i];
        }
    }

    if (!dir) {
        fprintf(stderr, "ERROR: missing directory.\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    server = sol_http_server_new(port);
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
