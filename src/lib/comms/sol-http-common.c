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

    if (params->api_version != SOL_HTTP_PARAM_API_VERSION) {
        SOL_ERR("API version mistmatch; expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return false;
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

    if (params->api_version != SOL_HTTP_PARAM_API_VERSION) {
        SOL_ERR("API version mistmatch; expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return;
    }
    sol_vector_clear(&params->params);
}

SOL_API int
sol_http_escape_string(char **escaped, const struct sol_str_slice value)
{
    int r;
    size_t i;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;

    SOL_NULL_CHECK(escaped, -EINVAL);


    for (i = 0; i < value.len; i++) {
        unsigned char c = value.data[i];
        switch (c) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case 'a': case 'b': case 'c': case 'd': case 'e':
        case 'f': case 'g': case 'h': case 'i': case 'j':
        case 'k': case 'l': case 'm': case 'n': case 'o':
        case 'p': case 'q': case 'r': case 's': case 't':
        case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
        case 'A': case 'B': case 'C': case 'D': case 'E':
        case 'F': case 'G': case 'H': case 'I': case 'J':
        case 'K': case 'L': case 'M': case 'N': case 'O':
        case 'P': case 'Q': case 'R': case 'S': case 'T':
        case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
        case '_': case '~': case '.': case '-':
            r = sol_buffer_append_char(&buf, c);
            SOL_INT_CHECK_GOTO(r, < 0, end);
            break;
        default:
            r = sol_buffer_append_printf(&buf, "%%%02X", c);
            SOL_INT_CHECK_GOTO(r, < 0, end);
        }
    }

    r = sol_buffer_ensure_nul_byte(&buf);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    *escaped = sol_buffer_steal(&buf, NULL);

end:
    sol_buffer_fini(&buf);
    return r;
}

SOL_API int
sol_http_encode_params(char **encoded_params, enum sol_http_param_type type,
    const struct sol_http_param *params)
{
    struct sol_buffer buf;
    struct sol_http_param_value *iter;
    char *encoded_key, *encoded_value;
    uint16_t idx;
    bool first = true;
    int r;

    SOL_NULL_CHECK(encoded_params, -EINVAL);
    SOL_NULL_CHECK(params, -EINVAL);

    if (type != SOL_HTTP_PARAM_QUERY_PARAM &&
        type != SOL_HTTP_PARAM_POST_FIELD &&
        type != SOL_HTTP_PARAM_COOKIE &&
        type != SOL_HTTP_PARAM_HEADER) {
        return -EINVAL;
    }

    sol_buffer_init(&buf);

    encoded_key = encoded_value = NULL;
    SOL_HTTP_PARAM_FOREACH_IDX (params, iter, idx) {
        if (iter->type != type)
            continue;

        r = sol_http_escape_string(&encoded_key, iter->value.key_value.key);
        SOL_INT_CHECK_GOTO(r, < 0, clean_up);

        r = sol_http_escape_string(&encoded_value, iter->value.key_value.value);
        SOL_INT_CHECK_GOTO(r, < 0, clean_up);

        if (type == SOL_HTTP_PARAM_COOKIE) {
            r = sol_buffer_append_printf(&buf, "%s%s=%s;", first ? "" : " ",
                encoded_key, encoded_value);
        } else {
            r = sol_buffer_append_printf(&buf, "%s%s=%s", first ? "" : "&",
                encoded_key, encoded_value);
        }

        SOL_INT_CHECK_GOTO(r, < 0, clean_up);

        first = false;
        free(encoded_key);
        free(encoded_value);
        encoded_value = encoded_key = NULL;
    }

    r = sol_buffer_ensure_nul_byte(&buf);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    *encoded_params = sol_buffer_steal(&buf, NULL);

    sol_buffer_fini(&buf);

    return 0;

clean_up:
    free(encoded_key);
    free(encoded_value);
err_exit:
    sol_buffer_fini(&buf);
    return r;
}

SOL_API int
sol_http_create_uri(char **uri, const char *protocol,
    const char *server, const char *path, const char *fragment, int port,
    const struct sol_http_param *params)
{
    struct sol_buffer buf;
    char *encoded_params = NULL;
    int r;

    SOL_NULL_CHECK(uri, -EINVAL);
    SOL_NULL_CHECK(server, -EINVAL);

    if (params) {
        r = sol_http_encode_params(&encoded_params, SOL_HTTP_PARAM_QUERY_PARAM,
            params);
        SOL_INT_CHECK(r, < 0, r);
    }

    sol_buffer_init(&buf);

    if (protocol)
        r = sol_buffer_append_printf(&buf, "%s://%s", protocol, server);
    else
        r = sol_buffer_append_printf(&buf, "http://%s", server);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    if (port != 80) {
        r = sol_buffer_append_printf(&buf, ":%d", port);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    if (path && (strlen(path) >= 1 && path[0] != '/')) {
        r = sol_buffer_append_printf(&buf, "%s%s",
            path[0] != '/' ? "/" : "", path);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    if (encoded_params && *encoded_params) {
        r = sol_buffer_append_printf(&buf, "?%s", encoded_params);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    if (fragment && *fragment) {
        r = sol_buffer_append_printf(&buf, "#%s", fragment);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    *uri = sol_buffer_steal(&buf, NULL);
exit:
    sol_buffer_fini(&buf);
    free(encoded_params);
    return r;
}

SOL_API int
sol_http_create_simple_uri(char **uri, const char *base_url,
    const struct sol_http_param *params)
{
    char *encoded_params;
    int r;

    SOL_NULL_CHECK(uri, -EINVAL);
    SOL_NULL_CHECK(base_url, -EINVAL);

    if (params) {
        r = sol_http_encode_params(&encoded_params, SOL_HTTP_PARAM_QUERY_PARAM,
            params);
        SOL_INT_CHECK(r, < 0, r);
    }

    if (asprintf(uri, "%s/%s%s", base_url, *encoded_params ? "?" : "",
        encoded_params) < 0) {
        r = -ENOMEM;
        SOL_ERR("Could not create the URL with the encoded parameters."
            "URL: %s, parameters: %s", base_url, encoded_params);
    }
    free(encoded_params);
    return r;
}

static bool
_is_sub_delimin(char c)
{
    if (c != '!' && c != '$' && c != '&' && c != '\''
        && c != '(' && c != ')' && c != '*' && c != '+' && c != ','
        && c != ';' && c != '=')
        return false;
    return true;
}

static bool
_is_unreserved(char c)
{
    if (!isalnum(c) && c != '-' && c != '.' && c != '_' && c != '~')
        return false;
    return true;
}

static bool
_is_pct_encoded(char c)
{
    if (c != '%')
        return false;
    return true;
}

static bool
_is_pchar(char c)
{
    if (!_is_unreserved(c) && !_is_pct_encoded(c) &&
        !_is_sub_delimin(c) && c != ':' && c != '@')
        return false;
    return true;
}

static int
_get_protocol(const char *uri, struct sol_str_slice *protocol, char **end)
{
    struct sol_str_slice p;
    size_t i;

    p.data = uri;
    p.len = 0;

    for (i = 0; uri[i] && uri[i] != ':'; i++, p.len++) {
        if (!isalnum(uri[i]) && uri[i] != '+' &&
            uri[i] != '-' && uri[i] != '.') {
            SOL_ERR("Invalid character at the URI's scheme: %c", uri[i]);
            return -EINVAL;
        }
    }

    if (uri[i] != ':') {
        SOL_ERR("Could not find the \":\" scheme");
        return -EINVAL;
    }

    SOL_DBG("URI Protocol: %.*s", SOL_STR_SLICE_PRINT(p));
    *end = (char *)p.data + p.len + 1;

    if (protocol)
        *protocol = p;

    return 0;
}

static int
_get_server(char *partial_uri, struct sol_str_slice *server, char **next)
{
    char *i;
    struct sol_str_slice s;
    size_t j;

    for (j = 0; j < 2; j++, partial_uri++) {

        if (!*partial_uri) {
            SOL_ERR("Expecting a \"/\" character to parse the server. "
                "Found: NUL");
            return -EINVAL;
        }

        if (*partial_uri != '/') {
            SOL_ERR("Expecting a \"/\" character to parse the server. "
                "Found: %c", *partial_uri);
            return -EINVAL;
        }
    }

    if (!*partial_uri) {
        SOL_ERR("Missing host information.");
        return -EINVAL;
    }

    s.data = partial_uri;

    for (i = partial_uri; *i && *i != '/' && *i != ':' && *i != '#'
        && *i != '?'; i++) {
        if (!_is_unreserved(*i) && !_is_pct_encoded(*i) &&
            !_is_sub_delimin(*i)) {
            return -EINVAL;
        }
    }

    s.len = i - partial_uri;

    *next = i;
    SOL_DBG("Uri server: %.*s", SOL_STR_SLICE_PRINT(s));

    if (server)
        *server = s;

    return 0;
}

static int
_get_port(char *partial_uri, int *port, char **next)
{
    struct sol_str_slice port_slice = sol_str_slice_from_str("80");
    char *i;
    int r = 0;

    //The server port is not required.
    if (!*partial_uri || *partial_uri != ':')
        goto exit;

    port_slice.data = ++partial_uri;
    for (i = partial_uri; *i && *i != '/' && *i != '?' && *i != '#'; i++) {
        if (!isdigit(*i)) {
            SOL_ERR("Found a non digit while parsing the port. Found: %c", *i);
            return -EINVAL;
        }
    }
    port_slice.len = i - port_slice.data;
    *next = i;

exit:
    SOL_DBG("URI Port: %.*s", SOL_STR_SLICE_PRINT(port_slice));
    if (port) {
        *port = sol_util_strtol(port_slice.data, NULL, port_slice.len, 10);
    }
    return r;
}

static int
_get_path(char *partial_uri, struct sol_str_slice *path, char **next)
{
    struct sol_str_slice p = SOL_STR_SLICE_EMPTY;
    char *i = partial_uri;

    if (!*i)
        goto exit;

    if (*i != '/')
        goto exit;

    i++;

    //Oops, this is not a path.
    if (*i == '?')
        goto exit;

    p.data = i;
    for (; *i && *i != '?' && *i != '#'; i++) {
        if (*i != '/' && !_is_pchar(*i)) {
            SOL_ERR("Invalid character while parsing the path. Found: %c", *i);
            return -EINVAL;
        }
    }
    p.len = i - p.data;

exit:
    SOL_DBG("URI Path: *%.*s*", SOL_STR_SLICE_PRINT(p));
    *next = i;
    if (path)
        *path = p;
    return 0;
}

static int
_get_query(char *partial_uri, struct sol_http_param *query_params, char **next)
{
    struct sol_str_slice query, *token;
    char *i = partial_uri, *sep;
    uint16_t j;
    struct sol_vector tokens;
    int r = 0;

    if (!*i)
        goto exit;

    if (*i != '?')
        goto exit;

    query.data = ++i;

    for (; *i && *i != '#'; i++) {
        if (*i != '/' && *i != '?' && !_is_pchar(*i)) {
            SOL_ERR("Invalid character while parsing the query. "
                "Found: %c", *i);
            return -EINVAL;
        }
    }
    query.len = i - query.data;

    tokens = sol_util_str_split(query, "&", 0);

    SOL_VECTOR_FOREACH_IDX (&tokens, token, j) {
        struct sol_http_param_value param;
        sep = memchr(token->data, '=', token->len);
        if (!sep) {
            SOL_ERR("The param: %.*s has no \"=\" separator",
                SOL_STR_SLICE_PRINT(*token));
            r = -EINVAL;
            break;
        }

        param.value.key_value.key.data = token->data;
        param.value.key_value.key.len = sep - token->data;

        param.value.key_value.value.data = sep + 1;
        param.value.key_value.value.len = token->len -
            param.value.key_value.key.len - 1;

        SOL_DBG("Query key: *%.*s* Query value: *%.*s*",
            SOL_STR_SLICE_PRINT(param.value.key_value.key),
            SOL_STR_SLICE_PRINT(param.value.key_value.value));

        param.type = 0;

        if (!sol_http_param_add(query_params, param)) {
            SOL_ERR("Could not alloc the param %.*s : %.*s",
                SOL_STR_SLICE_PRINT(param.value.key_value.key),
                SOL_STR_SLICE_PRINT(param.value.key_value.value));
            r = -ENOMEM;
            break;
        }
    }

    sol_vector_clear(&tokens);
exit:
    *next = i;
    return r;
}

static int
_get_fragment(char *partial_uri, struct sol_str_slice *fragment)
{
    struct sol_str_slice f = SOL_STR_SLICE_EMPTY;
    char *i = partial_uri;

    if (!*i)
        goto exit;

    if (*i != '#') {
        SOL_ERR("A \"#\" is required in order to identify the fragment");
        return -EINVAL;
    }

    f.data = ++i;
    for (; *i; i++) {
        if (*i != '/' && *i != '?' && !_is_pchar(*i)) {
            SOL_ERR("Invalid character while parsing the fragement. "
                "Found: %c", *i);
            return -EINVAL;
        }
    }
    f.len = i - f.data;

exit:
    SOL_DBG("URI Fragment: %.*s", SOL_STR_SLICE_PRINT(f));
    if (fragment)
        *fragment = f;
    return 0;
}

SOL_API int
sol_http_split_uri(const char *uri, struct sol_str_slice *protocol,
    struct sol_str_slice *server, struct sol_str_slice *path,
    struct sol_str_slice *fragment, struct sol_http_param *query_params,
    int *port)
{
    char *next;
    int r;

    SOL_NULL_CHECK(uri, -EINVAL);

    SOL_DBG("Splitting URI: %s", uri);

    r = _get_protocol(uri, protocol, &next);
    SOL_INT_CHECK(r, < 0, r);
    r = _get_server(next, server, &next);
    SOL_INT_CHECK(r, < 0, r);
    r = _get_port(next, port, &next);
    SOL_INT_CHECK(r, < 0, r);
    r = _get_path(next, path, &next);
    SOL_INT_CHECK(r, < 0, r);
    r = _get_query(next, query_params, &next);
    SOL_INT_CHECK(r, < 0, r);
    r = _get_fragment(next, fragment);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}
