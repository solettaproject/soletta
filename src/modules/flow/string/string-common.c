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

#include "sol-flow/string.h"
#include "sol-flow-internal.h"

#include "string-common.h"

int
string_is_empty(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_IS_EMPTY__OUT__OUT, strlen(in_value) == 0);
}

int
string_b64encode_string(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_b64_data *mdata = data;
    const char *in_value;
    char *output;
    struct sol_buffer buf;
    struct sol_str_slice slice;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    slice = sol_str_slice_from_str(in_value);

    sol_buffer_init(&buf);
    r = sol_buffer_append_as_base64(&buf, slice, mdata->base64_map);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    output = sol_buffer_steal(&buf, NULL);
    sol_buffer_fini(&buf);

    return sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_B64ENCODE__OUT__OUT, output);

error:
    sol_flow_send_error_packet(node, -r,
        "Could not encode string '%s' to base64: %s",
        in_value, sol_util_strerrora(-r));
    sol_buffer_fini(&buf);
    return r;
}

int
string_b64encode_blob(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_b64_data *mdata = data;
    struct sol_blob *in_value;
    char *output;
    struct sol_buffer buf;
    struct sol_str_slice slice;
    int r;

    r = sol_flow_packet_get_blob(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    slice = sol_str_slice_from_blob(in_value);

    sol_buffer_init(&buf);
    r = sol_buffer_append_as_base64(&buf, slice, mdata->base64_map);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    output = sol_buffer_steal(&buf, NULL);
    sol_buffer_fini(&buf);

    return sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_B64ENCODE__OUT__OUT, output);

error:
    sol_flow_send_error_packet(node, -r,
        "Could not encode blob mem=%p, size=%zd to base64: %s",
        in_value->mem, in_value->size, sol_util_strerrora(-r));
    sol_buffer_fini(&buf);
    return r;
}

int
string_b64decode(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_b64decode_data *mdata = data;
    const char *in_value;
    char *output;
    struct sol_buffer buf;
    struct sol_str_slice slice;
    size_t outputlen;
    int r;

    if (mdata->string_conns == 0 && mdata->blob_conns == 0)
        return 0;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    slice = sol_str_slice_from_str(in_value);

    sol_buffer_init(&buf);
    r = sol_buffer_append_from_base64(&buf, slice, mdata->base64_map);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    output = sol_buffer_steal(&buf, &outputlen);
    sol_buffer_fini(&buf);

    if (mdata->string_conns > 0 && mdata->blob_conns == 0) {
        return sol_flow_send_string_take_packet(node,
            SOL_FLOW_NODE_TYPE_STRING_B64DECODE__OUT__STRING, output);
    } else if (mdata->string_conns == 0 && mdata->blob_conns > 0) {
        struct sol_blob *blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT,
            NULL, output, outputlen);
        SOL_NULL_CHECK_GOTO(blob, error_blob);
        r = sol_flow_send_blob_packet(node,
            SOL_FLOW_NODE_TYPE_STRING_B64DECODE__OUT__BLOB, blob);
        sol_blob_unref(blob);
        return r;
    } else {
        struct sol_blob *blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT,
            NULL, output, outputlen);
        int r2;
        SOL_NULL_CHECK_GOTO(blob, error_blob);

        r = sol_flow_send_string_packet(node,
            SOL_FLOW_NODE_TYPE_STRING_B64DECODE__OUT__STRING, output);

        r2 = sol_flow_send_blob_packet(node,
            SOL_FLOW_NODE_TYPE_STRING_B64DECODE__OUT__BLOB, blob);
        sol_blob_unref(blob);

        return r || r2;
    }

error:
    sol_flow_send_error_packet(node, -r,
        "Could not decode string '%s' to base64: %s",
        in_value, sol_util_strerrora(-r));
    sol_buffer_fini(&buf);
    return r;

error_blob:
    free(output);
    return -ENOMEM;
}

int
string_b64decode_port_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct string_b64decode_data *mdata = data;

    if (port == SOL_FLOW_NODE_TYPE_STRING_B64DECODE__OUT__STRING)
        mdata->string_conns++;
    else if (port == SOL_FLOW_NODE_TYPE_STRING_B64DECODE__OUT__BLOB)
        mdata->blob_conns++;
    else
        return -EINVAL;

    return 0;
}

int
string_b64decode_port_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct string_b64decode_data *mdata = data;

    if (port == SOL_FLOW_NODE_TYPE_STRING_B64DECODE__OUT__STRING &&
        mdata->string_conns > 0)
        mdata->string_conns--;
    else if (port == SOL_FLOW_NODE_TYPE_STRING_B64DECODE__OUT__BLOB &&
        mdata->blob_conns > 0)
        mdata->blob_conns--;
    else
        return -EINVAL;

    return 0;
}

int
string_b64_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_b64_data *mdata = data;
    const struct sol_flow_node_type_string_b64encode_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (options, SOL_FLOW_NODE_TYPE_STRING_B64ENCODE_OPTIONS_API_VERSION, -EINVAL);
    /* both b64encode and b64decode have the same options structure, however
     * the generator emits different symbols. Let's pick one.
     */
    opts = (const struct sol_flow_node_type_string_b64encode_options *)options;

    if (!opts->base64_map ||
        (opts->base64_map[0] == '\0' ||
        streq(opts->base64_map, SOL_BASE64_MAP)))
        mdata->base64_map = SOL_BASE64_MAP;
    else if (opts->base64_map) {
        if (strlen(opts->base64_map) != 65) {
            SOL_WRN("Invalid base64_map of length %zd, must be 65: %s. "
                "Using default '%.65s'",
                strlen(opts->base64_map), opts->base64_map, SOL_BASE64_MAP);
            mdata->base64_map = SOL_BASE64_MAP;
        } else {
            mdata->base64_map = strdup(opts->base64_map);
            if (!mdata->base64_map) {
                SOL_WRN("Could not allocate memory for custom base64_map '%.65s'."
                    "Using default '%.65s'",
                    opts->base64_map, SOL_BASE64_MAP);
                mdata->base64_map = SOL_BASE64_MAP;
            }
        }
    }

    return 0;
}

