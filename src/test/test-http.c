/*
 * This file is part of the Soletta (TM) Project
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

#include "test.h"
#include "sol-util-internal.h"
#include "sol-http.h"
#include "sol-vector.h"
#include "sol-util.h"

DEFINE_TEST(test_http_content_type_priority);

static void
test_http_content_type_priority(void)
{
    size_t i;
    int r;

    static const struct {
        int r;
        size_t priorities_len;
        struct sol_str_slice accept;
        struct {
            struct sol_str_slice content_type;
            double qvalue;
            size_t tokens_size;
            const char *tokens[2];
        } result[6];
    } test[] = {
        { 0, 1, SOL_STR_SLICE_LITERAL("text/html;  q  =  2"),
          {
              { SOL_STR_SLICE_LITERAL("text/html"), 1.0, 0, { 0 } }
          } },
        { 0, 2, SOL_STR_SLICE_LITERAL("audio/*;q=0.2,      audio/basic   "),
          {
              { SOL_STR_SLICE_LITERAL("audio/basic"), 1.0, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("audio/*"), 0.2, 0, { 0 } }
          } },
        { 0, 4, SOL_STR_SLICE_LITERAL("text/plain; q=0.5, text/html,text/x-dvi; q=0.8, text/x-c"),
          {
              { SOL_STR_SLICE_LITERAL("text/html"), 1.0, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("text/x-c"), 1.0, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("text/x-dvi"), 0.8, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("text/plain"), 0.5, 0, { 0 } },
          } },
        { 0, 4, SOL_STR_SLICE_LITERAL("text/*, text/html, text/html;level=1, */*"),
          {
              { SOL_STR_SLICE_LITERAL("text/html"), 1.0, 1, { "level=1" } },
              { SOL_STR_SLICE_LITERAL("text/html"), 1.0, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("text/*"), 1.0, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("*/*"), 1.0, 0, { 0 } },
          } },
        { 0, 5, SOL_STR_SLICE_LITERAL("text/*;q=0.3, text/html;q=0.7, text/html;level=1,text/html;level=2;level=3;q=0.4, */*;q=0.5"),
          {
              { SOL_STR_SLICE_LITERAL("text/html"), 1.0, 1, { "level=1" } },
              { SOL_STR_SLICE_LITERAL("text/html"), 0.7, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("text/html"), 0.4, 2, { "level=2", "level=3" } },
              { SOL_STR_SLICE_LITERAL("text/*"), 0.3, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("*/*"), 0.5, 0, { 0 } },
          } },
        { 0, 6, SOL_STR_SLICE_LITERAL("text/html; q=1.0, text/*; q=0.8, image/gif; q=0.6, image/jpeg; q=0.6, image/*; q=0.5, */*; q=0.1"),
          {
              { SOL_STR_SLICE_LITERAL("text/html"), 1.0, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("text/*"), 0.8, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("image/gif"), 0.6, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("image/jpeg"), 0.6, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("image/*"), 0.5, 0, { 0 } },
              { SOL_STR_SLICE_LITERAL("*/*"), 0.1, 0, { 0 } },
          } }

    };

    for (i = 0; i < sol_util_array_size(test); i++) {
        size_t j;
        struct sol_vector array;

        r = sol_http_parse_content_type_priorities(test[i].accept, &array);
        ASSERT_INT_EQ(r, test[i].r);
        ASSERT_INT_EQ(test[i].priorities_len, array.len);

        for (j = 0; j < test[i].priorities_len; j++) {
            size_t k;
            struct sol_http_content_type_priority *pri = sol_vector_get_no_check(&array, j);
            ASSERT(sol_str_slice_eq(pri->content_type, test[i].result[j].content_type));
            ASSERT(sol_util_double_eq(pri->qvalue, test[i].result[j].qvalue));
            ASSERT_INT_EQ(test[i].result[j].tokens_size, pri->tokens.len);
            for (k = 0; k < test[i].result[j].tokens_size; k++) {
                struct sol_str_slice *token = sol_vector_get_no_check(&pri->tokens, k);
                ASSERT(sol_str_slice_str_eq(*token, test[i].result[j].tokens[k]));
            }
        }

        sol_http_content_type_priorities_array_clear(&array);
    }
}

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
        SET_PARAMS("http://[2001:db8::1]", "http", "", "", "2001:db8::1", "", "", "", 0, 0, true),
        SET_PARAMS("http://2001:db8::1", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS("http://[2001:db8::1", "", "", "", "", "", "", "", 0, -EINVAL, false),
        SET_PARAMS("http://2001:db8::1]", "", "", "", "", "", "", "", 0, -EINVAL, false),

        SET_PARAMS("http://[::1]:/", "http", "", "", "::1", "/", "", "", 0, 0, false),
        SET_PARAMS("http://[::1]/?go=2", "http", "", "", "::1", "/", "go=2", "", 0, 0, true),
        SET_PARAMS("http://[::1]:8080", "http", "", "", "::1", "", "", "", 8080, 0, true),
        SET_PARAMS("http://[::1]:1234/", "http", "", "", "::1", "/", "", "", 1234, 0, true),
        SET_PARAMS("http://[::1]/a/b/d?go=2#fragment", "http", "", "", "::1", "/a/b/d", "go=2", "fragment", 0, 0, true),
        SET_PARAMS("foo://user:pass@[::1]:123/a/b?p=1&c=2#/a/b", "foo", "user", "pass", "::1", "/a/b", "p=1&c=2", "/a/b", 123, 0, true),
        SET_PARAMS("foo://user@[::1]:123/a/b?p=1&c=2#/a/b", "foo", "user", "", "::1", "/a/b", "p=1&c=2", "/a/b", 123, 0, true),
        SET_PARAMS("foo://user:@[::1]:123/a/b?p=1&c=2#/a/b", "foo", "user", "", "::1", "/a/b", "p=1&c=2", "/a/b", 123, 0, false),
        SET_PARAMS("foo://[::1]:123/a/b?p=1&c=2#/a/b", "foo", "", "", "::1", "/a/b", "p=1&c=2", "/a/b", 123, 0, true),
        SET_PARAMS("foo://[::1]/a/b?p=1&c=2#/a/b", "foo", "", "", "::1", "/a/b", "p=1&c=2", "/a/b", 0, 0, true),
        SET_PARAMS("foo://[::1]/?p=1&c=2#/a/b", "foo", "", "", "::1", "/", "p=1&c=2", "/a/b", 0, 0, true),
        SET_PARAMS("foo://[::1]/?p=1&c=2", "foo", "", "", "::1", "/", "p=1&c=2", "", 0, 0, true),
        SET_PARAMS("foo://[::1]/#/a/b", "foo", "", "", "::1", "/", "", "/a/b", 0, 0, true),
        SET_PARAMS("foo://[::1]?p=1&c=2", "foo", "", "", "::1", "", "p=1&c=2", "", 0, 0, true),
        SET_PARAMS("foo://[::1]#/a/b", "foo", "", "", "::1", "", "", "/a/b", 0, 0, true),
        SET_PARAMS("foo://[::1]:123/#/a/b", "foo", "", "", "::1", "/", "", "/a/b", 123, 0, true),
        SET_PARAMS("file://[::1]/usr/home/user/hi.txt", "file", "", "", "::1", "/usr/home/user/hi.txt", "", "", 0, 0, true),
        SET_PARAMS("foo://[::1]/?go", "foo", "", "", "::1", "/", "go", "", 0, 0, true),
        SET_PARAMS("foo://:password@[::1]", "foo", "", "password", "::1", "", "", "", 0, 0, true),
        SET_PARAMS("foo://:@[::1]", "foo", "", "", "::1", "", "", "", 0, 0, false),
        SET_PARAMS("foo://@[::1]", "foo", "", "", "::1", "", "", "", 0, 0, false),

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
           This behaviour is acceptable, since ':' can be omitted if the password is not provided.
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
        SET_PARAMS("foo://:password@localhost", "foo", "", "password", "localhost", "", "", "", 0, 0, true),
        SET_PARAMS("foo://:@localhost", "foo", "", "", "localhost", "", "", "", 0, 0, false),
        SET_PARAMS("foo://@localhost", "foo", "", "", "localhost", "", "", "", 0, 0, false),
    };

    for (i = 0; i < sol_util_array_size(test_split); i++) {
        struct sol_http_url splitted;
        struct sol_http_params params;
        struct sol_buffer out_uri = SOL_BUFFER_INIT_EMPTY;

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
        r = sol_http_create_full_uri(&out_uri, splitted, &params);
        ASSERT_INT_EQ(r, 0);
        ASSERT(sol_str_slice_eq(test_split[i].url, sol_buffer_get_slice(&out_uri)));
        sol_http_params_clear(&params);
        sol_buffer_fini(&out_uri);
    }

#undef SET_PARAMS
}


TEST_MAIN();
