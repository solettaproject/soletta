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
static struct sol_fd *stdin_watch;
static char *value;

static bool
on_stdin(void *data, int fd, uint32_t flags)
{
    ssize_t r;
    char buffer[512];

    if (flags & SOL_FD_FLAGS_ERR) {
        fprintf(stderr, "ERROR: Something wrong happened with file descriptor: %d\n", fd);
        goto err;
    }

    if (flags & SOL_FD_FLAGS_IN) {
        r = read(fd, buffer, sizeof(buffer) - 1);
        if (r  == -1) {
            if ((errno == EAGAIN) || (errno == EINTR))
                return true;
            fprintf(stderr,
                "ERROR: Failed to read file descriptor: %d\n", fd);
            goto err;
        }

        buffer[r] = '\0';
        free(value);
        value = strdup(buffer);
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
        .content = SOL_BUFFER_INIT_EMPTY,
        .param = SOL_HTTP_REQUEST_PARAMS_INIT,
        .response_code = SOL_HTTP_STATUS_OK
    };

    r = sol_buffer_append_slice(&response.content,
        sol_str_slice_from_str(value ?: "Soletta string server"));
    if (r < 0) {
        fprintf(stderr, "ERROR: Failed to append slice\n");
        goto err;
    }

    goto end;

err:
    response.response_code = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR;
end:
    r = sol_http_server_send_response(request, &response);
    sol_buffer_fini(&response.content);
    sol_http_params_clear(&response.param);

    return 0;
}

static void
startup_server(void)
{
    char **argv = sol_argv();
    int port = 80, i, argc = sol_argc();

    if (argc < 2) {
        fprintf(stderr,
            "Usage:\n\t%s [-p <port>]\n", argv[0]);
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
        }
    }

    stdin_watch = sol_fd_add(STDIN_FILENO,
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_ERR, on_stdin, NULL);
    if (!stdin_watch) {
        fprintf(stderr, "ERROR: Failed to watch stdin\n");
        goto err_watch;
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

    return;

err_handler:
err_server:
    sol_fd_del(stdin_watch);
err_watch:
    sol_quit_with_code(EXIT_FAILURE);
}


static void
shutdown_server(void)
{
    if (server)
        sol_http_server_del(server);
    free(value);
}

SOL_MAIN_DEFAULT(startup_server, shutdown_server);
