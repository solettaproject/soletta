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

#include "sol-flow/json.h"
#include "sol-flow-internal.h"

#include <sol-json.h>
#include <sol-util.h>
#include <sol-types.h>
#include <sol-buffer.h>
#include <errno.h>

struct sol_json_object_key {
    struct sol_blob *json_object;
    char *key;
    int (*process)(struct sol_flow_node *node,
        struct sol_json_object_key *mdata);
};

static int json_object_key_process(struct sol_flow_node *node, struct sol_json_object_key *mdata);
static int json_object_path_process(struct sol_flow_node *node, struct sol_json_object_key *mdata);
static bool json_array_get_at_index(struct sol_json_scanner *scanner, struct sol_json_token *token, int32_t i, enum sol_json_loop_reason *reason);

static int
json_object_get_key_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_json_object_key *mdata = data;
    const struct sol_flow_node_type_json_object_get_key_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_json_object_get_key_options *)
        options;

    mdata->process = json_object_key_process;
    mdata->key = strdup(opts->key);
    SOL_NULL_CHECK(mdata->key, -ENOMEM);

    return 0;
}

static int
json_object_get_path_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_json_object_key *mdata = data;
    const struct sol_flow_node_type_json_object_get_path_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_PATH_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_json_object_get_path_options *)
        options;

    mdata->process = json_object_path_process;
    mdata->key = strdup(opts->path);
    SOL_NULL_CHECK(mdata->key, -ENOMEM);

    return 0;
}

static void
json_object_get_key_close(struct sol_flow_node *node, void *data)
{
    struct sol_json_object_key *mdata = data;

    if (mdata->json_object)
        sol_blob_unref(mdata->json_object);
    free(mdata->key);
}

static struct sol_blob *
create_sub_json(struct sol_blob *parent, struct sol_json_scanner *scanner, struct sol_json_token *token, enum sol_json_type type)
{
    const char *mem;

    if (sol_json_mem_get_type(token->end - 1) == type)
        return sol_blob_new(SOL_BLOB_TYPE_NOFREE, parent, token->start,
            token->end - token->start);

    mem = token->start;
    if (!sol_json_scanner_skip_over(scanner, token))
        return NULL;

    if (sol_json_token_get_type(token) != type) {
        errno = EINVAL;
        return NULL;
    }

    return sol_blob_new(SOL_BLOB_TYPE_NOFREE, parent, mem, token->end - mem);
}

struct json_path_scanner {
    const char *path;
    const char *end;
    const char *current;
};

static void
json_path_scanner_init(struct json_path_scanner *scanner, struct sol_str_slice path)
{
    scanner->path = path.data;
    scanner->end = path.data + path.len;
    scanner->current = path.data;
}

static bool
json_path_parse_key_in_brackets(struct json_path_scanner *scanner, struct sol_str_slice *slice)
{
    const char *p;

    p = scanner->current + 1;

    //index is a string
    if (*p == '\'') {
        //Look for first unescaped '
        for (p = p + 1; p < scanner->end; p++) {
            p = memchr(p, '\'', scanner->end - p);
            if (!p)
                return false;
            if (*(p - 1) != '\\') //is not escaped
                break;
        }
        p++;
        if (p >= scanner->end || *p != ']')
            return false;
    } else if (*p != ']') { //index is is not empty and is suppose to be a num
        p++;
        p = memchr(p, ']', scanner->end - p);
        if (!p)
            return false;

    } else
        return false;

    p++;
    *slice = SOL_STR_SLICE_STR(scanner->current, p - scanner->current);
    scanner->current += slice->len;
    return true;
}

static inline const char *
get_lowest_not_null_pointer(const char *p, const char *p2)
{
    if (p && (!p2 || p < p2))
        return p;

    return p2;
}

static bool
json_path_parse_key_after_dot(struct json_path_scanner *scanner, struct sol_str_slice *slice)
{
    const char *start, *first_dot, *first_bracket_start, *end;

    start = scanner->current + 1;
    first_dot = memchr(start, '.', scanner->end - start);
    first_bracket_start = memchr(start, '[', scanner->end - start);

    end = get_lowest_not_null_pointer(first_dot, first_bracket_start);
    if (end == NULL)
        end = scanner->end;

    if (end == start)
        return false;

    *slice = SOL_STR_SLICE_STR(start, end - start);
    scanner->current = start + slice->len;
    return true;
}