void
string_b64_close(struct sol_flow_node *node, void *data)
{
    struct string_b64_data *mdata = data;

    if (mdata->base64_map != SOL_BASE64_MAP)
        free((char *)mdata->base64_map);
}

int
string_b16encode_string(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_b16_data *mdata = data;
    const char *in_value;
    char *output;
    struct sol_buffer buf;
    struct sol_str_slice slice;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    slice = sol_str_slice_from_str(in_value);

    sol_buffer_init(&buf);
    r = sol_buffer_append_as_base16(&buf, slice, mdata->uppercase);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    output = sol_buffer_steal(&buf, NULL);
    sol_buffer_fini(&buf);

    return sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_B16ENCODE__OUT__OUT, output);

error:
    sol_flow_send_error_packet(node, -r,
        "Could not encode string '%s' to base16: %s",
        in_value, sol_util_strerrora(-r));
    sol_buffer_fini(&buf);
    return r;
}

int
string_b16encode_blob(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_b16_data *mdata = data;
    struct sol_blob *in_value;
    char *output;
    struct sol_buffer buf;
    struct sol_str_slice slice;
    int r;

    r = sol_flow_packet_get_blob(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    slice = sol_str_slice_from_blob(in_value);

    sol_buffer_init(&buf);
    r = sol_buffer_append_as_base16(&buf, slice, mdata->uppercase);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    output = sol_buffer_steal(&buf, NULL);
    sol_buffer_fini(&buf);

    return sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_B16ENCODE__OUT__OUT, output);

error:
    sol_flow_send_error_packet(node, -r,
        "Could not encode blob mem=%p, size=%zd to base16: %s",
        in_value->mem, in_value->size, sol_util_strerrora(-r));
    sol_buffer_fini(&buf);
    return r;
}

int
string_b16decode(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_b16decode_data *mdata = data;
    const char *in_value;
    char *output;
    struct sol_buffer buf;
    struct sol_str_slice slice;
    size_t outputlen;
    int r;

    if (mdata->string_conns == 0 && mdata->blob_conns == 0)
        return 0;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    slice = sol_str_slice_from_str(in_value);

    sol_buffer_init(&buf);
    r = sol_buffer_append_from_base16(&buf, slice,
        mdata->uppercase ? SOL_DECODE_UPPERCASE : SOL_DECODE_LOWERCASE);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    output = sol_buffer_steal(&buf, &outputlen);
    sol_buffer_fini(&buf);

    if (mdata->string_conns > 0 && mdata->blob_conns == 0) {
        return sol_flow_send_string_take_packet(node,
            SOL_FLOW_NODE_TYPE_STRING_B16DECODE__OUT__STRING, output);
    } else if (mdata->string_conns == 0 && mdata->blob_conns > 0) {
        struct sol_blob *blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT,
            NULL, output, outputlen);
        SOL_NULL_CHECK_GOTO(blob, error_blob);
        r = sol_flow_send_blob_packet(node,
            SOL_FLOW_NODE_TYPE_STRING_B16DECODE__OUT__BLOB, blob);
        sol_blob_unref(blob);
        return r;
    } else {
        struct sol_blob *blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT,
            NULL, output, outputlen);
        int r2;
        SOL_NULL_CHECK_GOTO(blob, error_blob);

        r = sol_flow_send_string_packet(node,
            SOL_FLOW_NODE_TYPE_STRING_B16DECODE__OUT__STRING, output);

        r2 = sol_flow_send_blob_packet(node,
            SOL_FLOW_NODE_TYPE_STRING_B16DECODE__OUT__BLOB, blob);
        sol_blob_unref(blob);

        return r || r2;
    }

error:
    sol_flow_send_error_packet(node, -r,
        "Could not decode string '%s' to base16: %s",
        in_value, sol_util_strerrora(-r));
    sol_buffer_fini(&buf);
    return r;

error_blob:
    free(output);
    return -ENOMEM;
}

int
string_b16decode_port_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct string_b16decode_data *mdata = data;

    if (port == SOL_FLOW_NODE_TYPE_STRING_B16DECODE__OUT__STRING)
        mdata->string_conns++;
    else if (port == SOL_FLOW_NODE_TYPE_STRING_B16DECODE__OUT__BLOB)
        mdata->blob_conns++;
    else
        return -EINVAL;

    return 0;
}

int
string_b16decode_port_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct string_b16decode_data *mdata = data;

    if (port == SOL_FLOW_NODE_TYPE_STRING_B16DECODE__OUT__STRING &&
        mdata->string_conns > 0)
        mdata->string_conns--;
    else if (port == SOL_FLOW_NODE_TYPE_STRING_B16DECODE__OUT__BLOB &&
        mdata->blob_conns > 0)
        mdata->blob_conns--;
    else
        return -EINVAL;

    return 0;
}

int
string_b16_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_b16_data *mdata = data;
    const struct sol_flow_node_type_string_b16encode_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (options, SOL_FLOW_NODE_TYPE_STRING_B16ENCODE_OPTIONS_API_VERSION, -EINVAL);
    /* both b16encode and b16decode have the same options structure, however
     * the generator emits different symbols. Let's pick one.
     */
    opts = (const struct sol_flow_node_type_string_b16encode_options *)options;
    mdata->uppercase = opts->uppercase;

    return 0;
}
