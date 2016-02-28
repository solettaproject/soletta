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
 * @file
 * @brief HTTP Server handler
 *
 * Sample server that return a text/plain string. The content of this
 * is read from stdin. To see the usage help, -h or --help.
 */


#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "sol-mainloop.h"
#include "sol-http.h"
#include "sol-http-server.h"
#include "sol-util.h"
#include "sol-util-file.h"

static struct sol_http_server *server;
static struct sol_fd *stdin_watch;
static struct sol_buffer value;

static bool
on_stdin(void *data, int fd, uint32_t flags)
{
    if (flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP)) {
        fprintf(stderr, "ERROR: Something wrong happened with file descriptor: %d\n", fd);
        goto err;
    }

    if (flags & SOL_FD_FLAGS_IN) {
        int err;

        value.used = 0;
        /* this will loop trying to read as much data as possible to buffer. */
        err = sol_util_load_file_fd_buffer(fd, &value);
        if (err < 0) {
            fprintf(stderr, "ERROR: failed to read from stdin: %s\n", sol_util_strerrora(-err));
            goto err;
        }

        if (value.used == 0) {
            /* no data usually means ^D on the terminal, quit the application */
            puts("no data on stdin, quitting.");
            sol_quit();
        } else {
            printf("Now serving %zd bytes:\n--BEGIN--\n%.*s\n--END--\n",
                value.used, SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&value)));
        }
    }

    return true;

err:
    sol_quit_with_code(EXIT_FAILURE);
    return false;
}

static int
request_cb(void *data, struct sol_http_request *request)
{
    int r;
    struct sol_http_response response = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
        .param = SOL_HTTP_REQUEST_PARAMS_INIT,
        .response_code = SOL_HTTP_STATUS_OK
    };

    response.content = value;

    r = sol_http_param_add(&response.param,
        SOL_HTTP_REQUEST_PARAM_HEADER("Content-Type", "text/plain"));
    if (r < 0)
        fprintf(stderr, "ERROR: Could not set the 'Content-Type' header\n");

    r = sol_http_server_send_response(request, &response);
    sol_http_params_clear(&response.param);

    return r;
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
    while ((c = getopt_long(argc, argv, "p:h", opts, &opt_idx)) != -1) {
        switch (c) {
        case 'p':
            port = atoi(optarg);
            break;
        case 'h':
        default:
            fprintf(stderr,
                "Usage:\n\t%s [-p <port >]\n", argv[0]);
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
        goto err_watch;
    }

    stdin_watch = sol_fd_add(STDIN_FILENO,
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_ERR, on_stdin, NULL);
    if (!stdin_watch) {
        fprintf(stderr, "ERROR: Failed to watch stdin\n");
        goto err_watch;
    }

    c = sol_buffer_set_slice(&value,
        sol_str_slice_from_str("Soletta string server, set the value using the keyboard"));
    if (c < 0) {
        fprintf(stderr, "ERROR: Failed to set buffer's value\n");
        goto err_buffer;
    }

    server = sol_http_server_new(port);
    if (!server) {
        fprintf(stderr, "ERROR: Failed to create the server\n");
        goto err_server;
    }

    if (sol_http_server_register_handler(server, "/", request_cb, NULL) < 0) {
        fprintf(stderr, "ERROR: Failed to register the handler\n");
        goto err_handler;
    }

    printf("HTTP server at port %d.\nDefault reply set to '%.*s'\n",
        port, SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&value)));

    return;

err_handler:
err_server:
    sol_buffer_fini(&value);
err_buffer:
    sol_fd_del(stdin_watch);
err_watch:
    sol_quit_with_code(EXIT_FAILURE);
}


static void
shutdown_server(void)
{
    if (server)
        sol_http_server_del(server);
    sol_buffer_fini(&value);
}

SOL_MAIN_DEFAULT(startup_server, shutdown_server);