static bool
json_path_get_next_slice(struct json_path_scanner *scanner, struct sol_str_slice *slice, enum sol_json_loop_reason *end_reason)
{
    if (scanner->path == scanner->end) {
        *end_reason = SOL_JSON_LOOP_REASON_INVALID;
        return false;
    }

    //Root element
    if (scanner->current == scanner->path) {
        if (scanner->path[0] != '$') {
            *end_reason = SOL_JSON_LOOP_REASON_INVALID;
            return false;
        }
        scanner->current = scanner->path + 1;
    }

    if (scanner->current >= scanner->end) {
        slice->data = scanner->end;
        slice->len = 0;
        return false;
    }

    if (*scanner->current == '[') {
        if (json_path_parse_key_in_brackets(scanner, slice))
            return true;
    } else if (*scanner->current == '.') {
        if (json_path_parse_key_after_dot(scanner, slice))
            return true;
    }

    *end_reason = SOL_JSON_LOOP_REASON_INVALID;
    return false;
}

static bool
json_object_search_for_token(struct sol_json_scanner *scanner, const struct sol_str_slice key_slice, struct sol_json_token *value)
{
    struct sol_json_token token, key;
    enum sol_json_loop_reason reason;

    SOL_JSON_SCANNER_OBJECT_LOOP_NEST (scanner, &token, &key, value, reason)
        if (sol_json_token_str_eq(&key, key_slice.data, key_slice.len))
            return true;

    return false;
}

static int
send_token_packet(struct sol_flow_node *node, struct sol_json_scanner *scanner, struct sol_blob *json, struct sol_json_token *token)
{
    enum sol_json_type type;
    struct sol_blob *new_blob;
    struct sol_irange value_int = SOL_IRANGE_INIT();
    struct sol_drange value_float = SOL_DRANGE_INIT();
    struct sol_str_slice slice;
    char *str;
    int r;

    type = sol_json_token_get_type(token);
    switch (type) {
    case SOL_JSON_TYPE_OBJECT_START:
        new_blob = create_sub_json(json, scanner, token,
            SOL_JSON_TYPE_OBJECT_END);
        SOL_NULL_CHECK(new_blob, -errno);
        r = sol_flow_send_json_object_packet(node,
            SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY__OUT__OBJECT,
            new_blob);
        sol_blob_unref(new_blob);
        return r;
    case SOL_JSON_TYPE_ARRAY_START:
        if (sol_json_token_get_size(token) > 1) {
            new_blob = sol_blob_new(SOL_BLOB_TYPE_NOFREE,
                json, token->start, sol_json_token_get_size(token));
            SOL_NULL_CHECK(new_blob, -errno);
            r = sol_flow_send_json_array_packet(node,
                SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY__OUT__ARRAY, new_blob);
            sol_blob_unref(new_blob);
            return r;
        }
        new_blob = create_sub_json(json, scanner, token,
            SOL_JSON_TYPE_ARRAY_END);
        SOL_NULL_CHECK(new_blob, -errno);
        r = sol_flow_send_json_array_packet(node,
            SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY__OUT__ARRAY,
            new_blob);
        sol_blob_unref(new_blob);
        return r;
    case SOL_JSON_TYPE_TRUE:
        return sol_flow_send_boolean_packet(node,
            SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY__OUT__BOOLEAN,
            true);
    case SOL_JSON_TYPE_FALSE:
        return sol_flow_send_boolean_packet(node,
            SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY__OUT__BOOLEAN,
            false);
    case SOL_JSON_TYPE_NULL:
        return sol_flow_send_empty_packet(node,
            SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY__OUT__NULL);
    case SOL_JSON_TYPE_STRING:
        str = sol_json_token_get_unescaped_string_copy(token);
        SOL_NULL_CHECK_GOTO(str, error);
        return sol_flow_send_string_take_packet(node,
            SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY__OUT__STRING, str);
    case SOL_JSON_TYPE_NUMBER:
        r = sol_json_token_get_double(token, &value_float.val);
        SOL_INT_CHECK(r, < 0, r);
        r = sol_flow_send_drange_packet(node,
            SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY__OUT__FLOAT,
            &value_float);
        SOL_INT_CHECK(r, < 0, r);

        r = sol_json_token_get_int32(token, &value_int.val);
        if (r == -EINVAL) /* Not an integer number */
            return 0;
        SOL_INT_CHECK(r, < 0, r);
        return sol_flow_send_irange_packet(node,
            SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY__OUT__INT,
            &value_int);
    default:
        break;
    }

error:
    slice = sol_json_token_to_slice(token);
    return sol_flow_send_error_packet(node, EINVAL,
        "JSON Object value %.*s is invalid",
        SOL_STR_SLICE_PRINT(slice));
}

