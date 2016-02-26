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

#include "soletta.h"
#include "sol-http.h"
#include "sol-http-client.h"

static bool verbose = false;

static void
response_cb(void *userdata, const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{

    SOL_HTTP_RESPONSE_CHECK_API(response);
    SOL_HTTP_PARAMS_CHECK_API_VERSION(&response->param);

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
create_post_data_params(struct sol_http_params *params, const char *query)
{
    struct sol_vector tokens;
    struct sol_str_slice *token;
    char *sep;
    uint16_t i;
    int ret = 0;

    tokens = sol_str_slice_split(sol_str_slice_from_str(query), "&", 0);

#define CREATE_PARAM(_key, _filename, _value) \
    (struct sol_http_param_value) { \
        .type = SOL_HTTP_PARAM_POST_DATA, \
        .value.data.filename = _filename, \
        .value.data.key = _key, \
        .value.data.value = _value \
    }


    SOL_VECTOR_FOREACH_IDX (&tokens, token, i) {
        struct sol_str_slice key, value;
        bool ok;

        sep = memchr(token->data, '=', token->len);
        key.data = token->data;
        if (sep) {
            key.len = sep - key.data;
            value.data = sep + 1;
            value.len = token->len - key.len - 1;
        } else {
            key.len = token->len;
            value.data = NULL;
            value.len = 0;
        }

        if (value.data && (value.data[0] == '@')) {
            value.data++;
            value.len--;
            ok = sol_http_param_add_copy(params,
                CREATE_PARAM(key, value, SOL_STR_SLICE_EMPTY));
        } else {
            ok = sol_http_param_add_copy(params,
                CREATE_PARAM(key, SOL_STR_SLICE_EMPTY, value));
        }

        if (!ok) {
            fprintf(stderr, "[ERROR] Could not add the HTTP param %.*s:%.*s\n",
                SOL_STR_SLICE_PRINT(key), SOL_STR_SLICE_PRINT(value));
            ret = -1;
            goto exit;
        }
    }

#undef CREATE_PARAM

exit:
    sol_vector_clear(&tokens);
    return ret;
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
        { "form", required_argument, NULL, 'F' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(argc, argv, "vH:d:F:h", opts, &opt_idx)) != -1) {
        switch (c) {
        case 'v':
            verbose = true;
            break;
        case 'd':
            method = SOL_HTTP_METHOD_POST;
            if (create_post_field_params(&params, optarg) < 0)
                goto err;
            break;
        case 'F':
            method = SOL_HTTP_METHOD_POST;
            if (create_post_data_params(&params, optarg) < 0)
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
                "\t-F, --form <\"post data\"> Specify HTTP mulitpart POST data\n"
                "\t           syntax: key=value (for post value) or key=@value to post "
                "the contents of the file value\n"
                "\t-d, --data  <\"post fields\"> HTTP POST fields (NOT encoded)\n",
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
