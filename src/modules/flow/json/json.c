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

struct sol_json_node_data {
    struct sol_blob *json_element;
    char *key;
};

struct json_node_type {
    struct sol_flow_node_type base;
    int (*process)(struct sol_flow_node *node,
        struct sol_json_node_data *mdata);
    int (*get_packet_data)(const struct sol_flow_packet *packet, struct sol_blob **value);
};

static int
json_node_key_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_json_node_data *mdata = data;
    const struct sol_flow_node_type_json_object_get_key_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_json_object_get_key_options *)
        options;

    mdata->key = strdup(opts->key);
    SOL_NULL_CHECK(mdata->key, -ENOMEM);

    return 0;
}

static void
json_node_close(struct sol_flow_node *node, void *data)
{
    struct sol_json_node_data *mdata = data;

    if (mdata->json_element)
        sol_blob_unref(mdata->json_element);
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
    double value_float;
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
        r = sol_json_token_get_double(token, &value_float);
        SOL_INT_CHECK(r, < 0, r);
        r = sol_flow_send_drange_value_packet(node,
            SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY__OUT__FLOAT,
            value_float);
        SOL_INT_CHECK(r, < 0, r);

        if (value_float < INT32_MAX && value_float > INT32_MIN)
            return sol_flow_send_irange_value_packet(node,
                SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_KEY__OUT__INT,
                value_float);
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
json_object_key_process(struct sol_flow_node *node, struct sol_json_node_data *mdata)
{
    struct sol_json_token value, token;
    struct sol_json_scanner scanner;

    if (!mdata->key[0] || !mdata->json_element)
        return 0;

    sol_json_scanner_init(&scanner, mdata->json_element->mem,
        mdata->json_element->size);
    if (_sol_json_loop_helper_init(&scanner, &token,
        SOL_JSON_TYPE_OBJECT_START) != SOL_JSON_LOOP_REASON_OK)
        return false;

    if (json_object_search_for_token(&scanner,
        sol_str_slice_from_str(mdata->key), &value))
        return send_token_packet(node, &scanner, mdata->json_element, &value);

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

static inline bool
json_path_is_array_key(struct sol_str_slice slice)
{
    return slice.data[0] == '[' &&  //is between brackets or
           slice.data[1] != '\''; //index is not a string
}

#define JSON_PATH_FOREACH(scanner, key, end_reason) \
    for (end_reason = SOL_JSON_LOOP_REASON_OK; \
        json_path_get_next_slice(&scanner, &key_slice, &end_reason);)

static int32_t
json_array_get_key_index(struct sol_str_slice key)
{
    int index_val;
    int r;

    r = sol_str_slice_to_int(SOL_STR_SLICE_STR(key.data + 1, key.len - 2),
        &index_val);
    SOL_INT_CHECK(r, < 0, r);

    if ((int)(uint16_t)index_val != index_val)
        return -ERANGE;

    return index_val;
}

static int
json_object_path_process(struct sol_flow_node *node, struct sol_json_node_data *mdata)
{
    enum sol_json_type type;
    struct sol_str_slice key_slice = SOL_STR_SLICE_EMPTY;
    struct sol_buffer current_key;
    struct sol_json_token value, token;
    struct sol_json_scanner json_scanner;
    struct json_path_scanner path_scanner;
    enum sol_json_loop_reason array_reason, reason;
    int32_t index_val;
    bool found;
    int r;

    if (!mdata->key[0] || !mdata->json_element)
        return 0;

    sol_json_scanner_init(&json_scanner, mdata->json_element->mem,
        mdata->json_element->size);
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

            index_val = json_array_get_key_index(key_slice);
            SOL_INT_CHECK_GOTO(index_val, < 0, error);

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
    if (json_scanner.current == mdata->json_element->mem) {
        type = sol_json_mem_get_type(json_scanner.current);
        if (type == SOL_JSON_TYPE_OBJECT_START)
            return sol_flow_send_json_object_packet(node,
                SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_PATH__OUT__OBJECT,
                mdata->json_element);
        else
            return sol_flow_send_json_array_packet(node,
                SOL_FLOW_NODE_TYPE_JSON_OBJECT_GET_PATH__OUT__ARRAY,
                mdata->json_element);
    }

    return send_token_packet(node, &json_scanner, mdata->json_element, &value);

error:
    return sol_flow_send_error_packet(node, EINVAL,
        "JSON element doesn't contain path %s", mdata->key);
}

static int
json_node_get_key_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_json_node_data *mdata = data;
    struct json_node_type *type;
    int r;
    const char *in_value;

    type = (struct json_node_type *)sol_flow_node_get_type(node);
    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->key);
    mdata->key = strdup(in_value);
    SOL_NULL_CHECK(mdata->key, -ENOMEM);

    return type->process(node, mdata);
}