static int
json_object_key_process(struct sol_flow_node *node, struct sol_json_object_key *mdata)
{
    struct sol_json_token value, token;
    struct sol_json_scanner scanner;

    if (!mdata->key[0] || !mdata->json_object)
        return 0;

    sol_json_scanner_init(&scanner, mdata->json_object->mem,
        mdata->json_object->size);
    if (_sol_json_loop_helper_init(&scanner, &token,
        SOL_JSON_TYPE_OBJECT_START) != SOL_JSON_LOOP_REASON_OK)
        return false;

    if (json_object_search_for_token(&scanner,
        sol_str_slice_from_str(mdata->key), &value))
        return send_token_packet(node, &scanner, mdata->json_object, &value);

    return sol_flow_send_error_packet(node, EINVAL,
        "JSON object doesn't contain key %s", mdata->key);
}

/*
 * If index is between [' and '] we need to unescape the ' char and to remove
 * the [' and '] separator.
 */
static int
json_path_parse_object_key(const struct sol_str_slice slice, struct sol_buffer *buffer)
{
    const char *end, *p, *p2;
    struct sol_str_slice key;
    int r;

    if (slice.data[0] != '[') {
        sol_buffer_init_flags(buffer, (char *)slice.data, slice.len,
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
        buffer->used = buffer->capacity;
        return 0;
    }

    //remove [' and ']
    key = SOL_STR_SLICE_STR(slice.data + 2, slice.len - 4);

    //unescape '\'' if necessary
    sol_buffer_init_flags(buffer, NULL, 0, SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    end = key.data + key.len;
    for (p = key.data; p < end; p = p2 + 1) {
        p2 = memchr(p, '\'', end - p);
        if (!p2)
            break;

        //Append string preceding '
        r = sol_buffer_append_slice(buffer, SOL_STR_SLICE_STR(p, p2 - p - 1));
        SOL_INT_CHECK(r, < 0, r);
        r = sol_buffer_append_char(buffer, '\'');
        SOL_INT_CHECK(r, < 0, r);
    }

    if (!buffer->data) {
        sol_buffer_init_flags(buffer, (char *)key.data, key.len,
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
        buffer->used = buffer->capacity;
        return 0;
    }

    //Append the string leftover
    r = sol_buffer_append_slice(buffer, SOL_STR_SLICE_STR(p, end - p));
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static inline bool
json_path_is_array_key(struct sol_str_slice slice)
{
    return slice.data[0] == '[' &&  //is between brackets or
           slice.data[1] != '\''; //index is not a string
}

#define JSON_PATH_FOREACH(scanner, key, end_reason) \
    for (end_reason = SOL_JSON_LOOP_REASON_OK; \
        json_path_get_next_slice(&scanner, &key_slice, &end_reason);)

static int
json_object_path_process(struct sol_flow_node *node, struct sol_json_object_key *mdata)
{
    enum sol_json_type type;
    struct sol_str_slice key_slice = SOL_STR_SLICE_EMPTY;
    struct sol_buffer current_key;
    struct sol_json_token value, token;
    struct sol_json_scanner json_scanner;
    struct json_path_scanner path_scanner;
    enum sol_json_loop_reason array_reason, reason;
    char *endptr;
    long int index_val;
    bool found;
    int r;

    if (!mdata->key[0] || !mdata->json_object)
        return 0;

    sol_json_scanner_init(&json_scanner, mdata->json_object->mem,
        mdata->json_object->size);
    json_path_scanner_init(&path_scanner, sol_str_slice_from_str(mdata->key));

    JSON_PATH_FOREACH(path_scanner, key_slice, reason) {
        type = sol_json_mem_get_type(json_scanner.current);
        if (_sol_json_loop_helper_init(&json_scanner, &token, type) !=
            SOL_JSON_LOOP_REASON_OK)
            goto error;

        switch (type) {
        case SOL_JSON_TYPE_OBJECT_START:
            if (json_path_is_array_key(key_slice))
                goto error;

            r = json_path_parse_object_key(key_slice, &current_key);
            SOL_INT_CHECK(r, < 0, r);

            found = json_object_search_for_token(&json_scanner,
                sol_buffer_get_slice(&current_key), &value);
            sol_buffer_fini(&current_key);
            if (!found)
                goto error;
            break;
        case SOL_JSON_TYPE_ARRAY_START:
            if (!json_path_is_array_key(key_slice))
                goto error;

            errno = 0;
            index_val = strtol(key_slice.data + 1, &endptr, 10);
            if (endptr != key_slice.data + key_slice.len - 1 ||
                index_val < 0 || errno > 0)
                goto error;

            if (!json_array_get_at_index(&json_scanner, &value, index_val,
                &array_reason))
                goto error;
            break;
        default:
            goto error;
        }

        json_scanner.current = value.start;
    }
    if (reason != SOL_JSON_LOOP_REASON_OK)
        goto error;

    //If path is root
    if (json_scanner.current == mdata->json_object->mem) {
        //TODO: use send_token_packet
        type = sol_json_mem_get_type(json_scanner.current);
        if (type == SOL_JSON_TYPE_OBJECT_START)
            return sol_flow_send_json_object_packet(node,
                SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_PATH__OUT__OBJECT,
                mdata->json_object);
        else
            return sol_flow_send_json_array_packet(node,
                SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_PATH__OUT__ARRAY,
                mdata->json_object);
    }

    return send_token_packet(node, &json_scanner, mdata->json_object, &value);

error:
    return sol_flow_send_error_packet(node, EINVAL,
        "JSON element doesn't contain path %s", mdata->key);
}

#undef JSON_PATH_FOREACH

static int
json_object_get_key_key_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_json_object_key *mdata = data;
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->key);
    mdata->key = strdup(in_value);
    SOL_NULL_CHECK(mdata->key, -ENOMEM);

    return mdata->process(node, mdata);
}

static int
json_array_get_path_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_json_object_key *mdata = data;
    int r;
    struct sol_blob *in_value;

    r = sol_flow_packet_get_json_array(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->json_object)
        sol_blob_unref(mdata->json_object);
    mdata->json_object = sol_blob_ref(in_value);
    SOL_NULL_CHECK(mdata->json_object, -errno);

    return mdata->process(node, mdata);
}

static int
json_object_get_key_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_json_object_key *mdata = data;
    int r;
    struct sol_blob *in_value;

    r = sol_flow_packet_get_json_object(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->json_object)
        sol_blob_unref(mdata->json_object);
    mdata->json_object = sol_blob_ref(in_value);
    SOL_NULL_CHECK(mdata->json_object, -errno);

    return mdata->process(node, mdata);
}

