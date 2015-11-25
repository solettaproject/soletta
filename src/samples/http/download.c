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

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "sol-mainloop.h"
#include "sol-http.h"
#include "sol-http-client.h"

static FILE *fd;
struct sol_http_client_connection *pending;

static size_t
recv_func(void *userdata, const struct sol_http_client_connection *connection,
    struct sol_buffer *buffer)
{
    ssize_t ret;

    ret = fwrite(buffer->data, buffer->used, 1, fd);
    if (!ret || ferror(fd)) {
        fprintf(stderr, "ERROR: Failed to write\n");
    }

    return ret * buffer->used;
}

static void
response_func(void *userdata, const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    fclose(fd);
    fd = NULL;
    pending = NULL;

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        fprintf(stderr, "ERROR: Finished with error, response code: %d\n", response->response_code);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    printf("Download concluded successfully\n");
    sol_quit_with_code(EXIT_SUCCESS);
}

static const struct sol_http_request_interface iface = {
    SOL_SET_API_VERSION(.api_version = SOL_HTTP_REQUEST_INTERFACE_API_VERSION, )
    .recv_cb = recv_func,
    .response_cb = response_func
};

static void
startup(void)
{
    char **argv = sol_argv();
    char *output = NULL, *url = NULL;
    int i, argc = sol_argc();

    if (argc < 2) {
        fprintf(stderr,
            "Usage:\n\t%s [-o <output_file>] <url>\n", argv[0]);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'o') {
                if (i + 1 < argc) {
                    output = argv[i + 1];
                    i++;
                    continue;
                } else {
                    fprintf(stderr, "ERROR: argument -o missing value.\n");
                }
            } else {
                fprintf(stderr, "ERROR: unknown option %s.\n", argv[i]);
            }

            sol_quit_with_code(EXIT_FAILURE);
            return;
        } else {
            url = argv[i];
        }
    }

    if (!url) {
        fprintf(stderr, "ERROR: missing url.\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (output) {
        fd = fopen(output, "w");
        if (fd == NULL) {
            fprintf(stderr, "ERROR: Failed to create the file: %s\n", output);
            sol_quit_with_code(EXIT_FAILURE);
            return;
        }
    } else {
        fd = stdout;
    }

    pending = sol_http_client_request_with_interface(SOL_HTTP_METHOD_GET,
        url, NULL, &iface, NULL);
    if (!pending) {
        fprintf(stderr, "ERROR: Failed to create the request\n");
        fclose(fd);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }
}


static void
shutdown(void)
{
    if (pending)
        sol_http_client_connection_cancel(pending);
    if (fd)
        fclose(fd);
}

SOL_MAIN_DEFAULT(startup, shutdown);
