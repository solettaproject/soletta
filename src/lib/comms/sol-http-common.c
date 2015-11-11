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
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "sol-http.h"
#include "sol-log.h"
#include "sol-util.h"
#include "sol-vector.h"

SOL_API bool
sol_http_param_add(struct sol_http_param *params,
    struct sol_http_param_value value)
{
    struct sol_http_param_value *ptr;

    SOL_NULL_CHECK(params, -EINVAL);

#ifndef SOL_NO_API_VERSION
    if (params->api_version != SOL_HTTP_PARAM_API_VERSION) {
        SOL_ERR("API version mistmatch; expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return false;
    }
#endif

    ptr = sol_vector_append(&params->params);
    if (!ptr) {
        SOL_WRN("Could not append option to parameter vector");
        return false;
    }

    memcpy(ptr, &value, sizeof(value));
    return true;
}

SOL_API bool
sol_http_param_add_copy(struct sol_http_param *params,
    struct sol_http_param_value value)
{
    struct sol_http_param_value *ptr;
    int r;

    SOL_NULL_CHECK(params, -EINVAL);

#ifndef SOL_NO_API_VERSION
    if (params->api_version != SOL_HTTP_PARAM_API_VERSION) {
        SOL_ERR("API version mistmatch; expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return false;
    }
#endif

    if (!params->arena) {
        params->arena = sol_arena_new();
        SOL_NULL_CHECK(params->arena, false);
    }

    if (value.type == SOL_HTTP_PARAM_QUERY_PARAM ||
        value.type == SOL_HTTP_PARAM_COOKIE ||
        value.type == SOL_HTTP_PARAM_POST_FIELD ||
        value.type == SOL_HTTP_PARAM_HEADER) {
        if (value.value.key_value.key.len) {
            r = sol_arena_slice_dup(params->arena, &value.value.key_value.key,
                value.value.key_value.key);
            SOL_INT_CHECK(r, < 0, false);
        }
        if (value.value.key_value.value.len) {
            r = sol_arena_slice_dup(params->arena, &value.value.key_value.value,
                value.value.key_value.value);
            SOL_INT_CHECK(r, < 0, false);
        }
    } else if (value.type == SOL_HTTP_PARAM_POST_DATA) {
        r = sol_arena_slice_dup(params->arena,
            (struct sol_str_slice *)&value.value.data.value,
            value.value.data.value);
        SOL_INT_CHECK(r, < 0, false);
    } else if (value.type == SOL_HTTP_PARAM_AUTH_BASIC) {
        if (value.value.auth.user.len) {
            r = sol_arena_slice_dup(params->arena, &value.value.auth.user,
                value.value.auth.user);
            SOL_INT_CHECK(r, < 0, false);
        }
        if (value.value.auth.password.len) {
            r = sol_arena_slice_dup(params->arena, &value.value.auth.password,
                value.value.auth.password);
            SOL_INT_CHECK(r, < 0, false);
        }
    }

    ptr = sol_vector_append(&params->params);
    if (!ptr) {
        SOL_WRN("Could not append option to parameter vector");
        return false;
    }

    memcpy(ptr, &value, sizeof(value));
    return true;
}

SOL_API void
sol_http_param_free(struct sol_http_param *params)
{
    SOL_NULL_CHECK(params);

#ifndef SOL_NO_API_VERSION
    if (params->api_version != SOL_HTTP_PARAM_API_VERSION) {
        SOL_ERR("API version mistmatch; expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return;
    }
#endif
    sol_vector_clear(&params->params);
    if (params->arena) {
        sol_arena_del(params->arena);
        params->arena = NULL;
    }
}

SOL_API int
sol_http_encode_slice(struct sol_buffer *buf, const struct sol_str_slice value)
{
    int r;
    size_t i, last_append;

    SOL_NULL_CHECK(buf, -EINVAL);

    sol_buffer_init(buf);
    last_append = 0;
    for (i = 0; i < value.len; i++) {
        unsigned char c = value.data[i];
        if (!isalnum(c) && c != '_' && c != '~' && c != '.' && c != '-') {
            r = sol_buffer_append_slice(buf,
                SOL_STR_SLICE_STR(value.data + last_append, i - last_append));
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
            last_append = i + 1;
            r = sol_buffer_append_printf(buf, "%%%02X", c);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }
    }

    if (!last_append) {
        sol_buffer_init_flags(buf, (char *)value.data, value.len,
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);
        buf->used = buf->capacity;
    } else if (last_append != value.len) {
        r = sol_buffer_append_slice(buf,
            SOL_STR_SLICE_STR(value.data + last_append,
            value.len - last_append));
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    return 0;

err_exit:
    sol_buffer_fini(buf);
    return r;
}

SOL_API int
sol_http_decode_slice(struct sol_buffer *buf,
    const struct sol_str_slice value)
{
    size_t i, last_append;
    int r;

    SOL_NULL_CHECK(buf, -EINVAL);

    sol_buffer_init(buf);
    last_append = 0;

    for (i = 0; i < value.len; i++) {
        unsigned char c = value.data[i];
        if (c == '%' && value.len - i > 2 && isxdigit(value.data[i + 1])
            && isxdigit(value.data[i + 2])) {
            struct sol_str_slice hex;
            ssize_t err;
            char chex;

            hex.data = value.data + i + 1;
            hex.len = 2;
            err = sol_util_base16_decode(&chex, 1, hex,
                SOL_DECODE_BOTH);
            SOL_INT_CHECK_GOTO(err, < 0, err_exit);

            r = sol_buffer_append_slice(buf,
                SOL_STR_SLICE_STR(value.data + last_append, i - last_append));
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
            i += 2;
            last_append = i + 1;
            r = sol_buffer_append_char(buf, chex);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }
    }

    if (!last_append) {
        sol_buffer_init_flags(buf, (char *)value.data, value.len,
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);
        buf->used = buf->capacity;
    } else if (last_append != value.len) {
        r = sol_buffer_append_slice(buf,
            SOL_STR_SLICE_STR(value.data + last_append,
            value.len - last_append));
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    return 0;
err_exit:
    sol_buffer_fini(buf);
    return r;
}

SOL_API int
sol_http_encode_params(struct sol_buffer *buf, enum sol_http_param_type type,
    const struct sol_http_param *params)
{
    const char *prefix, *suffix;
    struct sol_buffer encoded_key, encoded_value;
    struct sol_http_param_value *iter;
    uint16_t idx;
    bool first = true;
    int r;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_NULL_CHECK(params, -EINVAL);

    if (type != SOL_HTTP_PARAM_QUERY_PARAM &&
        type != SOL_HTTP_PARAM_POST_FIELD &&
        type != SOL_HTTP_PARAM_COOKIE &&
        type != SOL_HTTP_PARAM_HEADER) {
        SOL_WRN("The type %u is not supported", type);
        return -EINVAL;
    }

    if (type == SOL_HTTP_PARAM_COOKIE) {
        prefix = " ";
        suffix = ";";
    } else {
        prefix = "&";
        suffix = "";
    }

    SOL_HTTP_PARAM_FOREACH_IDX (params, iter, idx) {
        if (iter->type != type)
            continue;

        r = sol_http_encode_slice(&encoded_key, iter->value.key_value.key);
        SOL_INT_CHECK_GOTO(r, < 0, clean_up);

        r = sol_http_encode_slice(&encoded_value, iter->value.key_value.value);
        SOL_INT_CHECK_GOTO(r, < 0, clean_up);

        if (iter->value.key_value.value.len) {
            r = sol_buffer_append_printf(buf, "%s%.*s=%.*s%s",
                first ? "" : prefix,
                SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&encoded_key)),
                SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&encoded_value)),
                suffix);
        } else {
            r = sol_buffer_append_printf(buf, "%s%.*s%s",
                first ? "" : prefix,
                SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&encoded_key)),
                suffix);
        }