static int
json_object_length_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_blob *in_value;
    struct sol_json_scanner scanner;
    enum sol_json_loop_reason reason;
    struct sol_json_token token, key, value;
    struct sol_irange len = { 0, 0, INT32_MAX, 1 };

    r = sol_flow_packet_get_json_object(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    sol_json_scanner_init(&scanner, in_value->mem, in_value->size);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (len.val == INT32_MAX)
            return -ERANGE;

        len.val++;
    }

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_JSON_OBJECT_LENGTH__OUT__OUT, &len);
}

static int
json_object_get_all_keys_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_blob *in_value;
    struct sol_json_scanner scanner;
    enum sol_json_loop_reason reason;
    struct sol_json_token token, key, value;
    struct sol_buffer buffer;
    bool empty = true;

    r = sol_flow_packet_get_json_object(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    sol_json_scanner_init(&scanner, in_value->mem, in_value->size);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        r = sol_json_token_get_unescaped_string(&key, &buffer);
        SOL_INT_CHECK(r, < 0, r);
        r = sol_flow_send_string_slice_packet(node,
            SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_ALL_KEYS__OUT__OUT,
            sol_buffer_get_slice(&buffer));
        sol_buffer_fini(&buffer);
        SOL_INT_CHECK(r, < 0, r);

        empty = false;
    }

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_ALL_KEYS__OUT__EMPTY, empty);
}

struct sol_json_array_index {
    struct sol_blob *json_array;
    int32_t index;
};

static bool
json_array_get_at_index(struct sol_json_scanner *scanner, struct sol_json_token *token, int32_t i, enum sol_json_loop_reason *reason)
{
    int32_t cur_index = 0;

    SOL_JSON_SCANNER_ARRAY_LOOP_ALL_NEST(scanner, token, *reason) {
        if (i == cur_index)
            return true;

        if (!sol_json_scanner_skip_over(scanner, token))
            return false;
        cur_index++;
    }

    return false;
}