static int
json_node_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct json_node_type *type;
    struct sol_json_node_data *mdata = data;
    int r;
    struct sol_blob *in_value;

    type = (struct json_node_type *)sol_flow_node_get_type(node);
    r = type->get_packet_data(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->json_element)
        sol_blob_unref(mdata->json_element);
    mdata->json_element = sol_blob_ref(in_value);
    SOL_NULL_CHECK(mdata->json_element, -errno);

    return type->process(node, mdata);
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

    mdata->index = opts->index;

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

enum json_element_type {
    JSON_TYPE_UNKNOWN = 0,
    JSON_TYPE_INT,
    JSON_TYPE_STRING,
    JSON_TYPE_BOOLEAN,
    JSON_TYPE_FLOAT,
    JSON_TYPE_ARRAY,
    JSON_TYPE_OBJECT,
    JSON_TYPE_ARRAY_BLOB,
    JSON_TYPE_OBJECT_BLOB,
    JSON_TYPE_NULL,
};

struct json_element {
    enum json_element_type type;
    union {
        int32_t int_value;
        char *str;
        bool bool_value;
        double float_value;
        struct sol_vector children;
        struct sol_blob *blob;
    };
};

struct json_key_element {
    char *key;
    struct json_element element;
};

struct json_node_create_type {
    struct sol_flow_node_type base;
    int (*send_json_packet) (struct sol_flow_node *src, uint16_t src_port, const struct sol_blob *value);
    int (*add_new_element) (struct sol_flow_node *node, struct json_element *base_element, const char *key, struct json_element *new_element);
};

static void
json_element_clear(struct json_element *element)
{
    struct json_element *var;
    struct json_key_element *key_element;
    uint16_t i;

    switch (element->type) {
    case JSON_TYPE_STRING:
        free(element->str);
        break;
    case JSON_TYPE_OBJECT:
        SOL_VECTOR_FOREACH_IDX (&element->children, key_element, i) {
            free(key_element->key);
            json_element_clear(&key_element->element);
        }
        sol_vector_clear(&element->children);
        break;
    case JSON_TYPE_ARRAY:
        SOL_VECTOR_FOREACH_IDX (&element->children, var, i)
            json_element_clear(var);
        sol_vector_clear(&element->children);
        break;
    case JSON_TYPE_ARRAY_BLOB:
    case JSON_TYPE_OBJECT_BLOB:
        sol_blob_unref(element->blob);
        break;
    default:
        break;
    }
}

static void
init_json_array_element(struct json_element *element)
{
    element->type = JSON_TYPE_ARRAY;
    sol_vector_init(&element->children, sizeof(struct json_element));

}

static void
init_json_object_element(struct json_element *element)
{
    element->type = JSON_TYPE_OBJECT;
    sol_vector_init(&element->children, sizeof(struct json_key_element));
}

static int
json_array_create_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    init_json_array_element(data);

    return 0;
}

static int
json_object_create_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    init_json_object_element(data);

    return 0;
}

static void
json_create_close(struct sol_flow_node *node, void *data)
{
    struct json_element *mdata = data;

    json_element_clear(mdata);
}

static int
json_array_create_count(struct sol_flow_node *node, struct json_element *mdata)
{
    struct sol_irange count = SOL_IRANGE_INIT();

    count.val = mdata->children.len;
    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_JSON_CREATE_ARRAY__OUT__COUNT,
        &count);
}

