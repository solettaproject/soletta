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

/**
 * @file
 * @brief HTTP Server sse
 *
 * Sample server that return a response and keep it alive.
 * It implements server sent events. The content of this
 * is read from stdin. To see the usage help, -h or --help.
 */


#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "soletta.h"
#include "sol-http.h"
#include "sol-http-server.h"
#include "sol-util.h"
#include "sol-util-file.h"

static struct sol_http_server *server;
static struct sol_fd *stdin_watch;
static struct sol_buffer value = SOL_BUFFER_INIT_EMPTY;
static struct sol_ptr_vector responses = SOL_PTR_VECTOR_INIT;

static bool
on_stdin(void *data, int fd, uint32_t flags)
{
    uint16_t i;
    struct sol_http_response_sse *sse;

    if (flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP)) {
        fprintf(stderr, "ERROR: Something wrong happened with file descriptor: %d\n", fd);
        goto err;
    }

    if (flags & SOL_FD_FLAGS_IN) {
        int err;

        value.used = 0;
        err = sol_buffer_insert_bytes(&value, 0,
            (uint8_t *)"data: ", sizeof("data: ") - 1);
        if (err < 0) {
            fprintf(stderr, "ERROR: failed to append the data prefix: %s\n",
                sol_util_strerrora(-err));
            goto err;
        }
        /* this will loop trying to read as much data as possible to buffer. */
        err = sol_util_load_file_fd_buffer(fd, &value);
        if (err < 0) {
            fprintf(stderr, "ERROR: failed to read from stdin: %s\n",
                sol_util_strerrora(-err));
            goto err;
        }

        if (value.used == 0) {
            /* no data usually means ^D on the terminal, quit the application */
            printf("no data on stdin, quitting.\n");
            sol_quit();
        } else {
            err = sol_buffer_append_bytes(&value, (uint8_t *)"\n\n", sizeof("\n\n") - 1);
            if (err < 0) {
                fprintf(stderr, "ERROR: Could not append data to buffer: %s\n",
                    sol_util_strerrora(-err));
                goto err;
            }
        }
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&responses, sse, i)
        sol_http_response_sse_send_data(sse, &value);

    return true;

err:
    sol_quit_with_code(EXIT_FAILURE);
    stdin_watch = NULL;
    return false;
}

static void
delete_cb(void *data, const struct sol_http_response_sse *sse)
{
    sol_ptr_vector_remove(&responses, sse);
}

static int
request_cb(void *data, struct sol_http_request *request)
{
    int ret;
    struct sol_http_response_sse *sse;

    sse = sol_http_server_send_response_sse(request, delete_cb, NULL);
    if (!sse)
        return -1;

    ret = sol_ptr_vector_append(&responses, sse);
    if (ret < 0) {
        sol_http_response_sse_del(sse);
        return ret;
    }

    return 0;
}

static void
startup_server(void)
{
    char **argv = sol_argv();
    int port = 8080, c, opt_idx,  argc = sol_argc();
    static const struct option opts[] = {
        { "port", required_argument, NULL, 'p' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    sol_buffer_init(&value);
    while ((c = getopt_long(argc, argv, "p:h", opts,
            &opt_idx)) != -1) {
        switch (c) {
        case 'p':
            port = atoi(optarg);
            break;
        case 'h':
        default:
            fprintf(stderr,
                "Usage:\n\t%s [-p <port >]\n\n"
                "Then everything that is typed will be sent using SSE technique\n",
                argv[0]);
            sol_quit_with_code(EXIT_SUCCESS);
            return;
        }
    }

    /* always set stdin to non-block before we use sol_fd_add() on it,
     * otherwise we may block reading and it would impact the main
     * loop dispatching other events.
     */
    if (sol_util_fd_set_flag(STDIN_FILENO, O_NONBLOCK) < 0) {
        fprintf(stderr, "ERROR: cannot set stdin to non-block.\n");
        goto err;
    }

    stdin_watch = sol_fd_add(STDIN_FILENO,
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_ERR, on_stdin, NULL);
    if (!stdin_watch) {
        fprintf(stderr, "ERROR: Failed to watch stdin\n");
        goto err;
    }

    server = sol_http_server_new(port);
    if (!server) {
        fprintf(stderr, "ERROR: Failed to create the server\n");
        goto err;
    }

    if (sol_http_server_register_handler(server, "/",
        request_cb, NULL) < 0) {
        fprintf(stderr, "ERROR: Failed to register the handler\n");
        goto err;
    }

    printf("HTTP server at port %d.\nDefault reply set to '%.*s'\n",
        port, SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&value)));

    return;

err:
    sol_quit_with_code(EXIT_FAILURE);
}

static void
shutdown_server(void)
{
    uint16_t i;
    struct sol_http_response_sse *sse;

    if (stdin_watch)
        sol_fd_del(stdin_watch);
    if (server)
        sol_http_server_del(server);

    sol_buffer_fini(&value);
    SOL_PTR_VECTOR_FOREACH_IDX (&responses, sse, i)
        sol_http_response_sse_del(sse);

    sol_ptr_vector_clear(&responses);
}

SOL_MAIN_DEFAULT(startup_server, shutdown_server);
