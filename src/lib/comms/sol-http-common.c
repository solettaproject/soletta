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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#include "sol-http.h"
#include "sol-log.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

SOL_API int
sol_http_params_add(struct sol_http_params *params,
    struct sol_http_param_value value)
{
    struct sol_http_param_value *ptr;

    SOL_NULL_CHECK(params, -EINVAL);

    SOL_HTTP_PARAMS_CHECK_API_VERSION(params, -EINVAL);

    ptr = sol_vector_append(&params->params);
    if (!ptr) {
        SOL_WRN("Could not append option to parameter vector");
        return -ENOMEM;
    }

    memcpy(ptr, &value, sizeof(value));
    return 0;
}

SOL_API int
sol_http_params_add_copy(struct sol_http_params *params,
    struct sol_http_param_value value)
{
    struct sol_http_param_value *ptr;
    int r;

    SOL_NULL_CHECK(params, -EINVAL);
    SOL_HTTP_PARAMS_CHECK_API_VERSION(params, -EINVAL);

    if (!params->arena) {
        params->arena = sol_arena_new();
        SOL_NULL_CHECK(params->arena, -ENOMEM);
    }

    if (value.type == SOL_HTTP_PARAM_QUERY_PARAM ||
        value.type == SOL_HTTP_PARAM_COOKIE ||
        value.type == SOL_HTTP_PARAM_POST_FIELD ||
        value.type == SOL_HTTP_PARAM_HEADER) {
        if (value.value.key_value.key.len) {
            r = sol_arena_slice_dup(params->arena, &value.value.key_value.key,
                value.value.key_value.key);
            SOL_INT_CHECK(r, < 0, r);
        }
        if (value.value.key_value.value.len) {
            r = sol_arena_slice_dup(params->arena, &value.value.key_value.value,
                value.value.key_value.value);
            SOL_INT_CHECK(r, < 0, r);
        }
    } else if (value.type == SOL_HTTP_PARAM_POST_DATA) {
        if (value.value.data.value.len) {
            r = sol_arena_slice_dup(params->arena,
                &value.value.data.value,
                value.value.data.value);
        } else if (value.value.data.filename.len) {
            r = sol_arena_slice_dup(params->arena,
                &value.value.data.filename,
                value.value.data.filename);
        } else {
            SOL_WRN("POSTDATA must contain data or a filename");
            return -EINVAL;
        }
        SOL_INT_CHECK(r, < 0, r);
        r = sol_arena_slice_dup(params->arena,
            &value.value.data.key,
            value.value.data.key);
        SOL_INT_CHECK(r, < 0, r);
    } else if (value.type == SOL_HTTP_PARAM_AUTH_BASIC) {
        if (value.value.auth.user.len) {
            r = sol_arena_slice_dup(params->arena, &value.value.auth.user,
                value.value.auth.user);
            SOL_INT_CHECK(r, < 0, r);
        }
        if (value.value.auth.password.len) {
            r = sol_arena_slice_dup(params->arena, &value.value.auth.password,
                value.value.auth.password);
            SOL_INT_CHECK(r, < 0, r);
        }
    }

    ptr = sol_vector_append(&params->params);
    if (!ptr) {
        SOL_WRN("Could not append option to parameter vector");
        return -ENOMEM;
    }

    memcpy(ptr, &value, sizeof(value));
    return 0;
}