static int
json_node_fill_element(const struct sol_flow_packet *packet, uint16_t port, struct json_element *element)
{
    int r;
    double dval;
    int32_t ival;
    struct sol_blob *blob;
    const char *str;

    switch (port) {
    case SOL_FLOW_NODE_TYPE_JSON_CREATE_OBJECT__IN__INT:
        element->type = JSON_TYPE_INT;
        r = sol_flow_packet_get_irange_value(packet, &ival);
        SOL_INT_CHECK(r, < 0, r);
        element->int_value = ival;
        break;
    case SOL_FLOW_NODE_TYPE_JSON_CREATE_OBJECT__IN__FLOAT:
        element->type = JSON_TYPE_FLOAT;
        r = sol_flow_packet_get_drange_value(packet, &dval);
        SOL_INT_CHECK(r, < 0, r);
        element->float_value = dval;
        break;
    case SOL_FLOW_NODE_TYPE_JSON_CREATE_OBJECT__IN__BOOLEAN:
        element->type = JSON_TYPE_BOOLEAN;
        r = sol_flow_packet_get_boolean(packet, &element->bool_value);
        SOL_INT_CHECK(r, < 0, r);
        break;
    case SOL_FLOW_NODE_TYPE_JSON_CREATE_OBJECT__IN__STRING:
        element->type = JSON_TYPE_STRING;
        r = sol_flow_packet_get_string(packet, &str);
        SOL_INT_CHECK(r, < 0, r);
        element->str = strdup(str);
        SOL_NULL_CHECK(element->str, -ENOMEM);
        break;
    case SOL_FLOW_NODE_TYPE_JSON_CREATE_OBJECT__IN__ARRAY:
        element->type = JSON_TYPE_ARRAY_BLOB;
        r = sol_flow_packet_get_json_array(packet, &blob);
        SOL_INT_CHECK(r, < 0, r);
        element->blob = sol_blob_ref(blob);
        SOL_NULL_CHECK(element->blob, -ENOMEM);
        break;
    case SOL_FLOW_NODE_TYPE_JSON_CREATE_OBJECT__IN__OBJECT:
        element->type = JSON_TYPE_OBJECT_BLOB;
        r = sol_flow_packet_get_json_object(packet, &blob);
        SOL_INT_CHECK(r, < 0, r);
        element->blob = sol_blob_ref(blob);
        SOL_NULL_CHECK(element->blob, -ENOMEM);
        break;
    case SOL_FLOW_NODE_TYPE_JSON_CREATE_OBJECT__IN__NULL:
        element->type = JSON_TYPE_NULL;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static int
json_array_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct json_element *new, *mdata = data;
    int r;

    new = sol_vector_append(&mdata->children);
    SOL_NULL_CHECK(new, -errno);

    r = json_node_fill_element(packet, port, new);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return json_array_create_count(node, mdata);

error:
    sol_vector_del_element(&mdata->children, new);
    return r;
}

static int
json_serialize_blob(struct sol_buffer *buffer, struct sol_blob *blob)
{
    int r;
    char *p;

    if (!blob->size)
        return 0;

    r = sol_buffer_append_slice(buffer, sol_str_slice_from_blob(blob));
    SOL_INT_CHECK(r, < 0, r);

    if (buffer->used == 0)
        return 0;

    p = sol_buffer_at(buffer, buffer->used - 1);
    if (p && *p == '\0')
        buffer->used--;
    return 0;
}

static int json_serialize(struct sol_buffer *buffer, struct json_element *element);

static int
json_serialize_key_element(struct sol_buffer *buffer, struct json_key_element *key_element)
{
    int r;

    r = sol_json_serialize_string(buffer, key_element->key);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_buffer_append_char(buffer, ':');
    SOL_INT_CHECK(r, < 0, r);

    return json_serialize(buffer, &key_element->element);
}

static int
json_serialize(struct sol_buffer *buffer, struct json_element *element)
{
    struct json_element *var;
    struct json_key_element *key_element;
    uint16_t i;
    int r;

    switch (element->type) {
    case JSON_TYPE_OBJECT:
        r = sol_buffer_append_char(buffer, '{');
        SOL_INT_CHECK(r, < 0, r);

        SOL_VECTOR_FOREACH_IDX (&element->children, key_element, i) {
            if (i > 0) {
                r = sol_buffer_append_char(buffer, ',');
                SOL_INT_CHECK(r, < 0, r);
            }
            r = json_serialize_key_element(buffer, key_element);
            SOL_INT_CHECK(r, < 0, r);
        }
        return sol_buffer_append_char(buffer, '}');
    case JSON_TYPE_ARRAY:
        r = sol_buffer_append_char(buffer, '[');
        SOL_INT_CHECK(r, < 0, r);

        SOL_VECTOR_FOREACH_IDX (&element->children, var, i) {
            if (i > 0)
                r = sol_buffer_append_char(buffer, ',');
            r = json_serialize(buffer, var);
            SOL_INT_CHECK(r, < 0, r);
        }
        return sol_buffer_append_char(buffer, ']');
    case JSON_TYPE_UNKNOWN:
    case JSON_TYPE_NULL:
        return sol_json_serialize_null(buffer);
    case JSON_TYPE_INT:
        return sol_json_serialize_int32(buffer, element->int_value);
    case JSON_TYPE_FLOAT:
        return sol_json_serialize_double(buffer, element->float_value);
    case JSON_TYPE_BOOLEAN:
        return sol_json_serialize_boolean(buffer, element->bool_value);
    case JSON_TYPE_STRING:
        return sol_json_serialize_string(buffer, element->str);
    case JSON_TYPE_ARRAY_BLOB:
    case JSON_TYPE_OBJECT_BLOB:
        return json_serialize_blob(buffer, element->blob);
    }

    return -EINVAL;
}

static struct json_key_element *
json_object_get_or_create_child_element(struct json_element *element, const struct sol_str_slice key_slice)
{
    uint16_t i;
    struct json_key_element *key_element;

    SOL_VECTOR_FOREACH_IDX (&element->children, key_element, i)
        if (sol_str_slice_str_eq(key_slice, key_element->key))
            return key_element;

    key_element = sol_vector_append(&element->children);
    SOL_NULL_CHECK(key_element, NULL);
    key_element->key = sol_str_slice_to_string(key_slice);
    SOL_NULL_CHECK_GOTO(key_element->key, str_error);

    key_element->element.type = JSON_TYPE_UNKNOWN;

    return key_element;

str_error:
    sol_vector_del_element(&element->children, key_element);
    return NULL;
}

static struct json_element *
json_array_get_or_create_child_element(struct json_element *element, uint16_t i)
{
    struct json_element *new;
    uint16_t last_len;

    if (i < element->children.len)
        return sol_vector_get(&element->children, i);

    last_len = element->children.len;
    new = sol_vector_append_n(&element->children,
        i - element->children.len + 1);
    SOL_NULL_CHECK(new, NULL);

    for (i = last_len; i < element->children.len; i++) {
        new = sol_vector_get(&element->children, i);
        new->type = JSON_TYPE_UNKNOWN;
    }

    return new;
}

static int json_element_parse(struct sol_json_token *token, struct json_element *element);

static int
json_element_parse_array(struct sol_json_token *token, struct json_element *element)
{

    struct sol_json_scanner scanner;
    struct sol_json_token child_token;
    struct json_element *new;
    enum sol_json_loop_reason reason;
    int r;

    init_json_array_element(element);

    sol_json_scanner_init(&scanner, token->start, token->end - token->start);
    SOL_JSON_SCANNER_ARRAY_LOOP_ALL(&scanner, token, reason) {
        child_token.start = token->start;
        if (!sol_json_scanner_skip_over(&scanner, token))
            goto error_json;

        child_token.end = token->end;

        new = sol_vector_append(&element->children);
        SOL_NULL_CHECK_GOTO(new, error_json);

        r = json_element_parse(&child_token, new);
        SOL_INT_CHECK_GOTO(r, < 0, error);
    }

    return 0;

error:
    new->type = JSON_TYPE_UNKNOWN;
error_json:
    json_element_clear(element);
    return -EINVAL;
}

static int
json_element_parse_object(struct sol_json_token *token, struct json_element *element)
{
    struct sol_json_scanner scanner;
    struct sol_json_token key, value;
    struct json_key_element *new;
    enum sol_json_loop_reason reason;
    int r = 0;

    init_json_object_element(element);

    sol_json_scanner_init(&scanner, token->start, token->end - token->start);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, token, &key, &value, reason) {
        new = sol_vector_append(&element->children);
        SOL_NULL_CHECK_GOTO(new, error_append);

        new->key = sol_json_token_get_unescaped_string_copy(&key);
        SOL_NULL_CHECK_GOTO(new->key, error_key);

        r = json_element_parse(&value, &new->element);
        SOL_INT_CHECK_GOTO(r, < 0, error_parse);
    }

    return 0;

error_parse:
error_key:
    free(new->key);

error_append:
    if (r == 0)
        r = -ENOMEM;
    json_element_clear(element);

    return r;
}

