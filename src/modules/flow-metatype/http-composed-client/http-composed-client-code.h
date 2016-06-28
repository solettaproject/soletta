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

#define HTTP_COMPOSED_CLIENT_CODE_START \
    "#include \"sol-flow-packet.h\"\n" \
    "#include \"sol-http.h\"\n" \
    "#include \"sol-http-client.h\"\n" \
    "#include \"sol-json.h\"\n" \
    "#include \"sol-log.h\"\n" \
    "#include \"sol-util.h\"\n" \
    "#include \"sol-macros.h\"\n\n" \
    "struct http_composed_client_port_in {\n" \
    "    struct sol_flow_port_type_in base;\n" \
    "    const char *name;\n" \
    "};\n\n" \
    "struct http_composed_client_data {\n" \
    "    uint16_t inputs_len;\n" \
    "    struct sol_ptr_vector pending_conns;\n" \
    "    const struct sol_flow_packet_type *composed_type;\n" \
    "    char *url;\n" \
    "    struct sol_flow_packet **inputs;\n" \
    "};\n" \
    "struct http_composed_client_port_out {\n" \
    "    struct sol_flow_port_type_out base;\n" \
    "    const char *name;\n" \
    "};\n\n" \
    "struct http_composed_client_options {\n" \
    "    struct sol_flow_node_options base;\n" \
    "#define SOL_FLOW_NODE_TYPE_HTTP_COMPOSED_CLIENT_OPTIONS_API_VERSION (1)\n" \
    "    const char *url; /**< The url used on requests (optional) */\n" \
    "};\n" \
    "static int http_composed_client_open(struct sol_flow_node *node, void *data,\n" \
    "    const struct sol_flow_node_options *options)\n {\n" \
    "    struct http_composed_client_data *cdata = data;\n" \
    "    const struct http_composed_client_options *opts;\n" \
    "    const struct sol_flow_node_type *self;\n" \
    "    const struct http_composed_client_port_out *out;\n" \
    "    opts = (struct http_composed_client_options *)options;\n" \
    "    if (opts->url) {\n" \
    "        cdata->url = strdup(opts->url);\n" \
    "        SOL_NULL_CHECK(cdata->url, -ENOMEM);\n" \
    "    }\n" \
    "    self = sol_flow_node_get_type(node);\n" \
    "    sol_ptr_vector_init(&cdata->pending_conns);\n" \
    "    cdata->inputs_len = self->ports_in_count - 4;\n" \
    "    cdata->inputs = calloc(cdata->inputs_len, sizeof(struct sol_flow_packet *));\n" \
    "    SOL_NULL_CHECK_GOTO(cdata->inputs, err);\n" \
    "    out = (struct http_composed_client_port_out *)self->get_port_out(self, 0);\n" \
    "    cdata->composed_type = out->base.packet_type;\n" \
    "    return 0;\n" \
    "err:\n" \
    "    free(cdata->url);\n" \
    "    return -ENOMEM;\n" \
    "}\n" \
    "static void\n" \
    "http_composed_client_close(struct sol_flow_node *node, void *data)\n" \
    "{\n" \
    "    struct sol_http_client_connection *connection;\n" \
    "    struct http_composed_client_data *cdata = data;\n" \
    "    uint16_t i;\n" \
    "    for (i = 0; i < cdata->inputs_len; i++)\n" \
    "        sol_flow_packet_del(cdata->inputs[i]);\n" \
    "    SOL_PTR_VECTOR_FOREACH_IDX (&cdata->pending_conns, connection, i)\n" \
    "        sol_http_client_connection_cancel(connection);\n" \
    "    sol_ptr_vector_clear(&cdata->pending_conns);\n" \
    "    free(cdata->url);\n" \
    "    free(cdata->inputs);\n" \
    "}\n" \
    "static struct sol_flow_packet *\n" \
    "http_composed_client_create_packet_number(const struct sol_flow_packet_type *type,\n" \
    "    const struct sol_json_token *token)\n" \
    "{\n" \
    "    int r;\n" \
    "    if (type == SOL_FLOW_PACKET_TYPE_IRANGE) {\n" \
    "        int32_t value;\n" \
    "        r = sol_json_token_get_int32(token, &value);\n" \
    "        SOL_INT_CHECK(r, < 0, NULL);\n" \
    "        return sol_flow_packet_new_irange_value(value);\n" \
    "    } else if (type == SOL_FLOW_PACKET_TYPE_DRANGE) {\n" \
    "        double value;\n" \
    "        r = sol_json_token_get_double(token, &value);\n" \
    "        SOL_INT_CHECK(r, < 0, NULL);\n" \
    "        return sol_flow_packet_new_drange_value(value);\n" \
    "    } else if (type == SOL_FLOW_PACKET_TYPE_BYTE) {\n" \
    "        int32_t value;\n" \
    "        r = sol_json_token_get_int32(token, &value);\n" \
    "        SOL_INT_CHECK(r, < 0, NULL);\n" \
    "        if (value < 0)\n" \
    "            value = 0;\n" \
    "        else if (value > UINT8_MAX)\n" \
    "            value = UINT8_MAX;\n" \
    "        return sol_flow_packet_new_byte((uint8_t)value);\n" \
    "    }\n" \
    "    return NULL;\n" \
    "}\n" \
    "static struct sol_flow_packet *\n" \
    "http_composed_client_create_packet(const struct sol_flow_packet_type *type,\n" \
    "    const struct sol_json_token *token)\n" \
    "{\n" \
    "    enum sol_json_type json_type;\n" \
    "    json_type = sol_json_token_get_type(token);\n" \
    "    if (json_type == SOL_JSON_TYPE_FALSE) {\n" \
    "        return sol_flow_packet_new_bool(false);\n" \
    "    } else if (json_type == SOL_JSON_TYPE_TRUE) {\n" \
    "        return sol_flow_packet_new_bool(true);\n" \
    "    } else if (json_type == SOL_JSON_TYPE_NUMBER) {\n" \
    "        return http_composed_client_create_packet_number(type, token);\n" \
    "    } else if (json_type == SOL_JSON_TYPE_STRING) {\n" \
    "        int r;\n" \
    "        struct sol_buffer buffer;\n" \
    "        r = sol_json_token_get_unescaped_string(token, &buffer);\n" \
    "        SOL_INT_CHECK(r, < 0, NULL);\n" \
    "        return sol_flow_packet_new_string_slice(sol_buffer_get_slice(&buffer));\n" \
    "    }\n" \
    "    return NULL;\n" \
    "}\n" \
    "static void\n" \
    "http_composed_client_request_finished(void *data,\n" \
    "    struct sol_http_client_connection *connection,\n" \
    "    struct sol_http_response *response)\n" \
    "{\n" \
    "    int r = 0;\n" \
    "    uint16_t i = 0;\n" \
    "    struct sol_flow_node *node = data;\n" \
    "    const struct sol_flow_node_type *ntype = sol_flow_node_get_type(node);\n" \
    "    struct http_composed_client_data *cdata = sol_flow_node_get_private_data(node);\n" \
    "    if (sol_ptr_vector_remove(&cdata->pending_conns, connection) < 0)\n" \
    "        SOL_WRN(\"Failed to find pending connection %p\", connection);\n" \
    "    if (!response) {\n" \
    "        sol_flow_send_error_packet(node, EINVAL,\n" \
    "            \"Error while reaching %s\", cdata->url);\n" \
    "        return;\n" \
    "    }\n" \
    "    SOL_HTTP_RESPONSE_CHECK_API(response);\n" \
    "    if (response->response_code != SOL_HTTP_STATUS_OK) {\n" \
    "        sol_flow_send_error_packet(node, EINVAL,\n" \
    "            \"%s returned an unhandled response code: %d\",\n" \
    "            cdata->url, response->response_code);\n" \
    "        return;\n" \
    "    }\n" \
    "    if (!strcmp(response->content_type, \"application/json\")) {\n" \
    "        struct sol_json_scanner scanner;\n" \
    "        struct sol_json_token token;\n" \
    "        enum sol_json_loop_status reason;\n" \
    "        sol_json_scanner_init(&scanner, response->content.data, response->content.used);\n" \
    "        SOL_JSON_SCANNER_ARRAY_LOOP(&scanner, &token, reason) {\n" \
    "            struct http_composed_client_port_in *in =\n" \
    "                (struct http_composed_client_port_in *)ntype->get_port_in(ntype, i);\n" \
    "            SOL_NULL_CHECK_GOTO(in, err);\n" \
    "            sol_flow_packet_del(cdata->inputs[i]);\n" \
    "            cdata->inputs[i] = http_composed_client_create_packet(in->base.packet_type, &token);\n" \
    "            SOL_NULL_CHECK_GOTO(cdata->inputs[i], err);\n" \
    "            i++;\n" \
    "        }\n" \
    "        SOL_INT_CHECK_GOTO(i, != cdata->inputs_len, err);\n" \
    "        sol_flow_send_composed_packet(node, 0, cdata->composed_type,\n" \
    "            cdata->inputs);\n" \
    "    }\n" \
    "    return;\n" \
    "err:\n" \
    "    sol_flow_send_error_packet(node, r,\n" \
    "        \"%s Could not parse url contents \", cdata->url);\n" \
    "}\n" \
    "static int\n" \
    "http_composed_client_get_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,\n" \
    "    const struct sol_flow_packet *packet)\n" \
    "{\n" \
    "    int r;\n" \
    "    struct sol_http_params params;\n" \
    "    struct sol_http_client_connection *connection;\n" \
    "    struct http_composed_client_data *cdata = data;\n" \
    "    if (!cdata->url)\n" \
    "        return -EINVAL;\n" \
    "    sol_http_params_init(&params);\n" \
    "    if (sol_http_params_add(&params,\n" \
    "        SOL_HTTP_REQUEST_PARAM_HEADER(\"Accept\", \"application/json\")) < 0) {\n" \
    "        SOL_WRN(\"Failed to set query params\");\n" \
    "        sol_http_params_clear(&params);\n" \
    "        return -ENOMEM;\n" \
    "    }\n" \
    "    connection = sol_http_client_request(SOL_HTTP_METHOD_GET, cdata->url,\n" \
    "        &params, http_composed_client_request_finished, node);\n" \
    "    sol_http_params_clear(&params);\n" \
    "    SOL_NULL_CHECK(connection, -ENOTCONN);\n" \
    "    r = sol_ptr_vector_append(&cdata->pending_conns, connection);\n" \
    "    if (r < 0) {\n" \
    "        SOL_WRN(\"Failed to keep pending connection.\");\n" \
    "        sol_http_client_connection_cancel(connection);\n" \
    "        return r;\n" \
    "    }\n" \
    "    return 0;\n" \
    "}\n" \
    "static bool\n" \
    "http_composed_client_data_check(struct http_composed_client_data *cdata)\n" \
    "{\n" \
    "    uint16_t i;\n" \
    "    if (!cdata->url)\n" \
    "        return false;\n" \
    "    for (i = 0; i < cdata->inputs_len; i++) {\n" \
    "        if (!cdata->inputs[i])\n" \
    "            break;\n" \
    "    }\n" \
    "    if (i != cdata->inputs_len)\n" \
    "        return false;\n" \
    "    return true;\n" \
    "}\n" \
    "static int\n" \
    "http_composed_client_post_process(struct sol_flow_node *node, void *data, uint16_t port,\n" \
    "    uint16_t conn_id, const struct sol_flow_packet *packet)\n" \
    "{\n" \
    "    int r = 0;\n" \
    "    uint16_t i;\n" \
    "    struct sol_http_params params;\n" \
    "    struct http_composed_client_data *cdata = data;\n" \
    "    struct sol_http_client_connection *connection;\n" \
    "    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;\n" \
    "    if (!http_composed_client_data_check(cdata))\n" \
    "        return -EINVAL;\n" \
    "    sol_http_params_init(&params);\n" \
    "    r = sol_buffer_append_char(&buffer, '[');\n" \
    "    SOL_INT_CHECK_GOTO(r, > 0, end);\n" \
    "    for (i = 0; i < cdata->inputs_len; i++) {\n" \
    "        SOL_INT_CHECK_GOTO(r, > 0, end);\n" \
    "        if (sol_flow_packet_get_type(cdata->inputs[i]) == SOL_FLOW_PACKET_TYPE_IRANGE) {\n" \
    "            struct sol_irange value;\n" \
    "            r = sol_flow_packet_get_irange(cdata->inputs[i], &value);\n" \
    "            SOL_INT_CHECK_GOTO(r, < 0, end);\n" \
    "            r = sol_json_serialize_int32(&buffer, value.val);\n" \
    "            SOL_INT_CHECK_GOTO(r, < 0, end);\n" \
    "        } else if (sol_flow_packet_get_type(cdata->inputs[i]) == SOL_FLOW_PACKET_TYPE_BYTE) {\n" \
    "            uint8_t value;\n" \
    "            r = sol_flow_packet_get_byte(cdata->inputs[i], &value);\n" \
    "            SOL_INT_CHECK_GOTO(r, < 0, end);\n" \
    "            r = sol_json_serialize_int32(&buffer, value);\n" \
    "            SOL_INT_CHECK_GOTO(r, < 0, end);\n" \
    "        } else if (sol_flow_packet_get_type(cdata->inputs[i]) == SOL_FLOW_PACKET_TYPE_DRANGE) {\n" \
    "            struct sol_drange value;\n" \
    "            r = sol_flow_packet_get_drange(cdata->inputs[i], &value);\n" \
    "            SOL_INT_CHECK_GOTO(r, < 0, end);\n" \
    "            r = sol_json_serialize_double(&buffer, value.val);\n" \
    "            SOL_INT_CHECK_GOTO(r, < 0, end);\n" \
    "        } else if (sol_flow_packet_get_type(cdata->inputs[i]) == SOL_FLOW_PACKET_TYPE_BOOL) {\n" \
    "            bool value;\n" \
    "            r = sol_flow_packet_get_bool(cdata->inputs[i], &value);\n" \
    "            SOL_INT_CHECK_GOTO(r, < 0, end);\n" \
    "            r = sol_json_serialize_bool(&buffer, value);\n" \
    "            SOL_INT_CHECK_GOTO(r, < 0, end);\n" \
    "        } else if (sol_flow_packet_get_type(cdata->inputs[i]) == SOL_FLOW_PACKET_TYPE_STRING) {\n" \
    "            const char *value;\n" \
    "            r = sol_flow_packet_get_string(cdata->inputs[i], &value);\n" \
    "            SOL_INT_CHECK_GOTO(r, < 0, end);\n" \
    "            r = sol_json_serialize_string(&buffer, value);\n" \
    "            SOL_INT_CHECK_GOTO(r, < 0, end);\n" \
    "        }\n" \
    "        if (i == cdata->inputs_len - 1)\n" \
    "            r = sol_buffer_append_slice(&buffer, sol_str_slice_from_str(\"]\"));\n" \
    "        else\n" \
    "            r = sol_buffer_append_slice(&buffer, sol_str_slice_from_str(\",\"));\n" \
    "        SOL_INT_CHECK_GOTO(r, > 0, end);\n" \
    "    }\n" \
    "    if (sol_http_params_add(&params,\n" \
    "        SOL_HTTP_REQUEST_PARAM_POST_DATA_CONTENTS(\"json\", sol_buffer_get_slice(&buffer))) < 0) {\n" \
    "        SOL_WRN(\"Failed to set params\");\n" \
    "        r = -ENOMEM;\n" \
    "        goto end;\n" \
    "    }\n" \
    "    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, cdata->url,\n" \
    "        &params, http_composed_client_request_finished, node);\n" \
    "    if (connection == NULL) {\n" \
    "        SOL_WRN(\"Could not create the request for: %s\", cdata->url);\n" \
    "        r = -ENOTCONN;\n" \
    "        goto end;\n" \
    "    }\n" \
    "    r = sol_ptr_vector_append(&cdata->pending_conns, connection);\n" \
    "    if (r < 0) {\n" \
    "        SOL_WRN(\"Failed to keep pending connection (%s).\", cdata->url);\n" \
    "        sol_http_client_connection_cancel(connection);\n" \
    "    }\n" \
    "end:\n" \
    "    sol_http_params_clear(&params);\n" \
    "    sol_buffer_fini(&buffer);\n" \
    "    return r;\n" \
    "}\n" \
    "static int\n" \
    "http_composed_client_url_process(struct sol_flow_node *node, void *data, uint16_t port,\n" \
    "    uint16_t conn_id, const struct sol_flow_packet *packet)\n" \
    "{\n" \
    "    int r;\n" \
    "    const char *url;\n" \
    "    struct http_composed_client_data *cdata = data;\n" \
    "    r = sol_flow_packet_get_string(packet, &url);\n" \
    "    SOL_INT_CHECK(r, < 0, r);\n" \
    "    r = sol_util_replace_str_if_changed(&cdata->url, url);\n" \
    "    SOL_INT_CHECK(r, < 0, r);\n" \
    "    return 0;\n" \
    "}\n" \
    "static int\n" \
    "http_composed_client_in_process(struct sol_flow_node *node, void *data, uint16_t port,\n" \
    "    uint16_t conn_id, const struct sol_flow_packet *packet)\n" \
    "{\n" \
    "    int r;\n" \
    "    uint16_t len, i;\n" \
    "    struct http_composed_client_data *cdata = data;\n" \
    "    struct sol_flow_packet **children;\n" \
    "    r = sol_flow_packet_get_composed_members(packet, &children, &len);\n" \
    "    SOL_INT_CHECK(r, < 0, r);\n" \
    "    for (i = 0; i < len; i++) {\n" \
    "        sol_flow_packet_del(cdata->inputs[i]);\n" \
    "        cdata->inputs[i] = sol_flow_packet_dup(children[i]);\n" \
    "        SOL_NULL_CHECK(cdata->inputs[i], -ENOMEM);\n" \
    "    }\n" \
    "    return 0;\n" \
    "}\n" \
    "static int\n" \
    "http_composed_client_simple_process(struct sol_flow_node *node, void *data, uint16_t port,\n" \
    "    uint16_t conn_id, const struct sol_flow_packet *packet)\n" \
    "{\n" \
    "    struct http_composed_client_data *hdata = data;\n" \
    "    if (hdata->inputs[port]) {\n" \
    "        sol_flow_packet_del(hdata->inputs[port]);\n" \
    "        hdata->inputs[port] = NULL;\n" \
    "    }\n" \
    "    hdata->inputs[port] = sol_flow_packet_dup(packet);\n" \
    "    SOL_NULL_CHECK(hdata->inputs[port], -ENOMEM);\n" \
    "    return 0;\n" \
    "}\n"
