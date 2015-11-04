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

#include "test.h"
#include "sol-util.h"
#include "sol-http.h"
#include "sol-vector.h"

DEFINE_TEST(test_split_invalid_urls);

static void
test_split_invalid_urls(void)
{
    struct sol_str_slice protocol, server, fragment, path;
    struct sol_http_param params;
    int r, port;
    unsigned int i;
    const char *invalid_urls[] = {
        "",
        "!231312312#$http$//www.intel.com",
        "http//www.intel.com",
        "      http   :    //www.intel.com",
        "http:www.intel.com"
        "http://www.intel.com:asd!%%%!!23332182/",
        "http://",
        "www.intel.com",
        "http://\"",
        "http://www.intel.com:80:80",
    };

    for (i = 0; i < ARRAY_SIZE(invalid_urls); i++) {
        sol_http_param_init(&params);
        r = sol_http_split_uri(invalid_urls[i], &protocol, &server,
            &path, &fragment, &params, &port);
        ASSERT_INT_EQ(r, -EINVAL);
        sol_http_param_free(&params);
    }
}

DEFINE_TEST(test_split_create_valid_urls);

static void
test_split_create_valid_urls(void)
{
#define SET_VALUE(_uri, _protocol, _server, _path, \
        _query_size, _fragment, _port, ...)                   \
    { _uri, _protocol, _server, _path, _query_size, _fragment, _port, \
      __VA_ARGS__ }
#define MAX_QUERY_SIZE (10)

    struct sol_str_slice protocol, server, fragment, path;
    struct sol_http_param params;
    struct sol_http_param_value *param;
    int r, port;
    unsigned int i, j;
    char *uri;
    const struct {
        const char *uri;
        const char *protocol;
        const char *server;
        const char *path;
        unsigned int query_size;
        const char *fragment;
        int port;
        struct query_param {
            const char *key;
            const char *value;
        } query[MAX_QUERY_SIZE];
    } valid_urls[] = {
        SET_VALUE("http://www.intel.com", "http",
            "www.intel.com", "", 0, "", 80),
        SET_VALUE("http://www.intel.com:8080", "http", "www.intel.com",
            "", 0, "", 8080),
        SET_VALUE("http://www.intel.com:8080/a/path/here?q=2&b=2#fragment",
            "http", "www.intel.com", "a/path/here",
            2, "fragment", 8080, { { "q", "2" }, { "b", "2" } }),
        SET_VALUE("http://www.intel.com:8080#myFragment",
            "http", "www.intel.com", "", 0, "myFragment", 8080),
        SET_VALUE("ftp://10.1.1.1:1252/path/?q=2",
            "ftp", "10.1.1.1", "path/", 1, "", 1252, { { "q", "2" } }),
        SET_VALUE("http://www.intel.com?q=2&d=3",
            "http", "www.intel.com", "", 2, "", 80, { { "q", "2" }, { "d", "3" } })
    };

    for (i = 0; i < ARRAY_SIZE(valid_urls); i++) {
        sol_http_param_init(&params);
        r = sol_http_split_uri(valid_urls[i].uri, &protocol, &server,
            &path, &fragment, &params, &port);
        ASSERT_INT_EQ(r, 0);
        ASSERT(sol_str_slice_str_eq(protocol, valid_urls[i].protocol));
        ASSERT(sol_str_slice_str_eq(server, valid_urls[i].server));
        ASSERT(sol_str_slice_str_eq(path, valid_urls[i].path));
        ASSERT(sol_str_slice_str_eq(fragment, valid_urls[i].fragment));
        ASSERT_INT_EQ(valid_urls[i].port, port);
        ASSERT_INT_EQ(valid_urls[i].query_size, params.params.len);
        SOL_VECTOR_FOREACH_IDX (&params.params, param, j) {
            ASSERT(sol_str_slice_str_eq(param->value.key_value.key,
                valid_urls[i].query[j].key));
            ASSERT(sol_str_slice_str_eq(param->value.key_value.value,
                valid_urls[i].query[j].value));
        }
        r = sol_http_create_uri(&uri, valid_urls[i].protocol,
            valid_urls[i].server, valid_urls[i].path, valid_urls[i].fragment,
            valid_urls[i].port, &params);
        ASSERT_INT_EQ(r, 0);
        ASSERT(streq(uri, valid_urls[i].uri));
        free(uri);
        sol_http_param_free(&params);
    }
#undef SET_VALUE
#undef MAX_QUERY_SIZE
}

DEFINE_TEST(test_create_simple_uri);

static void
test_create_simple_uri(void)
{
    int r;
    char *uri;
    unsigned int i;
    bool b;

    struct {
        const char *base;
        const char *expected;
        struct sol_http_param params;
    } urls[] = {
        { "http://www.intel.com", "http://www.intel.com/",
          SOL_HTTP_REQUEST_PARAM_INIT },
        { "http://www.intel.com/", "http://www.intel.com//",
          SOL_HTTP_REQUEST_PARAM_INIT },
        { "http://www.intel.com", "http://www.intel.com/?This%20Key%20Should%20be%20encoded%20%21%21%2A%2F%26%25%24%24%C2%A8=My%20precious%20value%20%25%23%26%2A%2A%28%29%2C%2C&SimpleKey=SimpleValue",
          SOL_HTTP_REQUEST_PARAM_INIT },
    };

    b = sol_http_param_add(&urls[2].params,
        SOL_HTTP_REQUEST_PARAM_QUERY("This Key Should be encoded !!*/&%$$¨",
        "My precious value %#&**(),,"));
    ASSERT(b);
    b = sol_http_param_add(&urls[2].params,
        SOL_HTTP_REQUEST_PARAM_QUERY("SimpleKey", "SimpleValue"));
    ASSERT(b);

    for (i = 0; i < ARRAY_SIZE(urls); i++) {
        r = sol_http_create_simple_uri(&uri, urls[i].base, &urls[i].params);
        ASSERT_INT_EQ(r, 0);
        ASSERT(streq(uri, urls[i].expected));
        free(uri);
        sol_http_param_free(&urls[i].params);
    }
}

TEST_MAIN();
