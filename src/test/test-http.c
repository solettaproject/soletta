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

DEFINE_TEST(test_split_urls);

static void
test_split_urls(void)
{
#define SET_PARAMS(_url, _scheme, _user, _pass, _host, _path, _query, _fragment, _port, _result, _check_url) \
    { SOL_STR_SLICE_LITERAL(_url), { SOL_STR_SLICE_LITERAL(_scheme), SOL_STR_SLICE_LITERAL(_user), SOL_STR_SLICE_LITERAL(_pass), \
                                     SOL_STR_SLICE_LITERAL(_host), SOL_STR_SLICE_LITERAL(_path), \
                                     SOL_STR_SLICE_LITERAL(_query), SOL_STR_SLICE_LITERAL(_fragment), _port }, _result, _check_url }
    size_t i;
    int r;
    static const struct {
        struct sol_str_slice url;
        struct sol_http_url splitted_url;
        int result;
        bool check_url;
    } test_split[] =  {
        SET_PARAMS("http://2001:db8::1", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS("http://[2001:db8::1", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS("http://2001:db8::1]", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS("www.intel.com.br", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS(":www.intel.com", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS("//www.intel.com", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS("://www.intel.com", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS("/a/b", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS("//a/b", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS("http://", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS("http://www.intel.com:/", "http", "", "", "www.intel.com", "/", "", "", 0, 0, false),
        SET_PARAMS("http://intel.com/?go=2", "http", "", "", "intel.com", "/", "go=2", "", 0, 0, true),
        SET_PARAMS("http://www.intel.com:8080", "http", "", "", "www.intel.com", "", "", "", 8080, 0, true),
        SET_PARAMS("http://www.intel.com:1234/", "http", "", "", "www.intel.com", "/", "", "", 1234, 0, true),
        SET_PARAMS("http://www.intel.com/a/b/d?go=2#fragment", "http", "", "", "www.intel.com", "/a/b/d", "go=2", "fragment", 0, 0, true),
        SET_PARAMS("foo://user:pass@server.com:123/a/b?p=1&c=2#/a/b", "foo", "user", "pass", "server.com", "/a/b", "p=1&c=2", "/a/b", 123, 0, true),
        SET_PARAMS("foo://user@server.com:123/a/b?p=1&c=2#/a/b", "foo", "user", "", "server.com", "/a/b", "p=1&c=2", "/a/b", 123, 0, true),
        /* Do not check the created url for this one, Altought the created one will be correct it will not match,
           as the create url will be foo://user:@server.com:123/a/b?p=1&c=2#/a/b
           This behaviour is acceptable, since ':' can be ommited if the password is not provided.
         */
        SET_PARAMS("foo://user:@server.com:123/a/b?p=1&c=2#/a/b", "foo", "user", "", "server.com", "/a/b", "p=1&c=2", "/a/b", 123, 0, false),
        SET_PARAMS("foo://server.com:123/a/b?p=1&c=2#/a/b", "foo", "", "", "server.com", "/a/b", "p=1&c=2", "/a/b", 123, 0, true),
        SET_PARAMS("foo://server.com/a/b?p=1&c=2#/a/b", "foo", "", "", "server.com", "/a/b", "p=1&c=2", "/a/b", 0, 0, true),
        SET_PARAMS("foo://server.com/?p=1&c=2#/a/b", "foo", "", "", "server.com", "/", "p=1&c=2", "/a/b", 0, 0, true),
        SET_PARAMS("foo://server.com/?p=1&c=2", "foo", "", "", "server.com", "/", "p=1&c=2", "", 0, 0, true),
        SET_PARAMS("foo://server.com/#/a/b", "foo", "", "", "server.com", "/", "", "/a/b", 0, 0, true),
        SET_PARAMS("foo://server.com?p=1&c=2", "foo", "", "", "server.com", "", "p=1&c=2", "", 0, 0, true),
        SET_PARAMS("foo://server.com#/a/b", "foo", "", "", "server.com", "", "", "/a/b", 0, 0, true),
        SET_PARAMS("foo://192.3.3.3:123/#/a/b", "foo", "", "", "192.3.3.3", "/", "", "/a/b", 123, 0, true),
        SET_PARAMS("mailto:user@server.com", "mailto", "", "", "", "user@server.com", "", "", 0, 0, true),
        SET_PARAMS("file://localhost/usr/home/user/hi.txt", "file", "", "", "localhost", "/usr/home/user/hi.txt", "", "", 0, 0, true),
        SET_PARAMS("foo://localhost/?go", "foo", "", "", "localhost", "/", "go", "", 0, 0, true),
    };

    for (i = 0; i < ARRAY_SIZE(test_split); i++) {
        struct sol_http_url splitted;
        struct sol_http_params params;
        char *out_uri;
        r = sol_http_split_uri(test_split[i].url, &splitted);
        ASSERT_INT_EQ(r, test_split[i].result);
        if (test_split[i].result < 0)
            continue;
        ASSERT(sol_str_slice_eq(splitted.scheme, test_split[i].splitted_url.scheme));
        ASSERT(sol_str_slice_eq(splitted.host, test_split[i].splitted_url.host));
        ASSERT(sol_str_slice_eq(splitted.path, test_split[i].splitted_url.path));
        ASSERT(sol_str_slice_eq(splitted.fragment, test_split[i].splitted_url.fragment));
        ASSERT(sol_str_slice_eq(splitted.query, test_split[i].splitted_url.query));
        ASSERT(sol_str_slice_eq(splitted.user, test_split[i].splitted_url.user));
        ASSERT(sol_str_slice_eq(splitted.password, test_split[i].splitted_url.password));
        ASSERT_INT_EQ(splitted.port, test_split[i].splitted_url.port);
        if (!test_split[i].check_url)
            continue;
        sol_http_params_init(&params);
        r = sol_http_decode_params(splitted.query, SOL_HTTP_PARAM_QUERY_PARAM, &params);
        ASSERT_INT_EQ(r, 0);
        r = sol_http_create_uri(&out_uri, splitted, &params);
        ASSERT_INT_EQ(r, 0);
        ASSERT(sol_str_slice_str_eq(test_split[i].url, out_uri));
        sol_http_params_clear(&params);
        free(out_uri);
    }

#undef SET_PARAMS
}


TEST_MAIN();