static int
json_array_get_index_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_json_array_index *mdata = data;
    const struct sol_flow_node_type_json_array_get_at_index_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_JSON_ARRAY_GET_AT_INDEX_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_json_array_get_at_index_options *)
        options;

    mdata->index = opts->index.val;

    return 0;
}

static void
json_array_get_index_close(struct sol_flow_node *node, void *data)
{
    struct sol_json_array_index *mdata = data;

    if (mdata->json_array)
        sol_blob_unref(mdata->json_array);
}

static int
json_array_index_process(struct sol_flow_node *node, struct sol_json_array_index *mdata)
{
    struct sol_json_scanner scanner;
    enum sol_json_loop_reason reason;
    struct sol_json_token token;

    if (mdata->index < 0 || !mdata->json_array)
        return 0;

    sol_json_scanner_init(&scanner, mdata->json_array->mem,
        mdata->json_array->size);
    if (_sol_json_loop_helper_init(&scanner, &token,
        SOL_JSON_TYPE_ARRAY_START) != SOL_JSON_LOOP_REASON_OK)
        goto invalid_array;

    if (json_array_get_at_index(&scanner, &token, mdata->index, &reason))
        return send_token_packet(node, &scanner, mdata->json_array, &token);

    if (reason != SOL_JSON_LOOP_REASON_INVALID)
        return sol_flow_send_error_packet(node, EINVAL,
            "JSON array index out of bounds: %" PRId32, mdata->index);
invalid_array:
    return sol_flow_send_error_packet(node, EINVAL,
        "Invalid JSON array (%.*s)", (int)mdata->json_array->size,
        (char *)mdata->json_array->mem);
}

static int
json_array_get_index_index_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_json_array_index *mdata = data;
    int r;
    struct sol_irange in_value;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value.val < 0)
        return sol_flow_send_error_packet(node, EINVAL,
            "Invalid negative JSON array index: %" PRId32, in_value.val);

    mdata->index = in_value.val;
    return json_array_index_process(node, mdata);
}

static int
json_array_get_index_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_json_array_index *mdata = data;
    int r;
    struct sol_blob *in_value;

    r = sol_flow_packet_get_json_array(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->json_array)
        sol_blob_unref(mdata->json_array);
    mdata->json_array = sol_blob_ref(in_value);
    SOL_NULL_CHECK(mdata->json_array, -errno);

    return json_array_index_process(node, mdata);
}

static int
json_array_length_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_blob *in_value;
    struct sol_json_scanner scanner;
    enum sol_json_loop_reason reason;
    struct sol_json_token token;
    struct sol_irange len = { 0, 0, INT32_MAX, 1 };

    r = sol_flow_packet_get_json_array(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    sol_json_scanner_init(&scanner, in_value->mem, in_value->size);
    SOL_JSON_SCANNER_ARRAY_LOOP_ALL(&scanner, &token, reason) {
        if (!sol_json_scanner_skip_over(&scanner, &token))
            return -EINVAL;

        if (len.val == INT32_MAX)
            return -ERANGE;
        len.val++;
    }

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_JSON_ARRAY_LENGTH__OUT__OUT, &len);
}

static int
json_array_get_all_elements_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_blob *json_array;
    struct sol_json_scanner scanner;
    enum sol_json_loop_reason reason;
    struct sol_json_token token;
    bool empty = true;

    r = sol_flow_packet_get_json_array(packet, &json_array);
    SOL_INT_CHECK(r, < 0, r);

    sol_json_scanner_init(&scanner, json_array->mem, json_array->size);
    SOL_JSON_SCANNER_ARRAY_LOOP_ALL(&scanner, &token, reason) {
        r = send_token_packet(node, &scanner, json_array, &token);
        SOL_INT_CHECK(r, < 0, r);
        empty = false;
    }

    if (reason == SOL_JSON_LOOP_REASON_INVALID)
        return sol_flow_send_error_packet(node, EINVAL,
            "Invalid JSON array (%.*s)", (int)json_array->size,
            (char *)json_array->mem);

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_JSON_ARRAY_GET_ALL_ELEMENTS__OUT__EMPTY, empty);
}

#include "json-gen.c"