static int
json_element_parse(struct sol_json_token *token, struct json_element *element)
{
    enum sol_json_type type;
    int r;

    type = sol_json_token_get_type(token);
    switch (type) {
    case SOL_JSON_TYPE_OBJECT_START:
        return json_element_parse_object(token, element);
    case SOL_JSON_TYPE_ARRAY_START:
        return json_element_parse_array(token, element);
    case SOL_JSON_TYPE_TRUE:
        element->type = JSON_TYPE_BOOLEAN;
        element->bool_value = true;
        return 0;
    case SOL_JSON_TYPE_FALSE:
        element->type = JSON_TYPE_BOOLEAN;
        element->bool_value = false;
        return 0;
    case SOL_JSON_TYPE_NULL:
        element->type = JSON_TYPE_NULL;
        return 0;
    case SOL_JSON_TYPE_STRING:
        element->type = JSON_TYPE_STRING;
        element->str = sol_json_token_get_unescaped_string_copy(token);
        SOL_NULL_CHECK(element->str, -ENOMEM);
        return 0;
    case SOL_JSON_TYPE_NUMBER:
        element->type = JSON_TYPE_FLOAT;
        r = sol_json_token_get_double(token, &element->float_value);
        SOL_INT_CHECK(r, < 0, r);
        return 0;
    default:
        return -EINVAL;
    }
}

