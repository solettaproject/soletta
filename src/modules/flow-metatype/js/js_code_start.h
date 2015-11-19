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

#define JS_CODE_START \
    "#include \"duktape.h\"\n"           \
    "struct js_metatype_port_in {\n"               \
    "    struct sol_flow_port_type_in base;\n"     \
    "    const char *name;\n"                      \
    "};\n"                                         \
    "struct js_metatype_port_out {\n"               \
    "    struct sol_flow_port_type_out base;\n"     \
    "    const char *name;\n"                       \
    "};\n"                                                              \
    "enum {\n"                                                          \
    "    PORTS_IN_CONNECT_INDEX,\n"                                     \
    "    PORTS_IN_DISCONNECT_INDEX,\n"                                  \
    "    PORTS_IN_PROCESS_INDEX,\n"                                     \
    "    PORTS_IN_METHODS_LENGTH,\n"                                    \
    "};\n"                                                              \
    "enum {\n"                                                          \
    "    PORTS_OUT_CONNECT_INDEX,\n"                                    \
    "    PORTS_OUT_DISCONNECT_INDEX,\n"                                 \
    "    PORTS_OUT_METHODS_LENGTH,\n"                                   \
    "};\n"                                                              \
    "static struct sol_flow_node *\n"                                   \
    "js_metatype_get_node_from_duk_ctx(duk_context *ctx)\n"             \
    "{\n"                                                               \
    "    struct sol_flow_node *n;\n"                                    \
    "    duk_push_global_object(ctx);\n"                                \
    "    duk_get_prop_string(ctx, -1, \"\\xFF\" \"Soletta_node_pointer\");\n" \
    "    n = duk_require_pointer(ctx, -1);\n"                           \
    "    duk_pop_2(ctx);\n"                                             \
    "    return n;\n"                                                   \
    "}\n"                                                               \
                                                                        \
    /* sendErrorPacket() javascript callback */                         \
    "static duk_ret_t\n"                                                \
    "js_metatype_send_error_packet(duk_context *ctx)\n"                 \
    "{\n"                                                               \
    "    const char *value_msg = NULL;\n"                               \
    "    struct sol_flow_node *node;\n"                                 \
    "    int value_code, r;\n"                                          \
    "    value_code = duk_require_int(ctx, 0);\n"                       \
    "    if (duk_is_string(ctx, 1))\n"                                  \
    "        value_msg = duk_require_string(ctx, 1);\n"                 \
    "    node = js_metatype_get_node_from_duk_ctx(ctx);\n"              \
    "    if (!node) {\n"                                                \
    "        duk_error(ctx, DUK_ERR_ERROR, \"Couldn't send error packet.\");\n" \
    "        return 0;\n"                                               \
    "    }\n"                                                           \
    "    r = sol_flow_send_error_packet_str(node, value_code, value_msg);\n" \
    "    if (r < 0)\n"                                                  \
    "        duk_error(ctx, DUK_ERR_ERROR, \"Couldn't send error packet.\");\n" \
    "    return r;\n"                                                   \
    "}\n"                                                               \
                                                                        \
    /* Pop functions */                                                 \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_boolean(duk_context *ctx)\n"                       \
    "{\n"                                                               \
    "    bool value;\n"                                                 \
    "    value = duk_require_boolean(ctx, -1);\n"                       \
    "    return sol_flow_packet_new_boolean(value);\n"                  \
    "}\n"                                                               \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_byte(duk_context *ctx)\n"                          \
    "{\n"                                                               \
    "    unsigned char value;\n"                                        \
    "    value = duk_require_int(ctx, -1);\n"                           \
    "    return sol_flow_packet_new_byte(value);\n"                     \
    "}\n"                                                               \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_float(duk_context *ctx)\n"                         \
    "{\n"                                                               \
    "    struct sol_drange value;\n"                                    \
    "    if (duk_is_number(ctx, 1)) {\n"                                \
    "        value.val = duk_require_number(ctx, -1);\n"                \
    "        value.min = -DBL_MAX;\n"                                   \
    "        value.max = DBL_MAX;\n"                                    \
    "        value.step = DBL_MIN;\n"                                   \
    "    } else {\n"                                                    \
    "        duk_require_object_coercible(ctx, -1);\n"                  \
    "        duk_get_prop_string(ctx, -1, \"val\");\n"                  \
    "        duk_get_prop_string(ctx, -2, \"min\");\n"                  \
    "        duk_get_prop_string(ctx, -3, \"max\");\n"                  \
    "        duk_get_prop_string(ctx, -4, \"step\");\n"                 \
    "        value.val = duk_require_number(ctx, -4);\n"                \
    "        value.min = duk_require_number(ctx, -3);\n"                \
    "        value.max = duk_require_number(ctx, -2);\n"                \
    "        value.step = duk_require_number(ctx, -1);\n"               \
    "        duk_pop_n(ctx, 4); /* step, max, min, val values */\n"     \
    "    }\n"                                                           \
    "    return sol_flow_packet_new_drange(&value);\n"                  \
    "}\n"                                                               \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_int(duk_context *ctx)\n"                           \
    "{\n"                                                               \
    "    struct sol_irange value;\n"                                    \
    "    if (duk_is_number(ctx, 1)) {\n"                                \
    "        value.val = duk_require_int(ctx, -1);\n"                   \
    "        value.min = INT32_MIN;\n"                                  \
    "        value.max = INT32_MAX;\n"                                  \
    "        value.step = 1;\n"                                         \
    "    } else {\n"                                                    \
    "        duk_require_object_coercible(ctx, -1);\n"                  \
    "        duk_get_prop_string(ctx, -1, \"val\");\n"                  \
    "        duk_get_prop_string(ctx, -2, \"min\");\n"                  \
    "        duk_get_prop_string(ctx, -3, \"max\");\n"                  \
    "        duk_get_prop_string(ctx, -4, \"step\");\n"                 \
    "        value.val = duk_require_int(ctx, -4);\n"                   \
    "        value.min = duk_require_int(ctx, -3);\n"                   \
    "        value.max = duk_require_int(ctx, -2);\n"                   \
    "        value.step = duk_require_int(ctx, -1);\n"                  \
    "        duk_pop_n(ctx, 4);\n"                                      \
    "    }\n"                                                           \
    "    return sol_flow_packet_new_irange(&value);\n"                  \
    "}\n"                                                               \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_rgb(duk_context *ctx)\n"                           \
    "{\n"                                                               \
    "    struct sol_rgb value;\n"                                       \
    "    duk_require_object_coercible(ctx, -1);\n"                      \
    "    duk_get_prop_string(ctx, -1, \"red\");\n"                      \
    "    duk_get_prop_string(ctx, -2, \"green\");\n"                    \
    "    duk_get_prop_string(ctx, -3, \"blue\");\n"                     \
    "    duk_get_prop_string(ctx, -4, \"red_max\");\n"                  \
    "    duk_get_prop_string(ctx, -5, \"green_max\");\n"                \
    "    duk_get_prop_string(ctx, -6, \"blue_max\");\n"                 \
    "    value.red = duk_require_int(ctx, -6);\n"                       \
    "    value.green = duk_require_int(ctx, -5);\n"                     \
    "    value.blue = duk_require_int(ctx, -4);\n"                      \
    "    value.red_max = duk_require_int(ctx, -3);\n"                   \
    "    value.green_max = duk_require_int(ctx, -2);\n"                 \
    "    value.blue_max = duk_require_int(ctx, -1);\n"                  \
    "    duk_pop_n(ctx, 6);\n"                                          \
    "    return sol_flow_packet_new_rgb(&value);\n"                     \
    "}\n"                                                               \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_string(duk_context *ctx)\n"                        \
    "{\n"                                                               \
    "    const char *value;\n"                                          \
    "    value = duk_require_string(ctx, -1);\n"                        \
    "    return sol_flow_packet_new_string(value);\n"                   \
    "}\n"                                                               \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_timestamp(duk_context *ctx)\n"                     \
    "{\n"                                                               \
    "    struct timespec timestamp;\n"                                  \
    "    duk_require_object_coercible(ctx, -1);\n"                      \
    "    duk_get_prop_string(ctx, -1, \"tv_sec\");\n"                   \
    "    duk_get_prop_string(ctx, -2, \"tv_nsec\");\n"                  \
    "    timestamp.tv_sec = duk_require_number(ctx, -2);\n"             \
    "    timestamp.tv_nsec = duk_require_number(ctx, -1);\n"            \
    "    duk_pop_n(ctx, 2);\n"                                          \
    "    return sol_flow_packet_new_timestamp(&timestamp);\n"           \
    "}\n"                                                               \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_direction_vector(duk_context *ctx)\n"              \
    "{\n"                                                               \
    "    struct sol_direction_vector dir;\n"                            \
    "    duk_require_object_coercible(ctx, -1);\n"                      \
    "    duk_get_prop_string(ctx, -1, \"x\");\n"                        \
    "    duk_get_prop_string(ctx, -2, \"y\");\n"                        \
    "    duk_get_prop_string(ctx, -3, \"z\");\n"                        \
    "    duk_get_prop_string(ctx, -4, \"min\");\n"                      \
    "    duk_get_prop_string(ctx, -5, \"max\");\n"                      \
    "    dir.x = duk_require_number(ctx, -5);\n"                        \
    "    dir.y = duk_require_number(ctx, -4);\n"                        \
    "    dir.z = duk_require_number(ctx, -3);\n"                        \
    "    dir.min = duk_require_number(ctx, -2);\n"                      \
    "    dir.max = duk_require_number(ctx, -1);\n"                      \
    "    duk_pop_n(ctx, 5);\n"                                          \
    "    return sol_flow_packet_new_direction_vector(&dir);\n"          \
    "}\n"                                                               \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_location(duk_context *ctx)\n"                      \
    "{\n"                                                               \
    "    struct sol_location loc;\n"                                    \
    "    duk_require_object_coercible(ctx, -1);\n"                      \
    "    duk_get_prop_string(ctx, -1, \"lat\");\n"                      \
    "    duk_get_prop_string(ctx, -2, \"lon\");\n"                      \
    "    duk_get_prop_string(ctx, -3, \"alt\");\n"                      \
    "    loc.lat = duk_require_number(ctx, -3);\n"                      \
    "    loc.lon = duk_require_number(ctx, -2);\n"                      \
    "    loc.alt = duk_require_number(ctx, -1);\n"                      \
    "    duk_pop_n(ctx, 3);\n"                                          \
    "    return sol_flow_packet_new_location(&loc);\n"                  \
    "}\n"                                                               \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_blob(duk_context *ctx)\n"                          \
    "{\n"                                                               \
    "    void *mem, *cpy;\n"                                            \
    "    size_t size;\n"                                                \
    "    struct sol_blob *blob;\n"                                      \
    "    struct sol_flow_packet *packet;\n"                             \
    "    mem = duk_require_buffer(ctx, -1, &size);\n"                   \
    "    cpy = malloc(size);\n"                                         \
    "    SOL_NULL_CHECK(cpy, NULL);\n"                                  \
    "    memcpy(cpy, mem, size);\n"                                     \
    "    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, cpy, size);\n" \
    "    if (!blob) {\n"                                                \
    "        free(cpy);\n"                                              \
    "        return NULL;\n"                                            \
    "    }\n"                                                           \
    "    packet = sol_flow_packet_new_blob(blob);\n"                    \
    "    sol_blob_unref(blob);\n"                                       \
    "    return packet;\n"                                              \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_array_to_sol_key_value_vector(duk_context *ctx, struct sol_vector *vector,\n" \
    "    const char *prop_name)\n"                                      \
    "{\n"                                                               \
    "    int length, i;\n"                                              \
    "    struct sol_key_value *key_value;\n"                            \
    "    duk_get_prop_string(ctx, -1, prop_name);\n"                    \
    "    duk_require_object_coercible(ctx, -1);\n"                      \
    "    duk_get_prop_string(ctx, -1, \"length\");\n"                   \
    "    length = duk_require_int(ctx, -1);\n"                          \
    "    duk_pop(ctx);\n"                                               \
    "    for (i = 0; i < length; i++) {\n"                              \
    "        duk_get_prop_index(ctx, -1, i);\n"                         \
    "        duk_require_object_coercible(ctx, -1);\n"                  \
    "        duk_get_prop_string(ctx, -1, \"key\");\n"                  \
    "        duk_get_prop_string(ctx, -2, \"value\");\n"                \
    "        key_value = sol_vector_append(vector);\n"                  \
    "        SOL_NULL_CHECK(key_value, -ENOMEM);\n"                     \
    "        key_value->key = duk_require_string(ctx, -2);\n"           \
    "        key_value->value = duk_require_string(ctx, -1);\n"         \
    "        duk_pop_n(ctx, 3);\n"                                      \
    "    }\n"                                                           \
    "    duk_pop(ctx);\n"                                               \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_http_response(duk_context *ctx)\n"                 \
    "{\n"                                                               \
    "    int code;\n"                                                   \
    "    struct sol_blob *content;\n"                                   \
    "    const char *url, *content_type;\n"                             \
    "    struct sol_vector cookies, headers;\n"                         \
    "    void *mem, *cpy;\n"                                            \
    "    size_t size;\n"                                                \
    "    struct sol_flow_packet *packet;\n"                             \
    "    sol_vector_init(&cookies, sizeof(struct sol_key_value));\n"    \
    "    sol_vector_init(&headers, sizeof(struct sol_key_value));\n"    \
    "    duk_require_object_coercible(ctx, -1);\n"                      \
    "    duk_get_prop_string(ctx, -1, \"response_code\");\n"            \
    "    duk_get_prop_string(ctx, -2, \"url\");\n"                      \
    "    duk_get_prop_string(ctx, -3, \"content-type\");\n"             \
    "    duk_get_prop_string(ctx, -4, \"content\");\n"                  \
    "    code = duk_require_int(ctx, -4);\n"                            \
    "    url = duk_require_string(ctx, -3);\n"                          \
    "    content_type = duk_require_string(ctx, -2);\n"                 \
    "    mem = duk_require_buffer(ctx, -1, &size);\n"                   \
    "    duk_pop_n(ctx, 4);\n"                                          \
    "    js_array_to_sol_key_value_vector(ctx, &cookies, \"cookies\");\n" \
    "    js_array_to_sol_key_value_vector(ctx, &headers, \"headers\");\n" \
    "    cpy = malloc(size);\n"                                         \
    "    SOL_NULL_CHECK(cpy, NULL);\n"                                  \
    "    memcpy(cpy, mem, size);\n"                                     \
    "    content = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, cpy, size);\n" \
    "    SOL_NULL_CHECK_GOTO(content, err_exit);\n"                     \
    "    packet = sol_flow_packet_new_http_response(code, url,\n"       \
    "        content_type, content, &cookies, &headers);\n"             \
    "    sol_blob_unref(content);\n"                                    \
    "    sol_vector_clear(&cookies);\n"                                 \
    "    sol_vector_clear(&headers);\n"                                 \
    "    return packet;\n"                                              \
    "err_exit:\n"                                                       \
    "    sol_vector_clear(&cookies);\n"                                 \
    "    sol_vector_clear(&headers);\n"                                 \
    "    free(cpy);\n"                                                  \
    "    return NULL;\n"                                                \
    "}\n"                                                               \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_pop_json(duk_context *ctx,\n"                          \
    "    const struct sol_flow_packet_type *packet_type)\n"             \
    "{\n"                                                               \
    "    const char *value;\n"                                          \
    "    struct sol_blob *blob;\n"                                      \
    "    struct sol_flow_packet *packet;\n"                             \
    "    char *cpy;\n"                                                  \
    "    value = duk_require_string(ctx, -1);\n"                        \
    "    cpy = strdup(value);\n"                                        \
    "    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, cpy, strlen(cpy));\n" \
    "    if (!blob) {\n"                                                \
    "        free(cpy);\n"                                              \
    "        return NULL;\n"                                            \
    "    }\n"                                                           \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_OBJECT)\n"        \
    "        packet = sol_flow_packet_new_json_object(blob);\n"         \
    "    else\n"                                                        \
    "        packet = sol_flow_packet_new_json_array(blob);\n"          \
    "    sol_blob_unref(blob);\n"                                       \
    "    return packet;\n"                                              \
    "}\n"                                                               \
    /* Send packet functions */                                         \
    "static struct sol_flow_packet *\n"                                 \
    "js_metatype_create_packet(const struct sol_flow_packet_type *packet_type, duk_context *ctx)\n" \
    "{\n"                                                               \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_BOOLEAN)\n"            \
    "        return js_metatype_pop_boolean(ctx);\n"                    \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_BYTE)\n"               \
    "        return js_metatype_pop_byte(ctx);\n"                       \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_DRANGE)\n"             \
    "        return js_metatype_pop_float(ctx);\n"                      \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_IRANGE)\n"             \
    "        return js_metatype_pop_int(ctx);\n"                        \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_RGB)\n"                \
    "        return js_metatype_pop_rgb(ctx);\n"                        \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_STRING)\n"             \
    "        return js_metatype_pop_string(ctx);\n"                     \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_BLOB)\n"               \
    "        return js_metatype_pop_blob(ctx);\n"                       \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_LOCATION)\n"           \
    "        return js_metatype_pop_location(ctx);\n"                   \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_TIMESTAMP)\n"          \
    "        return js_metatype_pop_timestamp(ctx);\n"                  \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR)\n"   \
    "        return js_metatype_pop_direction_vector(ctx);\n"           \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_OBJECT ||\n"      \
    "        packet_type == SOL_FLOW_PACKET_TYPE_JSON_ARRAY)\n"         \
    "        return js_metatype_pop_json(ctx, packet_type);\n"          \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE)\n"      \
    "        return js_metatype_pop_http_response(ctx);\n"              \
    "    return NULL;\n"                                                \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_send_composed_packet(struct sol_flow_node *node, uint16_t port,\n" \
    "    duk_context *ctx, const struct sol_flow_packet_type *composed_type)\n" \
    "{\n"                                                               \
    "    int r;\n"                                                      \
    "    uint16_t i, len;\n"                                            \
    "    const struct sol_flow_packet_type **composed_members;\n"       \
    "    struct sol_flow_packet **packets;\n"                           \
    "    r = sol_flow_packet_get_composed_members_packet_types(composed_type,\n" \
    "        &composed_members, &len);\n"                               \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    packets = calloc(len, sizeof(struct sol_flow_packet *));\n"    \
    "    SOL_NULL_CHECK(packets, -ENOMEM);\n"                           \
    "    duk_require_object_coercible(ctx, -1);\n"                      \
    "    r = -ENOMEM;\n"                                                \
    "    for (i = 0; i < len; i++) {\n"                                 \
    "        duk_get_prop_index(ctx, 1, i);\n"                          \
    "        packets[i] = js_metatype_create_packet(composed_members[i], ctx);\n" \
    "        SOL_NULL_CHECK_GOTO(packets[i], exit);\n"                  \
    "        duk_pop(ctx);\n"                                           \
    "    }\n"                                                           \
    "    r = sol_flow_send_composed_packet(node, port, composed_type, packets);\n" \
    "    if (r < 0) {\n"                                                \
    "        duk_error(ctx, DUK_ERR_ERROR, \"Couldn't send packet.\");\n" \
    "    }\n"                                                           \
    "exit:\n"                                                           \
    "    for (i = 0; i < len; i++) {\n"                                 \
    "        if (!packets[i])\n"                                        \
    "            break;\n"                                              \
    "        sol_flow_packet_del(packets[i]);\n"                        \
    "    }\n"                                                           \
    "    free(packets);\n"                                              \
    "    return r;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_send_simple_packet(struct sol_flow_node *node, uint16_t port,\n" \
    "    duk_context *ctx, const struct sol_flow_packet_type *type)\n"  \
    "{\n"                                                               \
    "    struct sol_flow_packet *packet;\n"                             \
    "    int r;\n"                                                      \
    "    packet = js_metatype_create_packet(type, ctx);\n"              \
    "    SOL_NULL_CHECK(packet, -ENOMEM);\n"                            \
    "    r = sol_flow_send_packet(node, port, packet);\n"               \
    "    if (r < 0) {\n"                                                \
    "        duk_error(ctx, DUK_ERR_ERROR, \"Couldn't send packet.\");\n" \
    "    }\n"                                                           \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static duk_ret_t\n"                                                \
    "js_metatype_send_packet(duk_context *ctx)\n"                       \
    "{\n"                                                               \
    "    const struct sol_flow_node_type *type;\n"                      \
    "    const char *port_name;\n"                                      \
    "    struct sol_flow_node *node;\n"                                 \
    "    const struct sol_flow_packet_type *packet_type = NULL;\n"      \
    "    const struct js_metatype_port_out *out_port;\n"                \
    "    uint16_t i;\n"                                                 \
    "    port_name = duk_require_string(ctx, 0);\n"                     \
    "    node = js_metatype_get_node_from_duk_ctx(ctx);\n"              \
    "    if (!node) {\n"                                                \
    "        duk_error(ctx, DUK_ERR_ERROR, \"Couldn't send packet to '%s' port.\", port_name);\n" \
    "        return 0;\n"                                               \
    "    }\n"                                                           \
    "    type = sol_flow_node_get_type(node);\n"                        \
    "    if (!type) {\n"                                                \
    "        duk_error(ctx, DUK_ERR_ERROR, \"Couldn't send packet to '%s' port.\", port_name);\n" \
    "        return 0;\n"                                               \
    "    }\n"                                                           \
    "    for (i = 0; i < type->ports_out_count; i++) {\n"               \
    "        out_port = (const struct js_metatype_port_out *)type->get_port_out(type, i);\n" \
    "        if (!strcmp(port_name, out_port->name)) {\n"               \
    "            packet_type = out_port->base.packet_type;\n"           \
    "            break;\n"                                              \
    "        }\n"                                                       \
    "    }\n"                                                           \
    "    if (!packet_type) {\n"                                         \
    "        duk_error(ctx, DUK_ERR_ERROR, \"'%s' invalid port name.\", port_name);\n" \
    "        return 0;\n"                                               \
    "    }\n"                                                           \
    "    if (sol_flow_packet_is_composed_type(packet_type))\n"          \
    "        return js_metatype_send_composed_packet(node, i, ctx,\n"   \
    "            packet_type);\n"                                       \
    "    return js_metatype_send_simple_packet(node, i, ctx,\n"         \
    "            packet_type);\n"                                       \
    "}\n"                                                               \
    /* Push types to the javascript stack */                            \
    "static int\n"                                                      \
    "js_metatype_push_boolean(const struct sol_flow_packet *packet,\n"  \
    "    duk_context *duk_ctx)\n"                                       \
    "{\n"                                                               \
    "    bool value;\n"                                                 \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_boolean(packet, &value);\n"            \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    duk_push_boolean(duk_ctx, value);\n"                           \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_byte(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    unsigned char value;\n"                                        \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_byte(packet, &value);\n"               \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    duk_push_int(duk_ctx, value);\n"                               \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_error(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    const char *value_msg;\n"                                      \
    "    int r, value_code;\n"                                          \
    "    r = sol_flow_packet_get_error(packet, &value_code, &value_msg);\n" \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    duk_push_int(duk_ctx, value_code);\n"                          \
    "    duk_push_string(duk_ctx, value_msg);\n"                        \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_float(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    struct sol_drange value;\n"                                    \
    "    duk_idx_t obj_idx;\n"                                          \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_drange(packet, &value);\n"             \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    obj_idx = duk_push_object(duk_ctx);\n"                         \
    "    duk_push_number(duk_ctx, value.val);\n"                        \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"val\");\n"             \
    "    duk_push_number(duk_ctx, value.min);\n"                        \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"min\");\n"             \
    "    duk_push_number(duk_ctx, value.max);\n"                        \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"max\");\n"             \
    "    duk_push_number(duk_ctx, value.step);\n"                       \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"step\");\n"            \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_int(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    struct sol_irange value;\n"                                    \
    "    duk_idx_t obj_idx;\n"                                          \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_irange(packet, &value);\n"             \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    obj_idx = duk_push_object(duk_ctx);\n"                         \
    "    duk_push_int(duk_ctx, value.val);\n"                           \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"val\");\n"             \
    "    duk_push_int(duk_ctx, value.min);\n"                           \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"min\");\n"             \
    "    duk_push_int(duk_ctx, value.max);\n"                           \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"max\");\n"             \
    "    duk_push_int(duk_ctx, value.step);\n"                          \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"step\");\n"            \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_rgb(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    struct sol_rgb value;\n"                                       \
    "    duk_idx_t obj_idx;\n"                                          \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_rgb(packet, &value);\n"                \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    obj_idx = duk_push_object(duk_ctx);\n"                         \
    "    duk_push_int(duk_ctx, value.red);\n"                           \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"red\");\n"             \
    "    duk_push_int(duk_ctx, value.green);\n"                         \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"green\");\n"           \
    "    duk_push_int(duk_ctx, value.blue);\n"                          \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"blue\");\n"            \
    "    duk_push_int(duk_ctx, value.red_max);\n"                       \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"red_max\");\n"         \
    "    duk_push_int(duk_ctx, value.green_max);\n"                     \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"green_max\");\n"       \
    "    duk_push_int(duk_ctx, value.blue_max);\n"                      \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"blue_max\");\n"        \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_string(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    const char *value;\n"                                          \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_string(packet, &value);\n"             \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    duk_push_string(duk_ctx, value);\n"                            \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_timestamp(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    struct timespec timestamp;\n"                                  \
    "    duk_idx_t obj_idx;\n"                                          \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_timestamp(packet, &timestamp);\n"      \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    obj_idx = duk_push_object(duk_ctx);\n"                         \
    "    duk_push_number(duk_ctx, timestamp.tv_sec);\n"                 \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"tv_sec\");\n"          \
    "    duk_push_number(duk_ctx, timestamp.tv_nsec);\n"                \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"tv_nsec\");\n"         \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_direction_vector(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    struct sol_direction_vector dir;\n"                            \
    "    duk_idx_t obj_idx;\n"                                          \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_direction_vector(packet, &dir);\n"     \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    obj_idx = duk_push_object(duk_ctx);\n"                         \
    "    duk_push_number(duk_ctx, dir.x);\n"                            \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"x\");\n"               \
    "    duk_push_number(duk_ctx, dir.y);\n"                            \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"y\");\n"               \
    "    duk_push_number(duk_ctx, dir.z);\n"                            \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"z\");\n"               \
    "    duk_push_number(duk_ctx, dir.min);\n"                          \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"min\");\n"             \
    "    duk_push_number(duk_ctx, dir.max);\n"                          \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"max\");\n"             \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static void\n"                                                     \
    "push_blob(const struct sol_blob *blob, duk_context *duk_ctx)\n"    \
    "{\n"                                                               \
    "    void *mem;\n"                                                  \
    "    mem = duk_push_fixed_buffer(duk_ctx, blob->size);\n"           \
    "    memcpy(mem, blob->mem, blob->size);\n"                         \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_blob(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    struct sol_blob *blob;\n"                                      \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_blob(packet, &blob);\n"                \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    push_blob(blob, duk_ctx);\n"                                   \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_location(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    struct sol_location loc;\n"                                    \
    "    duk_idx_t obj_idx;\n"                                          \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_location(packet, &loc);\n"             \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    obj_idx = duk_push_object(duk_ctx);\n"                         \
    "    duk_push_number(duk_ctx, loc.lat);\n"                          \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"lat\");\n"             \
    "    duk_push_number(duk_ctx, loc.lon);\n"                          \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"lon\");\n"             \
    "    duk_push_number(duk_ctx, loc.alt);\n"                          \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"alt\");\n"             \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_json_array(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    struct sol_blob *blob;\n"                                      \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_json_array(packet, &blob);\n"          \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    duk_push_lstring(duk_ctx, (const char *)blob->mem, blob->size);\n" \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_json_object(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    struct sol_blob *blob;\n"                                      \
    "    int r;\n"                                                      \
    "    r = sol_flow_packet_get_json_object(packet, &blob);\n"         \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    duk_push_lstring(duk_ctx, (const char *)blob->mem, blob->size);\n" \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static void\n"                                                     \
    "js_metatype_add_sol_key_valueto_js_array(const struct sol_vector *vector,\n" \
    "    duk_context *duk_ctx, duk_idx_t request_idx, const char *prop_name)\n" \
    "{\n"                                                               \
    "    uint16_t i;\n"                                                 \
    "    duk_idx_t obj_idx, array_idx;\n"                               \
    "    struct sol_key_value *key_value;\n"                            \
    "    array_idx = duk_push_array(duk_ctx);\n"                        \
    "    SOL_VECTOR_FOREACH_IDX (vector, key_value, i) {\n"             \
    "        obj_idx = duk_push_object(duk_ctx);\n"                     \
    "        duk_push_string(duk_ctx, key_value->key);\n"               \
    "        duk_put_prop_string(duk_ctx, obj_idx, \"key\");\n"         \
    "        duk_push_string(duk_ctx, key_value->value);\n"             \
    "        duk_put_prop_string(duk_ctx, obj_idx, \"value\");\n"       \
    "        duk_put_prop_index(duk_ctx, array_idx, i);\n"              \
    "    }\n"                                                           \
    "    duk_put_prop_string(duk_ctx, request_idx, prop_name);\n"       \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_push_http_response(const struct sol_flow_packet *packet, duk_context *duk_ctx)\n" \
    "{\n"                                                               \
    "    const char *url, *content_type;\n"                             \
    "    const struct sol_blob *content;\n"                             \
    "    struct sol_vector cookies, headers;\n"                         \
    "    duk_idx_t obj_idx;\n"                                          \
    "    int r, code;\n"                                                \
    "    sol_vector_init(&cookies, sizeof(struct sol_key_value));\n"    \
    "    sol_vector_init(&headers, sizeof(struct sol_key_value));\n"    \
    "    r = sol_flow_packet_get_http_response(packet, &code, &url, &content_type,\n" \
    "        &content, &cookies, &headers);\n"                          \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    obj_idx = duk_push_object(duk_ctx);\n"                         \
    "    duk_push_number(duk_ctx, code);\n"                             \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"response_code\");\n"   \
    "    duk_push_string(duk_ctx, url);\n"                              \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"url\");\n"             \
    "    duk_push_string(duk_ctx, content_type);\n"                     \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"content-type\");\n"    \
    "    push_blob(content, duk_ctx);\n"                                \
    "    duk_put_prop_string(duk_ctx, obj_idx, \"content\");\n"         \
    "    js_metatype_add_sol_key_valueto_js_array(&cookies, duk_ctx, obj_idx, \"cookies\");\n" \
    "    js_metatype_add_sol_key_valueto_js_array(&headers, duk_ctx, obj_idx, \"headers\");\n" \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    /* Handle packets by type */                                        \
    "static int\n"                                                      \
    "js_metatype_process_simple_packet(const struct sol_flow_packet *packet,\n" \
    "    duk_context *duk_ctx)\n"                                       \
    "{\n"                                                               \
    "    const struct sol_flow_packet_type *packet_type =\n"            \
    "        sol_flow_packet_get_type(packet);\n"                       \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_BOOLEAN)\n"            \
    "        return js_metatype_push_boolean(packet, duk_ctx);\n"       \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_BYTE)\n"               \
    "        return js_metatype_push_byte(packet, duk_ctx);\n"          \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_ERROR)\n"              \
    "        return js_metatype_push_error(packet, duk_ctx);\n"         \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_DRANGE)\n"             \
    "        return js_metatype_push_float(packet, duk_ctx);\n"         \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_IRANGE)\n"             \
    "        return js_metatype_push_int(packet, duk_ctx);\n"           \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_RGB)\n"                \
    "        return js_metatype_push_rgb(packet, duk_ctx);\n"           \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_STRING)\n"             \
    "        return js_metatype_push_string(packet, duk_ctx);\n"        \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_BLOB)\n"               \
    "        return js_metatype_push_blob(packet, duk_ctx);\n"          \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_LOCATION)\n"           \
    "        return js_metatype_push_location(packet, duk_ctx);\n"      \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_TIMESTAMP)\n"          \
    "        return js_metatype_push_timestamp(packet, duk_ctx);\n"     \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR)\n"   \
    "        return js_metatype_push_direction_vector(packet, duk_ctx);\n" \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_OBJECT)\n"        \
    "        return js_metatype_push_json_object(packet, duk_ctx);\n"   \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_ARRAY)\n"         \
    "        return js_metatype_push_json_array(packet, duk_ctx);\n"    \
    "    if (packet_type == SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE)\n"      \
    "        return js_metatype_push_http_response(packet, duk_ctx);\n" \
    "    return -EINVAL;\n"                                             \
    "}\n"                                                               \
    /* Fetch javascript process functions and call them */              \
    "static int\n"                                                      \
    "js_metatype_process_boilerplate_pre(duk_context *ctx, struct sol_flow_node *node, uint16_t port)\n" \
    "{\n"                                                               \
    "    duk_push_global_stash(ctx);\n"                                 \
    "    if (!duk_get_prop_index(ctx, -1, port * PORTS_IN_METHODS_LENGTH + PORTS_IN_PROCESS_INDEX)) {\n" \
    "        duk_pop_2(ctx);\n"                                         \
    "        return -1;\n"                                              \
    "    }\n"                                                           \
    "    if (duk_is_null_or_undefined(ctx, -1)) {\n"                    \
    "        duk_pop_2(ctx);\n"                                         \
    "        return 0;\n"                                               \
    "    }\n"                                                           \
    "    duk_dup(ctx, -3);\n"                                           \
    "    return 1;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_process_boilerplate_post(duk_context *ctx, struct sol_flow_node *node, uint16_t port, uint16_t js_method_nargs)\n" \
    "{\n"                                                               \
    "    if (duk_pcall_method(ctx, js_method_nargs) != DUK_EXEC_SUCCESS) {\n" \
    "        duk_pop_2(ctx);\n"                                         \
    "        return -1;\n"                                              \
    "    }\n"                                                           \
    "    duk_pop_2(ctx);\n"                                             \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    /* Port process functions. */                                       \
    "static int\n"                                                      \
    "js_metatype_simple_port_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,\n" \
    "    const struct sol_flow_packet *packet)\n"                       \
    "{\n"                                                               \
    "    duk_context **duk_ctx = data;\n"                               \
    "    int r;\n"                                                      \
    "    r = js_metatype_process_boilerplate_pre(*duk_ctx, node, port);\n" \
    "    SOL_INT_CHECK(r, <= 0, r);\n"                                  \
    "    r = js_metatype_process_simple_packet(packet, *duk_ctx);\n"    \
    "    SOL_INT_CHECK_GOTO(r, < 0, err_exit);\n"                       \
    "    return js_metatype_process_boilerplate_post(*duk_ctx, node, port, 1);\n" \
    "err_exit:\n"                                                       \
    "    duk_pop_n(*duk_ctx, 3);\n"                                     \
    "    return r;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_composed_port_process(struct sol_flow_node *node, void *data,\n" \
    "    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)\n" \
    "{\n"                                                               \
    "    duk_context **duk_ctx = data;\n"                               \
    "    int r;\n"                                                      \
    "    uint16_t i, len;\n"                                            \
    "    struct sol_flow_packet **children;\n"                          \
    "    duk_idx_t array_idx;\n"                                        \
    "    r = sol_flow_packet_get_composed_members(packet, &children, &len);\n" \
    "    SOL_INT_CHECK(r, < 0, r);\n"                                   \
    "    r = js_metatype_process_boilerplate_pre(*duk_ctx, node, port);\n" \
    "    SOL_INT_CHECK(r, <= 0, r);\n"                                  \
    "    array_idx = duk_push_array(*duk_ctx);\n"                       \
    "    for (i = 0; i < len; i++) {\n"                                 \
    "        r = js_metatype_process_simple_packet(children[i], *duk_ctx);\n" \
    "        SOL_INT_CHECK_GOTO(r, < 0, err_exit);\n"                   \
    "        duk_put_prop_index(*duk_ctx, array_idx, i);\n"             \
    "    }\n"                                                           \
    "    return js_metatype_process_boilerplate_post(*duk_ctx, node, port, 1);\n" \
    "err_exit:\n"                                                       \
    "    duk_pop_n(*duk_ctx, 4);\n"                                     \
    "    return r;\n"                                                   \
    "}\n"                                                               \
    /* Functions that handle connect/disconnect methods */              \
    "static int\n"                                                      \
    "js_metatype_handle_js_port_activity(void *data, uint16_t port, uint16_t conn_id,\n" \
    "    uint16_t base, uint16_t methods_length, uint16_t method_index)\n" \
    "{\n"                                                               \
    "    duk_context **duk_ctx = data;\n"                               \
    "    duk_push_global_stash(*duk_ctx);\n"                            \
    "    if (!duk_get_prop_index(*duk_ctx, -1, base + port * methods_length + method_index)) {\n" \
    "        duk_pop_2(*duk_ctx);\n"                                    \
    "        return -1;\n"                                              \
    "    }\n"                                                           \
    "    if (duk_is_null_or_undefined(*duk_ctx, -1)) {\n"               \
    "        duk_pop_2(*duk_ctx);\n"                                    \
    "        return 0;\n"                                               \
    "    }\n"                                                           \
    "    if (duk_pcall(*duk_ctx, 0) != DUK_EXEC_SUCCESS) {\n"           \
    "        duk_pop_2(*duk_ctx);\n"                                    \
    "        return -1;\n"                                              \
    "    }\n"                                                           \
    "    duk_pop_2(*duk_ctx);\n"                                        \
    "    return 0;\n"                                                   \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_port_in_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)\n" \
    "{\n"                                                               \
    "    return js_metatype_handle_js_port_activity(data, port, conn_id, 0, PORTS_IN_METHODS_LENGTH, PORTS_IN_CONNECT_INDEX);\n" \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_port_in_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)\n" \
    "{\n"                                                               \
    "    return js_metatype_handle_js_port_activity(data, port, conn_id, 0, PORTS_IN_METHODS_LENGTH, PORTS_IN_DISCONNECT_INDEX);\n" \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_port_out_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)\n" \
    "{\n"                                                               \
    "    const struct sol_flow_node_type *type = sol_flow_node_get_type(node);\n" \
    "    return js_metatype_handle_js_port_activity(data, port, conn_id,\n" \
    "        type->ports_in_count * PORTS_IN_METHODS_LENGTH, PORTS_OUT_METHODS_LENGTH, PORTS_OUT_CONNECT_INDEX);\n" \
    "}\n"                                                               \
    "static int\n"                                                      \
    "js_metatype_port_out_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)\n" \
    "{\n"                                                               \
    "    const struct sol_flow_node_type *type = sol_flow_node_get_type(node);\n" \
    "    return js_metatype_handle_js_port_activity(data, port, conn_id,\n" \
    "        type->ports_in_count * PORTS_IN_METHODS_LENGTH, PORTS_OUT_METHODS_LENGTH, PORTS_OUT_DISCONNECT_INDEX);\n" \
    "}\n"                                                               \
    /* Node close method */                                             \
    "static void\n"                                                     \
    "js_metatype_close(struct sol_flow_node *node, void *data)\n"       \
    "{\n"                                                               \
    "    duk_context **duk_ctx = data;\n"                               \
    "    if (duk_has_prop_string(*duk_ctx, -1, \"close\")) {\n"         \
    "        duk_push_string(*duk_ctx, \"close\");\n"                   \
    "        if (duk_pcall_prop(*duk_ctx, -2, 0) != DUK_EXEC_SUCCESS) {\n" \
    "            duk_error(*duk_ctx, DUK_ERR_ERROR, \"Javascript close() function error: %s\",\n" \
    "                duk_safe_to_string(*duk_ctx, -1));\n"              \
    "        }\n"                                                       \
    "        duk_pop(*duk_ctx);\n"                                      \
    "    }\n"                                                           \
    "    duk_destroy_heap(*duk_ctx);\n"                                 \
    "}\n"                                                               \
    /* Setup port methos like: connect, process and disconnect */       \
    "static bool\n"                                                     \
    "js_metatype_fetch_ports_methods(duk_context *duk_ctx, const char *prop,\n" \
    "    uint16_t ports_len, uint16_t base, uint16_t methods_len, uint16_t *methods_index)\n" \
    "{\n"                                                               \
    "    uint16_t i;\n"                                                 \
    "    if (ports_len == 0)\n"                                         \
    "        return true;\n"                                            \
    "    duk_get_prop_string(duk_ctx, -1, prop);\n"                     \
    "    if (!duk_is_array(duk_ctx, -1)) {\n"                           \
    "        SOL_ERR(\"'%s' property of object 'node' should be an array.\", prop);\n" \
    "        return false;\n"                                           \
    "    }\n"                                                           \
    "    duk_push_global_stash(duk_ctx);\n"                             \
    "    for (i = 0; i < ports_len; i++) {\n"                           \
    "        if (!duk_get_prop_index(duk_ctx, -2, i)) {\n"              \
    "            SOL_ERR(\"Couldn't get input port information from 'ports.%s[%d]'.\", prop, i);\n" \
    "            return false;\n"                                       \
    "        }\n"                                                       \
    "        duk_get_prop_string(duk_ctx, -1, \"connect\");\n"          \
    "        duk_put_prop_index(duk_ctx, -3, base + i * methods_len + methods_index[0]);\n" \
    "        duk_get_prop_string(duk_ctx, -1, \"disconnect\");\n"       \
    "        duk_put_prop_index(duk_ctx, -3, base + i * methods_len + methods_index[1]);\n" \
    "        if (methods_len >= 3) {\n"                                 \
    "            duk_get_prop_string(duk_ctx, -1, \"process\");\n"      \
    "            duk_put_prop_index(duk_ctx, -3, base + i * methods_len + methods_index[2]);\n" \
    "        }\n"                                                       \
    "        duk_pop(duk_ctx);\n"                                       \
    "    }\n"                                                           \
    "    duk_pop_2(duk_ctx);\n"                                         \
    "    return true;\n"                                                \
    "}\n"                                                               \
    "static bool\n"                                                     \
    "js_metatype_setup_ports_methods(duk_context *duk_ctx, uint16_t ports_in_len, uint16_t ports_out_len)\n" \
    "{\n"                                                               \
    "    uint16_t methods_in_index[] = { PORTS_IN_CONNECT_INDEX,\n"     \
    "        PORTS_IN_DISCONNECT_INDEX, PORTS_IN_PROCESS_INDEX };\n"    \
    "    uint16_t methods_out_index[] = { PORTS_OUT_CONNECT_INDEX, PORTS_OUT_DISCONNECT_INDEX };\n" \
    "    if (!js_metatype_fetch_ports_methods(duk_ctx, \"in\", ports_in_len, 0,\n" \
    "        PORTS_IN_METHODS_LENGTH, methods_in_index))\n"             \
    "        return false;\n"                                           \
    "    if (!js_metatype_fetch_ports_methods(duk_ctx, \"out\", ports_out_len,\n" \
    "        ports_in_len * PORTS_IN_METHODS_LENGTH,\n"                 \
    "        PORTS_OUT_METHODS_LENGTH, methods_out_index))\n"           \
    "        return false;\n"                                           \
    "    return true;\n"                                                \
    "}\n"                                                               \
    /* Common open function */                                          \
    "static int\n"                                                      \
    "js_metatype_common_open(struct sol_flow_node *node, duk_context **duk_ctx, const char *code, size_t code_size)\n" \
    "{\n"                                                               \
    "    const struct sol_flow_node_type *type = sol_flow_node_get_type(node);\n" \
    "    *duk_ctx = duk_create_heap_default();\n"                       \
    "    if (!*duk_ctx) {\n"                                            \
    "        SOL_ERR(\"Failed to create a Duktape heap\");\n"           \
    "        return -1;\n"                                              \
    "    }\n"                                                           \
    "    if (duk_peval_lstring(*duk_ctx, code, code_size) != 0) {\n"    \
    "        SOL_ERR(\"Failed to read from javascript content buffer: %s\", duk_safe_to_string(duk_ctx, -1));\n" \
    "        duk_destroy_heap(*duk_ctx);\n"                             \
    "        return -1;\n"                                              \
    "    }\n"                                                           \
    "    duk_pop(*duk_ctx);\n"                                          \
    "    duk_push_global_object(*duk_ctx);\n"                           \
    "    duk_push_string(*duk_ctx, \"\\xFF\" \"Soletta_node_pointer\");\n" \
    "    duk_push_pointer(*duk_ctx, node);\n"                           \
    "    duk_def_prop(*duk_ctx, -3,\n"                                  \
    "        DUK_DEFPROP_HAVE_VALUE |\n"                                \
    "        DUK_DEFPROP_HAVE_WRITABLE |\n"                             \
    "        DUK_DEFPROP_HAVE_ENUMERABLE |\n"                           \
    "        DUK_DEFPROP_HAVE_CONFIGURABLE);\n"                         \
    "    duk_push_c_function(*duk_ctx, js_metatype_send_packet, 2);\n"  \
    "    duk_put_prop_string(*duk_ctx, -2, \"sendPacket\");\n"          \
    "    duk_push_c_function(*duk_ctx, js_metatype_send_error_packet, 2);\n" \
    "    duk_put_prop_string(*duk_ctx, -2, \"sendErrorPacket\");\n"     \
    "    duk_get_prop_string(*duk_ctx, -1, \"node\");\n"                \
    "    if (!js_metatype_setup_ports_methods(*duk_ctx, type->ports_in_count, type->ports_out_count)) {\n" \
    "        SOL_ERR(\"Failed to handle ports methods: %s\", duk_safe_to_string(*duk_ctx, -1));\n" \
    "        duk_destroy_heap(*duk_ctx);\n"                             \
    "        return -1;\n"                                              \
    "    }\n"                                                           \
    "    if (!duk_has_prop_string(*duk_ctx, -1, \"open\"))\n"           \
    "        return 0;\n"                                               \
    "    duk_push_string(*duk_ctx, \"open\");\n"                        \
    "    if (duk_pcall_prop(*duk_ctx, -2, 0) != DUK_EXEC_SUCCESS) {\n"  \
    "        duk_error(*duk_ctx, DUK_ERR_ERROR, \"Javascript open() function error: %s\",\n" \
    "            duk_safe_to_string(*duk_ctx, -1));\n"                  \
    "    }\n"                                                           \
    "    duk_pop(*duk_ctx);\n"                                          \
    "    return 0;\n"                                                   \
    "}\n"
