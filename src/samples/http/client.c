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
response_cb(void *userdata, const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    bool verbose = (bool)(long)userdata;

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        fprintf(stderr, "Finished with error, response code: %d\n", response->response_code);
        goto err;
    }

    if (verbose) {
        uint16_t idx;
        struct sol_http_param_value *value;

        SOL_HTTP_PARAMS_FOREACH_IDX (&response->param, value, idx) {
            switch (value->type) {
            case SOL_HTTP_PARAM_COOKIE:
                fprintf(stdout, "[COOKIE] %.*s : %.*s\n",
                    SOL_STR_SLICE_PRINT(value->value.key_value.key),
                    SOL_STR_SLICE_PRINT(value->value.key_value.value));
                break;
            case SOL_HTTP_PARAM_HEADER:
                fprintf(stdout, "[HEADER] %.*s : %.*s\n",
                    SOL_STR_SLICE_PRINT(value->value.key_value.key),
                    SOL_STR_SLICE_PRINT(value->value.key_value.value));
            default:
                break;
            }
        }
    }

    fprintf(stdout, "%.*s\n", (int)response->content.used, (char *)response->content.data);
    sol_quit_with_code(EXIT_SUCCESS);

    return;

err:
    sol_quit_with_code(EXIT_FAILURE);
}

static int
create_post_field_params(struct sol_http_params *params, const char *value)
{
    char *aux, *copy = strdup(value);

    if (!copy) {
        fprintf(stderr, "ERROR: Could not duplicate the string: %s\n", value);
        return -ENOMEM;
    }

    aux = strtok(copy, "&");
    do {
        size_t len;
        char *ptr, *key, *val;

        key = aux;
        len = strlen(key);

        ptr = memmem(key, len, "=", 1);
        if (!ptr) {
            fprintf(stderr, "Invalid key/value in %s\n", value);
            goto err;
        }

        *ptr = '\0';
        val = ++ptr;

        if (!sol_http_param_add_copy(params,
            SOL_HTTP_REQUEST_PARAM_POST_FIELD(key, val))) {
            fprintf(stderr, "ERROR: Could not add %s=%s\n", key, val);
            goto err;
        }

    } while ((aux = strtok(NULL, "&")));

    free(copy);
    return 0;

err:
    free(copy);
    return -1;
}

static void
startup(void)
{
    struct sol_http_client_connection *pending;
    char **argv = sol_argv();
    char *postfields = NULL, *url = NULL;
    int i, argc = sol_argc();
    bool verbose = false;
    struct sol_http_params params = SOL_HTTP_REQUEST_PARAMS_INIT;
    enum sol_http_method method = SOL_HTTP_METHOD_GET;

    if (argc < 2) {
        fprintf(stderr,
            "Usage:\n\t%s [-v verbose] [-d <\"post fields\">] <url>\n", argv[0]);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'v') {
                    verbose = true;
                    i++;
                    continue;
            } else if (argv[i][1] == 'd') {
                if (i + 1 < argc) {
                    postfields = argv[i + 1];
                    i++;
                    continue;
                } else {
                    fprintf(stderr, "ERROR: argument -d missing value.\n");
                }
            } else {
                fprintf(stderr, "ERROR: unknown option %s.\n", argv[i]);
            }

            goto err;
        } else {
            url = argv[i];
        }
    }

    if (!url) {
        fprintf(stderr, "ERROR: missing url.\n");
        goto err;
    }

    if (postfields) {
        method = SOL_HTTP_METHOD_POST;
        if (create_post_field_params(&params, postfields) < 0)
            goto err;
    }

    pending = sol_http_client_request(method,
        url, &params, response_cb, (void *)(long)verbose);
    if (!pending) {
        fprintf(stderr, "ERROR: Failed to create the request\n");
        goto err;
    }

    sol_http_params_clear(&params);
    return;

err:
    sol_http_params_clear(&params);
    sol_quit_with_code(EXIT_FAILURE);
    return;
}

static void
shutdown(void)
{
}

SOL_MAIN_DEFAULT(startup, shutdown);