static int
json_blob_element_parse(struct json_element *element)
{
    struct sol_json_token token;
    struct json_element new_element;
    int ret;

    token.start = element->blob->mem;
    token.end = token.start + element->blob->size;

    ret = json_element_parse(&token, &new_element);
    if (ret == 0) {
        sol_blob_unref(element->blob);
        *element = new_element;
    }

    return ret;
}

static bool
reinit_element_if_needed(struct json_element *cur_element, struct sol_str_slice key_slice, bool is_base_element)
{
    if (json_path_is_array_key(key_slice)) {
        if (cur_element->type == JSON_TYPE_ARRAY ||
            cur_element->type == JSON_TYPE_ARRAY_BLOB)
            return true;

        if (is_base_element)
            return false;

        json_element_clear(cur_element);
        init_json_array_element(cur_element);
        return true;
    }

    if (cur_element->type == JSON_TYPE_OBJECT ||
        cur_element->type == JSON_TYPE_OBJECT_BLOB)
        return true;

    if (is_base_element)
        return false;

    json_element_clear(cur_element);
    init_json_object_element(cur_element);
    return true;
}
static int
json_path_add_new_element(struct sol_flow_node *node, struct json_element *base_element, const char *key, struct json_element *new_element)
{
    struct json_path_scanner path_scanner;
    enum sol_json_loop_reason reason;
    struct sol_str_slice key_slice = SOL_STR_SLICE_EMPTY;
    struct json_element *cur_element;
    struct json_key_element *key_element;
    int32_t index_val;
    int r;

    cur_element = base_element;
    json_path_scanner_init(&path_scanner, sol_str_slice_from_str(key));
    JSON_PATH_FOREACH(path_scanner, key_slice, reason) {
        if (!reinit_element_if_needed(cur_element, key_slice,
            cur_element == base_element))
            goto error;

        switch (cur_element->type) {
        case JSON_TYPE_OBJECT_BLOB:
            r = json_blob_element_parse(cur_element);
            SOL_INT_CHECK(r, == -ENOMEM, r);
            SOL_INT_CHECK_GOTO(r, < 0, error_parse);

        case JSON_TYPE_OBJECT:
            if (json_path_is_array_key(key_slice))
                goto error;

            key_element = json_object_get_or_create_child_element(cur_element,
                key_slice);
            SOL_NULL_CHECK_GOTO(key_element, error);
            cur_element = &key_element->element;

            break;
        case JSON_TYPE_ARRAY_BLOB:
            r = json_blob_element_parse(cur_element);
            SOL_INT_CHECK(r, == -ENOMEM, r);
            SOL_INT_CHECK_GOTO(r, < 0, error_parse);

        case JSON_TYPE_ARRAY:
            if (!json_path_is_array_key(key_slice))
                goto error;

            index_val = json_array_get_key_index(key_slice);
            SOL_INT_CHECK_GOTO(index_val, < 0, error);

            cur_element = json_array_get_or_create_child_element(cur_element,
                index_val);
            SOL_NULL_CHECK_GOTO(cur_element, error);

            break;
        default:
            goto error;
        }
    }

    if (reason != SOL_JSON_LOOP_REASON_OK)
        goto error;

    json_element_clear(cur_element);
    *cur_element = *new_element;

    return 0;

error:
    return sol_flow_send_error_packet(node, EINVAL, "Invalid JSON path %s",
        key);
error_parse:
    SOL_WRN("error parse");
    return sol_flow_send_error_packet(node, EINVAL, "JSON element in path %s"
        " is invalid: %.*s", key,
        SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(cur_element->blob)));
}