SOL_API void
sol_http_params_clear(struct sol_http_params *params)
{
    SOL_NULL_CHECK(params);

#ifndef SOL_NO_API_VERSION
    if (params->api_version != SOL_HTTP_PARAM_API_VERSION) {
        SOL_ERR("API version mistmatch; expected %" PRIu16 ", got %" PRIu16,
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

    /* Empty slice. Just return an empty buffer */
    if (!value.len)
        return 0;

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
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
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

    /* Empty slice. Just return an empty buffer */
    if (!value.len)
        return 0;

    last_append = 0;

    for (i = 0; i < value.len; i++) {
        unsigned char c = value.data[i];
        if (c == '%' && value.len - i > 2 && isxdigit((uint8_t)value.data[i + 1])
            && isxdigit((uint8_t)value.data[i + 2])) {
            struct sol_str_slice hex;
            ssize_t err;
            char chex;

            hex.data = value.data + i + 1;
            hex.len = 2;
            err = sol_util_base16_decode(&chex, 1, hex,
                SOL_DECODE_BOTH);
            r = (int)err;
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
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
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
    const struct sol_http_params *params)
{
    const char *prefix, *suffix;
    struct sol_buffer encoded_key, encoded_value;
    struct sol_http_param_value *iter;
    uint16_t idx;
    bool first = true;
    int r;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_NULL_CHECK(params, -EINVAL);
    SOL_HTTP_PARAMS_CHECK_API_VERSION(params, -EINVAL);

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

    SOL_HTTP_PARAMS_FOREACH_IDX (params, iter, idx) {
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
    enum sol_http_param_type type, struct sol_http_params *params)
{
    struct sol_buffer decoded_key, decoded_value;
    struct sol_str_slice *token;
    struct sol_vector tokens;
    const char *param_sep;
    char *sep;
    uint16_t i;
    int r = 0;

    SOL_NULL_CHECK(params, -EINVAL);
    SOL_HTTP_PARAMS_CHECK_API_VERSION(params, -EINVAL);

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

    tokens = sol_str_slice_split(params_slice, param_sep, 0);
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

        if (sol_http_params_add_copy(params, param) < 0) {
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

static bool
is_host_ipv6(const struct sol_str_slice host)
{
    return memchr(host.data, ':', host.len) != NULL;
}

SOL_API int
sol_http_create_full_uri(struct sol_buffer *buf, const struct sol_http_url url,
    const struct sol_http_params *params)
{
    struct sol_str_slice scheme;
    struct sol_buffer buf_encoded;
    size_t used;
    int r;

    SOL_NULL_CHECK(buf, -EINVAL);

    used = buf->used;

    if (url.scheme.len)
        scheme = url.scheme;
    else
        scheme = sol_str_slice_from_str("http");

    r = sol_buffer_append_slice(buf, scheme);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    r = sol_buffer_append_char(buf, ':');
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    if (url.host.len > 0) {
        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("//"));
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        if (url.user.len > 0) {
            r = sol_http_encode_slice(&buf_encoded, url.user);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
            r = sol_buffer_append_slice(buf,
                sol_buffer_get_slice(&buf_encoded));
            sol_buffer_fini(&buf_encoded);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }

        if (url.password.len > 0) {
            r = sol_buffer_append_char(buf, ':');
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
            r = sol_http_encode_slice(&buf_encoded, url.password);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
            r = sol_buffer_append_slice(buf,
                sol_buffer_get_slice(&buf_encoded));
            sol_buffer_fini(&buf_encoded);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }

        if (url.user.len > 0 || url.password.len > 0) {
            r = sol_buffer_append_char(buf, '@');
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }

        if (is_host_ipv6(url.host) && url.host.data[0] != '[') {
            r = sol_buffer_append_printf(buf, "[%.*s]",
                SOL_STR_SLICE_PRINT(url.host));
        } else
            r = sol_buffer_append_slice(buf, url.host);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        if (url.port > 0) {
            r = sol_buffer_append_printf(buf, ":%" PRIu32, url.port);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }
    }

    r = sol_buffer_append_slice(buf, url.path);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    if (params && params->params.len) {
        size_t aux_used;

        r = -EINVAL;
        SOL_HTTP_PARAMS_CHECK_API_VERSION_GOTO(params, err_exit);

        r = sol_buffer_append_char(buf, '?');
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        aux_used = buf->used;
        r = sol_http_encode_params(buf, SOL_HTTP_PARAM_QUERY_PARAM,
            params);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        if (aux_used == buf->used)
            buf->used--;
    }

    if (url.fragment.len) {
        r = sol_buffer_append_char(buf, '#');
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        r = sol_buffer_append_slice(buf, url.fragment);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    if (SOL_BUFFER_NEEDS_NUL_BYTE(buf)) {
        r = sol_buffer_ensure_nul_byte(buf);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    return 0;

err_exit:
    buf->used = used;
    return r;
}

static struct sol_str_slice *
_find_fragment(const struct sol_http_params *params)
{
    uint16_t idx;
    struct sol_http_param_value *itr;

    SOL_HTTP_PARAMS_FOREACH_IDX (params, itr, idx) {
        if (itr->type == SOL_HTTP_PARAM_FRAGMENT)
            return &itr->value.key_value.key;
    }

    return NULL;
}

SOL_API int
sol_http_create_uri(struct sol_buffer *buf, const struct sol_str_slice base_uri,
    const struct sol_http_params *params)
{
    struct sol_str_slice *fragment;
    size_t used;
    int r;

    SOL_NULL_CHECK(buf, -EINVAL);

    used = buf->used;

    if (base_uri.len == 0) {
        SOL_WRN("base_url is empty!");
        return -EINVAL;
    }

    r = sol_buffer_append_slice(buf, base_uri);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    if (params && params->params.len) {
        size_t aux_used;

        r = -EINVAL;
        SOL_HTTP_PARAMS_CHECK_API_VERSION_GOTO(params, err_exit);

        r = sol_buffer_append_char(buf, '?');
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        aux_used = buf->used;
        r = sol_http_encode_params(buf, SOL_HTTP_PARAM_QUERY_PARAM,
            params);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        if (aux_used == buf->used)
            buf->used--;

        fragment = _find_fragment(params);

        if (fragment && fragment->len) {
            r = sol_buffer_append_char(buf, '#');
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
            r = sol_buffer_append_slice(buf, *fragment);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        }
    }

    if (SOL_BUFFER_NEEDS_NUL_BYTE(buf)) {
        r = sol_buffer_ensure_nul_byte(buf);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    return 0;

err_exit:
    buf->used = used;
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
    const char *itr, *itr_end, *port_start;
    size_t discarted = 2;
    bool parsing_ipv6, parsed_ipv6;

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
    port_start = NULL;
    parsing_ipv6 = parsed_ipv6 = false;

    for (itr = auth.data, host->data = auth.data; itr < itr_end; itr++) {
        if (*itr == '[') {
            host->data = itr + 1;
            parsing_ipv6 = true;
            discarted++;
        } else if (*itr == ']') {
            parsing_ipv6 = false;
            parsed_ipv6 = true;
            host->len = itr - host->data;
            discarted++;
        } else if (*itr == '@') {
            *user = *host;
            if (!user->len && port_slice.data != port_start)
                user->len = itr - user->data;
            *pass = port_slice;
            pass->len = itr > pass->data ? itr - pass->data : 0;
            host->data = itr + 1;
            host->len = 0;
            port_slice.data = "";
            port_slice.len = 0;
            port_start = NULL;
            discarted++;
        } else if (*itr == ':' && !parsing_ipv6) {
            if (host->len > 0 && !parsed_ipv6)
                return -EINVAL;
            if (!host->len)
                host->len = itr - host->data;
            port_start = itr + 1;
            port_slice.data = port_start;
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
    else if (!port_slice.len && port_start) {
        port_slice.len = itr - port_slice.data;
        if (port_slice.len) {
            char *endptr;
            *port = sol_util_strtol_n(port_slice.data, &endptr,
                port_slice.len, 10);
            if (endptr == port_slice.data) {
                SOL_WRN("Could not convert the host port to integer."
                    " Port: %.*s", SOL_STR_SLICE_PRINT(port_slice));
                return -EINVAL;
            }
        }
    }

    SOL_DBG("User: %.*s Host: %.*s Pass: %.*s Port: %.*s",
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

static int
sol_http_split_str_key_value(const char *query, const enum sol_http_param_type type,
    struct sol_http_params *params)
{
    struct sol_vector tokens;
    struct sol_str_slice *token;
    char *sep;
    uint16_t i;

    SOL_NULL_CHECK(params, -EINVAL);
    SOL_HTTP_PARAMS_CHECK_API_VERSION(params, -EINVAL);

    tokens = sol_str_slice_split(sol_str_slice_from_str(query), "&", 0);

#define CREATE_PARAM(_key, _value) \
    (struct sol_http_param_value) { \
        .type = type, \
        .value.key_value.key = _key, \
        .value.key_value.value = _value \
    }

    SOL_VECTOR_FOREACH_IDX (&tokens, token, i) {
        struct sol_str_slice key, value;

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

        if (sol_http_params_add_copy(params,
            CREATE_PARAM(key, value)) < 0) {
            SOL_ERR("Could not add the HTTP param %.*s:%.*s",
                SOL_STR_SLICE_PRINT(key), SOL_STR_SLICE_PRINT(value));
            goto exit;
        }
    }
#undef CREATE_PARAM

exit:
    sol_vector_clear(&tokens);
    return 0;
}

SOL_API int
sol_http_split_query(const char *query, struct sol_http_params *params)
{
    return sol_http_split_str_key_value(query, SOL_HTTP_PARAM_QUERY_PARAM, params);
}

SOL_API int
sol_http_split_post_field(const char *query, struct sol_http_params *params)
{
    return sol_http_split_str_key_value(query, SOL_HTTP_PARAM_POST_FIELD, params);
}

static int
sort_priority(const void *v1, const void *v2)
{
    const struct sol_http_content_type_priority *pri1, *pri2;
    bool any_type1, any_type2;

    pri1 = (const struct sol_http_content_type_priority *)v1;
    pri2 = (const struct sol_http_content_type_priority *)v2;

    any_type1 = sol_str_slice_str_eq(pri1->type, "*");
    any_type2 = sol_str_slice_str_eq(pri2->type, "*");

    //text/html have precedence over */* for example.
    if (!any_type1 && any_type2)
        return -1;
    if (any_type1 && !any_type2)
        return 1;

    //Higher qvalue first
    if (pri1->qvalue > pri2->qvalue)
        return -1;
    if (pri1->qvalue < pri2->qvalue)
        return 1;

    //Specialized first
    if (pri1->tokens.len > pri2->tokens.len)
        return -1;
    if (pri1->tokens.len < pri2->tokens.len)
        return 1;

    //Specialized first
    if (sol_str_slice_eq(pri1->type, pri2->type)) {
        //text/html have precedence over text/* for example.
        any_type1 = sol_str_slice_str_eq(pri1->sub_type, "*");
        any_type2 = sol_str_slice_str_eq(pri2->sub_type, "*");

        if (!any_type1 && any_type2)
            return -1;
        if (any_type1 && !any_type2)
            return 1;
    }

    //Precedence must prevail
    if (pri1->index > pri2->index)
        return 1;
    if (pri1->index < pri2->index)
        return -1;

    return 0;
}

static struct sol_str_slice
is_qvalue_token(const struct sol_str_slice param)
{
    const char *itr, *itr_end;
    struct sol_str_slice qvalue = SOL_STR_SLICE_EMPTY;

    enum { STATE_NEEDS_Q_CHAR, STATE_NEEDS_EQUAL_CHAR } state = STATE_NEEDS_Q_CHAR;

    itr = param.data;
    itr_end = itr + param.len;

    for (; itr < itr_end; itr++) {
        if (isspace((int)*itr))
            continue;

        switch (state) {
        case STATE_NEEDS_Q_CHAR:
            if (*itr != 'q')
                return qvalue;
            state = STATE_NEEDS_EQUAL_CHAR;
            break;
        default:
            if (*itr != '=')
                return qvalue;
            qvalue.data = itr + 1;
            qvalue.len = param.len - (itr - param.data) - 1;
            return sol_str_slice_trim(qvalue);
        }
    }

    return qvalue;
}

static int
set_type_and_sub_type(const struct sol_str_slice content_type,
    struct sol_str_slice *type, struct sol_str_slice *sub_type)
{
    char *sep;

    sep = memchr(content_type.data, '/', content_type.len);
    SOL_NULL_CHECK(sep, -EINVAL);

    type->data = content_type.data;
    type->len = sep - content_type.data;

    sub_type->data = sep + 1;
    sub_type->len = content_type.len - type->len - 1;

    return 0;
}

SOL_API int
sol_http_parse_content_type_priorities(const struct sol_str_slice content_type,
    struct sol_vector *priorities)
{
    struct sol_str_slice type;
    const char *itr_type = NULL;
    uint16_t i = 0;

    SOL_NULL_CHECK(priorities, -EINVAL);

    SOL_DBG("Parsing content priorities for: %.*s",
        SOL_STR_SLICE_PRINT(content_type));
    sol_vector_init(priorities, sizeof(struct sol_http_content_type_priority));

    while (sol_str_slice_str_split_iterate(content_type, &type, &itr_type, ",")) {
        struct sol_http_content_type_priority *pri = NULL;
        struct sol_str_slice token;
        struct sol_str_slice qvalue_slice;
        const char *itr_token = NULL;

        SOL_DBG("Content type:%.*s %zu", SOL_STR_SLICE_PRINT(type), type.len);

        while (sol_str_slice_str_split_iterate(type, &token, &itr_token, ";")) {
            token = sol_str_slice_trim(token);

            SOL_DBG("Clean token: %.*s", SOL_STR_SLICE_PRINT(token));
            if (!pri) {
                int r;
                pri = sol_vector_append(priorities);
                SOL_NULL_CHECK_GOTO(pri, err_append);
                pri->content_type = token;
                pri->index = i++;
                pri->qvalue = 1.0;
                sol_vector_init(&pri->tokens, sizeof(struct sol_str_slice));
                r = set_type_and_sub_type(pri->content_type,
                    &pri->type, &pri->sub_type);
                SOL_INT_CHECK_GOTO(r, < 0, err_set_type);
                continue;
            }

            qvalue_slice = is_qvalue_token(token);

            if (!qvalue_slice.len) {
                struct sol_str_slice *p;

                p = sol_vector_append(&pri->tokens);
                SOL_NULL_CHECK_GOTO(p, err_append);
                *p = token;
                SOL_DBG("Adding token: %.*s for %.*s",
                    SOL_STR_SLICE_PRINT(*p),
                    SOL_STR_SLICE_PRINT(pri->content_type));
            } else {
                double qvalue;
                char *endptr = NULL;

                qvalue = sol_util_strtod_n(qvalue_slice.data, &endptr,
                    qvalue_slice.len, false);
                if (errno < 0 || endptr == qvalue_slice.data) {
                    SOL_WRN("Could not convert the value from the content type: %.*s",
                        SOL_STR_SLICE_PRINT(type));
                    goto err_convert;
                }

                if (qvalue > 1.0) {
                    SOL_INF("The qvalue '%g' for %.*s is bigger than 1.0. Using 1.0",
                        pri->qvalue, SOL_STR_SLICE_PRINT(pri->content_type));
                } else
                    pri->qvalue = qvalue;
                SOL_DBG("Type:%.*s with qvalue: %g",
                    SOL_STR_SLICE_PRINT(pri->content_type), pri->qvalue);
            }
        }
    }

    qsort(priorities->data, priorities->len, priorities->elem_size,
        sort_priority);

    return 0;

err_set_type:
err_convert:
    sol_http_content_type_priorities_array_clear(priorities);
    return -EINVAL;
err_append:
    sol_http_content_type_priorities_array_clear(priorities);
    return -ENOMEM;
}

SOL_API void
sol_http_content_type_priorities_array_clear(struct sol_vector *priorities)
{
    uint16_t i;
    struct sol_http_content_type_priority *pri;

    SOL_NULL_CHECK(priorities);

    SOL_VECTOR_FOREACH_IDX (priorities, pri, i)
        sol_vector_clear(&pri->tokens);
    sol_vector_clear(priorities);
}
