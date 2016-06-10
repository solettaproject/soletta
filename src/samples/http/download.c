/*
 * This file is part of the Soletta™ Project
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

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "soletta.h"
#include "sol-http.h"
#include "sol-http-client.h"

static FILE *fd;
struct sol_http_client_connection *pending;

static ssize_t
on_data_func(void *userdata, struct sol_http_client_connection *connection,
    const struct sol_buffer *buffer)
{
    ssize_t ret;

    ret = fwrite(buffer->data, buffer->used, 1, fd);
    if (!ret || ferror(fd)) {
        fprintf(stderr, "ERROR: Failed to write\n");
        return -1;
    }

    return ret * buffer->used;
}

static void
response_func(void *userdata, struct sol_http_client_connection *connection,
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
    .on_data = on_data_func,
    .on_response = response_func
};

static void
startup(void)
{
    char **argv = sol_argv();
    char *output = NULL, *url = NULL;
    int c, opt_idx,  argc = sol_argc();
    static const struct option opts[] = {
        { "output", required_argument, NULL, 'o' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(argc, argv, "o:h", opts, &opt_idx)) != -1) {
        switch (c) {
        case 'o':
            output = optarg;
            break;
        case 'h':
        default:
            fprintf(stderr,
                "Usage:\n\t%s [-o <output_file>] <url>\n", argv[0]);
            sol_quit_with_code(EXIT_SUCCESS);
            return;
        }
    }

    url = argv[optind];
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