#undef JSON_PATH_FOREACH

static int
json_object_add_new_element(struct sol_flow_node *node, struct json_element *base_element, const char *key, struct json_element *new_element)
{
    struct json_key_element *new;

    new = json_object_get_or_create_child_element(base_element,
        sol_str_slice_from_str(key));
    if (!new) {
        new = sol_vector_append(&base_element->children);
        SOL_NULL_CHECK(new, -errno);
        new->key = strdup(key);
        SOL_NULL_CHECK_GOTO(new->key, str_error);
    } else
        json_element_clear(&new->element);
    new->element = *new_element;

    return 0;

str_error:
    sol_vector_del_element(&base_element->children, new);
    return -ENOMEM;
}

static int
json_object_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct json_node_create_type *type;
    struct json_element *mdata = data;
    struct json_element new_element;
    const char *key;
    uint16_t len;
    struct sol_flow_packet **packets;
    int r;

    r = sol_flow_packet_get_composed_members(packet, &packets, &len);
    SOL_INT_CHECK(r, < 0, r);
    SOL_INT_CHECK(len, < 2, -EINVAL);

    r = sol_flow_packet_get_string(packets[0], &key);
    SOL_INT_CHECK(r, < 0, r);

    r = json_node_fill_element(packets[1], port, &new_element);
    SOL_INT_CHECK(r, < 0, r);

    type = (struct json_node_create_type *)sol_flow_node_get_type(node);
    r = type->add_new_element(node, mdata, key, &new_element);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return 0;

error:
    json_element_clear(&new_element);
    return r;
}

static int
json_object_null_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct json_key_element *new;
    struct json_element *mdata = data;
    const char *key;
    int r;

    r = sol_flow_packet_get_string(packet, &key);
    SOL_INT_CHECK(r, < 0, r);

    new = sol_vector_append(&mdata->children);
    SOL_NULL_CHECK(new, -errno);

    new->element.type = JSON_TYPE_NULL;
    new->key = strdup(key);
    SOL_NULL_CHECK_GOTO(new->key, error);
    return 0;

error:
    sol_vector_del_element(&mdata->children, new);
    return -ENOMEM;
}

static int
json_node_create_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct json_node_create_type *type;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    struct sol_blob *blob;
    char *str;
    size_t size;
    int r;
    struct json_element *mdata = data;

    r = json_serialize(&buffer, mdata);
    if (r < 0) {
        sol_buffer_fini(&buffer);
        return r;
    }

    str = sol_buffer_steal(&buffer, &size);
    SOL_NULL_CHECK(str, -ENOMEM);
    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, str, size);
    SOL_NULL_CHECK(blob, -errno);

    type = (struct json_node_create_type *)sol_flow_node_get_type(node);
    r = type->send_json_packet(node,
        SOL_FLOW_NODE_TYPE_JSON_CREATE_OBJECT__OUT__OUT, blob);
    sol_blob_unref(blob);
    return r;
}

static int
json_clear_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct json_element *mdata = data;

    json_element_clear(mdata);

    return 0;
}

#include "json-gen.c"
