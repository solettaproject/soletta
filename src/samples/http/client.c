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
 * @brief HTTP Client sample
 *
 * A command-line application that allows one make GET and POST.
 * It intends to replicate some curl functionalities, such as, set post
 * fields and headers.
 */


#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "sol-mainloop.h"
#include "sol-http.h"
#include "sol-http-client.h"

static bool verbose = false;

static void
response_cb(void *userdata, const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    if (response->response_code != SOL_HTTP_STATUS_OK) {
        fprintf(stderr, "Finished with error, response code: %d\n",
            response->response_code);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (verbose) {
        uint16_t idx;
        struct sol_http_param_value *value;

        SOL_HTTP_PARAMS_FOREACH_IDX (&response->param, value, idx) {
            switch (value->type) {
            case SOL_HTTP_PARAM_COOKIE:
                printf("[COOKIE] %.*s : %.*s\n",
                    SOL_STR_SLICE_PRINT(value->value.key_value.key),
                    SOL_STR_SLICE_PRINT(value->value.key_value.value));
                break;
            case SOL_HTTP_PARAM_HEADER:
                printf("[HEADER] %.*s : %.*s\n",
                    SOL_STR_SLICE_PRINT(value->value.key_value.key),
                    SOL_STR_SLICE_PRINT(value->value.key_value.value));
            default:
                break;
            }
        }
    }

    printf("%.*s\n", (int)response->content.used, (char *)response->content.data);
    sol_quit_with_code(EXIT_SUCCESS);
}

static int
create_post_field_params(struct sol_http_params *params, const char *value)
{
    int r;

    r = sol_http_split_post_field(value, params);
    if (r < 0)
        fprintf(stderr, "ERROR: Could not parse the post fields - \'%s\'\n", value);

    return r;
}

static int
create_header_params(struct sol_http_params *params, const char *value)
{
    char *sep;
    struct sol_http_param_value param;
    struct sol_str_slice key, val;
    size_t len = strlen(value);

    sep = memchr(value, ':', len);
    key.data = value;
    if (sep) {
        key.len = sep - key.data;
        val.data = sep + 1;
        val.len = len - key.len - 1;
    } else {
        key.len = len;
        val.data = NULL;
        val.len = 0;
    }

    param.type = SOL_HTTP_PARAM_HEADER;
    param.value.key_value.key = key;
    param.value.key_value.value = val;

    if (!sol_http_param_add_copy(params, param)) {
        fprintf(stderr, "Could not add the HTTP param %.*s:%.*s",
            SOL_STR_SLICE_PRINT(key), SOL_STR_SLICE_PRINT(val));
        return -1;
    }

    return 0;
}

static void
startup(void)
{
    struct sol_http_client_connection *pending;
    char **argv = sol_argv();
    char *url = NULL;
    struct sol_http_params params = SOL_HTTP_REQUEST_PARAMS_INIT;
    enum sol_http_method method = SOL_HTTP_METHOD_GET;
    int c, opt_idx, argc = sol_argc();
    static const struct option opts[] = {
        { "verbose", no_argument, NULL, 'v' },
        { "header", required_argument, NULL, 'H' },
        { "data", required_argument, NULL, 'd' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(argc, argv, "vH:d:h", opts, &opt_idx)) != -1) {
        switch (c) {
        case 'v':
            verbose = true;
            break;
        case 'd':
            method = SOL_HTTP_METHOD_POST;
            if (create_post_field_params(&params, optarg) < 0)
                goto err;
            break;
        case 'H':
            if (create_header_params(&params, optarg) < 0)
                goto err;
            break;
        case 'h':
        default:
            fprintf(stderr,
                "Usage: %s <url> \n\t-v, --verbose Make it more talkative\n"
                "\t-H, --header <\"Header\"> pass custom header to server\n"
                "\t-d, --data  <\"post fields\"> HTTP POST data (NOT encoded)\n",
                argv[0]);
            sol_quit_with_code(EXIT_SUCCESS);
            return;
        }
    }

    url = argv[optind];
    if (!url) {
        fprintf(stderr, "ERROR: missing url.\n");
        goto err;
    }

    pending = sol_http_client_request(method,
        url, &params, response_cb, NULL);
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