        SOL_INT_CHECK_GOTO(r, < 0, clean_up);

        first = false;
        sol_buffer_fini(&encoded_key);
        sol_buffer_fini(&encoded_value);
    }

    return 0;

clean_up:
    sol_buffer_fini(&encoded_key);
    sol_buffer_fini(&encoded_value);
    return r;
}

SOL_API int
sol_http_decode_params(const struct sol_str_slice params_slice,
    enum sol_http_param_type type, struct sol_http_param *params)
{
    struct sol_buffer decoded_key, decoded_value;
    struct sol_str_slice *token;
    struct sol_vector tokens;
    const char *param_sep;
    char *sep;
    uint16_t i;
    int r = 0;

    SOL_NULL_CHECK(params, -EINVAL);

    if (type != SOL_HTTP_PARAM_QUERY_PARAM &&
        type != SOL_HTTP_PARAM_POST_FIELD &&
        type != SOL_HTTP_PARAM_COOKIE &&
        type != SOL_HTTP_PARAM_HEADER) {
        SOL_WRN("The type %u is not supported", type);
        return -EINVAL;
    }

    if (!params_slice.len)
        return 0;

    if (type == SOL_HTTP_PARAM_COOKIE)
        param_sep = ";";
    else
        param_sep = "&";

    tokens = sol_util_str_split(params_slice, param_sep, 0);
    SOL_VECTOR_FOREACH_IDX (&tokens, token, i) {
        struct sol_http_param_value param;
        sep = memchr(token->data, '=', token->len);

        param.value.key_value.key.data = token->data;
        if (sep) {
            param.value.key_value.key.len = sep - token->data;

            param.value.key_value.value.data = sep + 1;
            param.value.key_value.value.len = token->len -
                param.value.key_value.key.len - 1;
        } else {
            param.value.key_value.key.len = token->len;
            param.value.key_value.value.data = NULL;
            param.value.key_value.value.len = 0;
        }

        r = sol_http_decode_slice(&decoded_key, param.value.key_value.key);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        r = sol_http_decode_slice(&decoded_value, param.value.key_value.value);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        param.value.key_value.key = sol_buffer_get_slice(&decoded_key);
        param.value.key_value.value = sol_buffer_get_slice(&decoded_value);
        SOL_DBG("Query key: %.*s Query value: %.*s",
            SOL_STR_SLICE_PRINT(param.value.key_value.key),
            SOL_STR_SLICE_PRINT(param.value.key_value.value));

        param.type = type;

        if (!sol_http_param_add_copy(params, param)) {
            SOL_WRN("Could not alloc the param %.*s : %.*s",
                SOL_STR_SLICE_PRINT(param.value.key_value.key),
                SOL_STR_SLICE_PRINT(param.value.key_value.value));
            r = -ENOMEM;
            goto err_exit;
        }

        sol_buffer_fini(&decoded_key);
        sol_buffer_fini(&decoded_value);
    }

