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
};

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

    mdata->key = strdup(opts->key);
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

    mem = token->start;
    if (!sol_json_scanner_skip_over(scanner, token))
        return NULL;

    if (sol_json_token_get_type(token) != type) {
        errno = EINVAL;
        return NULL;
    }

    return sol_blob_new(SOL_BLOB_TYPE_NOFREE, parent, mem, token->end - mem);
}

static bool
json_object_search_for_token(struct sol_json_scanner *scanner, const char *key, struct sol_json_token *value)
{
    struct sol_json_token token, token_key;
    enum sol_json_loop_reason reason;
    unsigned int len;

    len = strlen(key);
    SOL_JSON_SCANNER_OBJECT_LOOP (scanner, &token, &token_key, value, reason)
        if (sol_json_token_str_eq(&token_key, key, len))
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
    struct sol_json_scanner scanner;
    struct sol_json_token value;

    if (!mdata->key[0] || !mdata->json_object)
        return 0;

    sol_json_scanner_init(&scanner, mdata->json_object->mem,
        mdata->json_object->size);

    if (json_object_search_for_token(&scanner, mdata->key, &value))
        return send_token_packet(node, &scanner, mdata->json_object, &value);

    return sol_flow_send_error_packet(node, EINVAL,
        "JSON object doesn't contain key %s", mdata->key);
    return 0;
}

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

    return json_object_key_process(node, mdata);
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

    return json_object_key_process(node, mdata);
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

    SOL_JSON_SCANNER_ARRAY_LOOP_ALL(scanner, token, *reason) {
        if (i == cur_index)
            return true;

        if (!sol_json_scanner_skip_over(scanner, token))
            return -errno;
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

    if (json_array_get_at_index(&scanner, &token, mdata->index, &reason))
        return send_token_packet(node, &scanner, mdata->json_array, &token);

    if (reason == SOL_JSON_LOOP_REASON_INVALID)
        return sol_flow_send_error_packet(node, EINVAL,
            "Invalid JSON array (%.*s)", (int)mdata->json_array->size,
            (char *)mdata->json_array->mem);

    return sol_flow_send_error_packet(node, EINVAL,
        "JSON array index out of bounds: %" PRId32, mdata->index);
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
    JSON_TYPE_INT = 0,
    JSON_TYPE_STRING,
    JSON_TYPE_BOOLEAN,
    JSON_TYPE_FLOAT,
    JSON_TYPE_ARRAY,
    JSON_TYPE_OBJECT,
    JSON_TYPE_ARRAY_BLOB,
    JSON_TYPE_OBJECT_BLOB,
    JSON_TYPE_NULL,
};

struct json_node_create_type {
    struct sol_flow_node_type base;
    int (*send_json_packet) (struct sol_flow_node *src, uint16_t src_port, const struct sol_blob *value);
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

static int
json_array_create_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct json_element *mdata = data;

    mdata->type = JSON_TYPE_ARRAY;
    sol_vector_init(&mdata->children, sizeof(struct json_element));

    return 0;
}

static int
json_object_create_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct json_element *mdata = data;

    mdata->type = JSON_TYPE_OBJECT;
    sol_vector_init(&mdata->children, sizeof(struct json_key_element));

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
    sol_vector_del(&mdata->children, mdata->children.len - 1);
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
            if (i > 0)
                r = sol_buffer_append_char(buffer, ',');
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
    case JSON_TYPE_NULL:
        return sol_json_serialize_null(buffer);
    case JSON_TYPE_INT:
        return sol_json_serialize_int32(buffer, element->int_value);
    case JSON_TYPE_FLOAT:
        return sol_json_serialize_double(buffer, element->float_value);
    case JSON_TYPE_BOOLEAN:
        return sol_json_serialize_int32(buffer, element->bool_value);
    case JSON_TYPE_STRING:
        return sol_json_serialize_string(buffer, element->str);
    case JSON_TYPE_ARRAY_BLOB:
    case JSON_TYPE_OBJECT_BLOB:
        return json_serialize_blob(buffer, element->blob);
    }

    return -EINVAL;
}

static int
json_object_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct json_key_element *new;
    struct json_element *mdata = data;
    const char *key;
    uint16_t len;
    struct sol_flow_packet **packets;
    int r;

    r = sol_flow_packet_get_composed_members(packet, &packets, &len);
    SOL_INT_CHECK(r, < 0, r);
    SOL_INT_CHECK(len, < 2, -EINVAL);

    r = sol_flow_packet_get_string(packets[0], &key);
    SOL_INT_CHECK(r, < 0, r);

    new = sol_vector_append(&mdata->children);
    SOL_NULL_CHECK(new, -errno);

    r = json_node_fill_element(packets[1], port, &new->element);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    new->key = strdup(key);
    SOL_NULL_CHECK_GOTO(new->key, str_error);
    return 0;

str_error:
    r = -ENOMEM;
error:
    sol_vector_del(&mdata->children, mdata->children.len - 1);
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
    sol_vector_del(&mdata->children, mdata->children.len - 1);
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
