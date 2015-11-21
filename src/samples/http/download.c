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
#include "sol-http-client.h"

static void
write_func(void *userdata, const struct sol_http_client_connection *connection,
    struct sol_buffer *buffer)
{
    int fd = (int)(long)userdata;
    void *data;
    size_t len;
    ssize_t ret;

    data = sol_buffer_steal(buffer, &len);
    ret = write(fd, data, len);
    if (ret != (ssize_t)len) {
        fprintf(stderr, "ERROR: Failed to write, just have written: %d\n", (int)ret);
    }

    free(data);
}

static void
response_func(void *userdata, const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    int fd = (int)(long)userdata;

    close(fd);
    if (response->response_code != SOL_HTTP_STATUS_OK) {
        fprintf(stderr, "Finished with error, response code: %d\n", response->response_code);
        goto err;
    }

    fprintf(stdout, "Download concluded successfully\n");
    sol_quit_with_code(EXIT_SUCCESS);
    return;

err:
    sol_quit_with_code(EXIT_FAILURE);
}

static struct sol_http_request_interface iface = {
    .write_cb = write_func,
    .response_cb = response_func
};

static void
startup(void)
{
    struct sol_http_client_connection *pending;
    char **argv = sol_argv();
    char *output = NULL, *url = NULL;
    int fd, i, argc = sol_argc();

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
        fd = open(output, O_CREAT | O_EXCL | O_CLOEXEC | O_WRONLY);
        if (fd == -1) {
            fprintf(stderr, "ERROR: Failed to create the file: %s\n", output);
            sol_quit_with_code(EXIT_FAILURE);
            return;
        }
    } else {
        /* use stdout */
        fd = 1;
    }

    pending = sol_http_client_request_with_interface(SOL_HTTP_METHOD_GET,
        url, NULL, &iface, (void *)(long)fd);
    if (!pending) {
        fprintf(stderr, "ERROR: Failed to create the request\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }
}


static void
shutdown(void)
{
}

SOL_MAIN_DEFAULT(startup, shutdown);