    sol_vector_clear(&tokens);
    return r;

err_exit:
    sol_buffer_fini(&decoded_key);
    sol_buffer_fini(&decoded_value);
    sol_vector_clear(&tokens);
    return r;
}

SOL_API int
sol_http_create_uri(char **uri_out, const struct sol_http_url url,
    const struct sol_http_param *params)
{
    struct sol_str_slice scheme;
    struct sol_buffer buf, buf_encoded;
    int r;

    SOL_NULL_CHECK(uri_out, -EINVAL);

    sol_buffer_init(&buf);

    if (url.scheme.len)
        scheme = url.scheme;
    else
        scheme = sol_str_slice_from_str("http");

    r = sol_buffer_append_slice(&buf, scheme);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = sol_buffer_append_char(&buf, ':');
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    if (url.host.len > 0) {
        r = sol_buffer_append_slice(&buf, sol_str_slice_from_str("//"));
        SOL_INT_CHECK_GOTO(r, < 0, exit);

        if (url.user.len > 0) {
            r = sol_http_encode_slice(&buf_encoded, url.user);
            SOL_INT_CHECK_GOTO(r, < 0, exit);
            r = sol_buffer_append_slice(&buf,
                sol_buffer_get_slice(&buf_encoded));
            sol_buffer_fini(&buf_encoded);
            SOL_INT_CHECK_GOTO(r, < 0, exit);
            if (url.password.len > 0) {
                r = sol_buffer_append_char(&buf, ':');
                SOL_INT_CHECK_GOTO(r, < 0, exit);
                r = sol_http_encode_slice(&buf_encoded, url.password);
                SOL_INT_CHECK_GOTO(r, < 0, exit);
                r = sol_buffer_append_slice(&buf,
                    sol_buffer_get_slice(&buf_encoded));
                sol_buffer_fini(&buf_encoded);
                SOL_INT_CHECK_GOTO(r, < 0, exit);
            }
            r = sol_buffer_append_char(&buf, '@');
            SOL_INT_CHECK_GOTO(r, < 0, exit);
        }

        r = sol_buffer_append_slice(&buf, url.host);
        SOL_INT_CHECK_GOTO(r, < 0, exit);

        if (url.port > 0) {
            r = sol_buffer_append_printf(&buf, ":%" PRIu32, url.port);
            SOL_INT_CHECK_GOTO(r, < 0, exit);
        }
    }

    r = sol_buffer_append_slice(&buf, url.path);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    if (params && params->params.len) {
        size_t used;
        r = sol_buffer_append_char(&buf, '?');
        SOL_INT_CHECK_GOTO(r, < 0, exit);
        used = buf.used;
        r = sol_http_encode_params(&buf, SOL_HTTP_PARAM_QUERY_PARAM,
            params);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
        if (used == buf.used) {
            sol_buffer_resize(&buf, used - 1);
            SOL_INT_CHECK_GOTO(r, < 0, exit);
        }
    }

    if (url.fragment.len) {
        r = sol_buffer_append_char(&buf, '#');
        SOL_INT_CHECK_GOTO(r, < 0, exit);
        r = sol_buffer_append_slice(&buf, url.fragment);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = sol_buffer_ensure_nul_byte(&buf);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    *uri_out = sol_buffer_steal(&buf, NULL);

    if (!*uri_out)
        r = -ENOMEM;

exit:
    sol_buffer_fini(&buf);
    return r;
}

SOL_API int
sol_http_create_simple_uri(char **uri, const struct sol_str_slice base_uri,
    const struct sol_http_param *params)
{
    struct sol_buffer buf;
    int r;

    SOL_NULL_CHECK(uri, -EINVAL);
    if (base_uri.len == 0) {
        SOL_WRN("base_url is empty!");
        return -EINVAL;
    }

    sol_buffer_init(&buf);

    r = sol_buffer_append_slice(&buf, base_uri);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    if (params && params->params.len) {
        size_t used;
        r = sol_buffer_append_char(&buf, '?');
        SOL_INT_CHECK_GOTO(r, < 0, exit);
        used = buf.used;
        r = sol_http_encode_params(&buf, SOL_HTTP_PARAM_QUERY_PARAM,
            params);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
        if (used == buf.used) {
            r = sol_buffer_resize(&buf, used - 1);
            SOL_INT_CHECK_GOTO(r, < 0, exit);
        }
    }

    r = sol_buffer_ensure_nul_byte(&buf);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    *uri = sol_buffer_steal(&buf, NULL);

    if (!*uri)
        r = -ENOMEM;

exit:
    sol_buffer_fini(&buf);
    return r;
}

static void
_update_partial_uri(struct sol_str_slice *partial_uri, size_t offset)
{
    partial_uri->data = partial_uri->data + offset;
    partial_uri->len = partial_uri->len - offset;
}

static int
_get_scheme(const struct sol_str_slice uri,
    struct sol_str_slice *next,
    struct sol_str_slice *scheme)
{
    char *sep;

    scheme->data = uri.data;

    sep = memchr(uri.data, ':', uri.len);

    if (!sep) {
        SOL_WRN("Could not find the scheme seperator (:) at URI:%.*s",
            SOL_STR_SLICE_PRINT(uri));
        return -EINVAL;
    }

    scheme->len = sep - scheme->data;

    if (!scheme->len) {
        SOL_WRN("Empty scheme. URI: %.*s", SOL_STR_SLICE_PRINT(uri));
        return -EINVAL;
    }

    SOL_DBG("URI Scheme: %.*s", SOL_STR_SLICE_PRINT(*scheme));
    _update_partial_uri(next, scheme->len + 1);

    return 0;
}

static int
_get_authority(const struct sol_str_slice partial_uri,
    const struct sol_str_slice full_uri,
    struct sol_str_slice *next,
    struct sol_str_slice *host,
    struct sol_str_slice *user,
    struct sol_str_slice *pass,
    uint32_t *port)
{
    struct sol_str_slice auth, port_slice = SOL_STR_SLICE_EMPTY;
    const char *itr, *itr_end;
    size_t discarted = 2;
    bool parsing_ipv6;

    if (!partial_uri.len) {
        SOL_WRN("Empty authority. URI: %.*s", SOL_STR_SLICE_PRINT(full_uri));
        return -EINVAL;
    }

    //Not an URL
    if (!sol_str_slice_str_starts_with(partial_uri, "//"))
        return 0;
    if (partial_uri.len == 2)
        return -EINVAL;

    auth = partial_uri;
    auth.data += 2;
    auth.len -= 2;
    itr_end = auth.data + auth.len;
    parsing_ipv6 = false;

    for (itr = auth.data, host->data = auth.data; itr < itr_end; itr++) {
        if (*itr == '[') {
            host->data = itr;
            parsing_ipv6 = true;
        } else if (*itr == ']') {
            parsing_ipv6 = false;
            host->len = itr - host->data;
        } else if (*itr == '@') {
            *user = *host;
            if (!user->len)
                user->len = itr - user->data;
            *pass = port_slice;
            pass->len = itr > pass->data ? itr - pass->data : 0;
            host->data = itr + 1;
            host->len = 0;
            port_slice.data = "";
            port_slice.len = 0;
            discarted++;
        } else if (*itr == ':' && !parsing_ipv6) {
            if (host->len > 0)
                return -EINVAL;
            host->len = itr - host->data;
            port_slice.data = itr + 1;
            discarted++;
        } else if (*itr == '/' || *itr == '?' || *itr == '#')
            break;
    }

    if (parsing_ipv6) {
        SOL_WRN("Malformed IPV6 at URI: %.*s", SOL_STR_SLICE_PRINT(full_uri));
        return -EINVAL;
    }

    if (!host->len)
        host->len = itr - host->data;
    else if (!port_slice.len) {
        port_slice.len = itr - port_slice.data;
        if (port_slice.len) {
            char *endptr;
            *port = sol_util_strtol(port_slice.data, &endptr,
                port_slice.len, 10);
            if (endptr == port_slice.data) {
                SOL_WRN("Could not convert the host port to integer."
                    " Port: %.*s", SOL_STR_SLICE_PRINT(port_slice));
                return -EINVAL;
            }
        }
    }

    SOL_DBG("User:%.*s Host:%.*s Pass%.*s Port:%.*s",
        SOL_STR_SLICE_PRINT(*user), SOL_STR_SLICE_PRINT(*host),
        SOL_STR_SLICE_PRINT(*pass), SOL_STR_SLICE_PRINT(port_slice));

    _update_partial_uri(next,
        port_slice.len + host->len + pass->len + user->len + discarted);
    return 0;
}

static int
_get_path(const struct sol_str_slice partial_uri,
    const struct sol_str_slice full_uri,
    struct sol_str_slice *next,
    struct sol_str_slice *path)
{
    const char *itr, *itr_end;

    if (!partial_uri.len)
        return 0;

    if (partial_uri.data[0] == '#' || partial_uri.data[0] == '?')
        return 0;

    path->data = partial_uri.data;
    itr_end = partial_uri.data + partial_uri.len;

    for (itr = path->data; itr < itr_end; itr++) {
        if (*itr == '?' || *itr == '#')
            break;
    }

    path->len = itr - path->data;

    _update_partial_uri(next, path->len);

    SOL_DBG("URI Path: %.*s", SOL_STR_SLICE_PRINT(*path));
    return 0;
}

static int
_get_query(const struct sol_str_slice partial_uri,
    const struct sol_str_slice full_uri,
    struct sol_str_slice *next,
    struct sol_str_slice *query)
{
    char *sep;

    if (!partial_uri.len)
        return 0;

    if (partial_uri.data[0] != '?')
        return 0;

    query->data = partial_uri.data + 1;

    sep = memchr(partial_uri.data, '#', partial_uri.len);

    if (sep)
        query->len = sep - query->data;
    else
        query->len = partial_uri.len - 1;

    _update_partial_uri(next, query->len + 1);

    SOL_DBG("Query params: %.*s", SOL_STR_SLICE_PRINT(*query));
    return 0;
}

static int
_get_fragment(const struct sol_str_slice partial_uri,
    const struct sol_str_slice full_uri,
    struct sol_str_slice *next,
    struct sol_str_slice *fragment)
{

    if (!partial_uri.len)
        return 0;

    if (partial_uri.data[0] != '#') {
        SOL_WRN("A \"#\" is required in order to identify the fragment."
            "URI: %.*s", SOL_STR_SLICE_PRINT(full_uri));
        return -EINVAL;
    }

    fragment->data = partial_uri.data + 1;
    fragment->len = partial_uri.len - 1;

    SOL_DBG("URI Fragment: %.*s", SOL_STR_SLICE_PRINT(*fragment));
    return 0;
}

SOL_API int
sol_http_split_uri(const struct sol_str_slice full_uri,
    struct sol_http_url *url)
{
    struct sol_str_slice partial_uri;
    int r;

    SOL_NULL_CHECK(url, -EINVAL);

    if (!full_uri.len) {
        SOL_WRN("Empty URI");
        return -EINVAL;
    }

    memset(url, 0, sizeof(struct sol_http_url));
    SOL_DBG("Splitting URI: %.*s", SOL_STR_SLICE_PRINT(full_uri));

    partial_uri = full_uri;
    r = _get_scheme(partial_uri, &partial_uri, &url->scheme);
    SOL_INT_CHECK(r, < 0, r);
    r = _get_authority(partial_uri, full_uri, &partial_uri, &url->host,
        &url->user, &url->password, &url->port);
    SOL_INT_CHECK(r, < 0, r);
    r = _get_path(partial_uri, full_uri, &partial_uri, &url->path);
    SOL_INT_CHECK(r, < 0, r);
    r = _get_query(partial_uri, full_uri, &partial_uri, &url->query);
    SOL_INT_CHECK(r, < 0, r);
    r = _get_fragment(partial_uri, full_uri, &partial_uri, &url->fragment);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}
