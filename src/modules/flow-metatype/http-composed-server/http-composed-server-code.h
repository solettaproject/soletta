/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#define HTTP_COMPOSED_SERVER_CODE_START \
    "#include \"sol-flow-packet.h\"\n" \
    "#include \"sol-http.h\"\n" \
    "#include \"sol-http-server.h\"\n" \
    "#include \"sol-json.h\"\n" \
    "#include \"sol-log.h\"\n" \
    "#include \"sol-util.h\"\n" \
    "#include \"sol-macros.h\"\n\n" \
    "#define HTTP_HEADER_CONTENT_TYPE \"Content-Type\"\n" \
    "#define HTTP_HEADER_CONTENT_TYPE_TEXT \"text/plain\"\n" \
    "#define HTTP_HEADER_CONTENT_TYPE_JSON \"application/json\"\n" \
    "static struct sol_ptr_vector servers = SOL_PTR_VECTOR_INIT;\n" \
    "struct http_composed_server_type {\n" \
    "    struct sol_flow_node_type base;\n" \
    "    struct sol_vector ports_in;\n" \
    "    struct sol_vector ports_out;\n" \
    "    struct sol_ptr_vector servers;\n" \
    "};\n" \
    "struct http_composed_server_port_in {\n" \
    "    struct sol_flow_port_type_in base;\n" \
    "    const char *name;\n" \
    "};\n\n" \
    "struct http_composed_server_data {\n" \
    "    const struct sol_flow_packet_type *composed_type;\n" \
    "    struct sol_flow_packet **inputs;\n" \
    "    struct http_server *server;\n" \
    "    char *path;\n" \
    "    uint16_t inputs_len;\n" \
    "};\n" \
    "struct http_server {\n" \
    "    struct sol_http_server *server;\n" \
    "    int port;\n" \
    "    int refcount;\n" \
    "};\n" \
    "struct http_composed_server_port_out {\n" \
    "    struct sol_flow_port_type_out base;\n" \
    "    const char *name;\n" \
    "};\n\n" \
    "struct http_composed_server_options {\n" \
    "    struct sol_flow_node_options base;\n" \
    "#define SOL_FLOW_NODE_TYPE_HTTP_COMPOSED_SERVER_OPTIONS_API_VERSION (1)\n" \
    "    const char *path; /**< The path used to receive requests */\n" \
    "    int port;\n" \
    "};\n" \
    "static struct sol_flow_packet *\n" \
    "_create_packet_number(const struct sol_flow_packet_type *type,\n" \
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
    "_create_packet(const struct sol_flow_packet_type *type,\n" \
    "    const struct sol_json_token *token)\n" \
    "{\n" \
    "    enum sol_json_type json_type;\n" \
    "    json_type = sol_json_token_get_type(token);\n" \
    "    if (json_type == SOL_JSON_TYPE_FALSE) {\n" \
    "        return sol_flow_packet_new_bool(false);\n" \
    "    } else if (json_type == SOL_JSON_TYPE_TRUE) {\n" \
    "        return sol_flow_packet_new_bool(true);\n" \
    "    } else if (json_type == SOL_JSON_TYPE_NUMBER) {\n" \
    "        return _create_packet_number(type, token);\n" \
    "    } else if (json_type == SOL_JSON_TYPE_STRING) {\n" \
    "        int r;\n" \
    "        struct sol_buffer buffer;\n" \
    "        r = sol_json_token_get_unescaped_string(token, &buffer);\n" \
    "        SOL_INT_CHECK(r, < 0, NULL);\n" \
    "        return sol_flow_packet_new_string_slice(sol_buffer_get_slice(&buffer));\n" \
    "    }\n" \
    "    return NULL;\n" \
    "}\n" \
    "static int\n" \
    "_process_json(struct sol_flow_node *node, struct sol_str_slice contents)\n" \
    "{\n" \
    "    uint16_t i = 0;\n" \
    "    struct sol_json_scanner scanner;\n" \
    "    struct sol_json_token token;\n" \
    "    enum sol_json_loop_status reason;\n" \
    "    struct http_composed_server_data *hdata = sol_flow_node_get_private_data(node);\n" \
    "    const struct sol_flow_node_type *type = sol_flow_node_get_type(node);\n" \
    "    sol_json_scanner_init(&scanner, contents.data, contents.len);\n" \
    "    SOL_JSON_SCANNER_ARRAY_LOOP(&scanner, &token, reason) {\n" \
    "        struct http_composed_server_port_in *in;\n" \
    "        if (i >= hdata->inputs_len)\n" \
    "            return -EINVAL;\n" \
    "        in = (struct http_composed_server_port_in *)type->get_port_in(type, i);\n" \
    "        sol_flow_packet_del(hdata->inputs[i]);\n" \
    "        hdata->inputs[i] = _create_packet(in->base.packet_type, &token);\n" \
    "        SOL_NULL_CHECK(hdata->inputs[i], -ENOMEM);\n" \
    "        i++;\n" \
    "    }\n" \
    "    SOL_INT_CHECK(i, != hdata->inputs_len, -EINVAL);\n" \
    "    sol_flow_send_composed_packet(node, 0, hdata->composed_type,\n" \
    "        hdata->inputs);\n" \
    "    return 0;\n" \
    "}\n" \
    "static int\n" \
    "_process_post(struct sol_flow_node *node, struct sol_http_request *request)\n" \
    "{\n" \
    "    uint16_t i;\n" \
    "    int r = -EINVAL;\n" \
    "    struct sol_http_param_value *value;\n" \
    "    struct sol_str_slice contents;\n" \
    "    SOL_HTTP_PARAMS_FOREACH_IDX (sol_http_request_get_params(request),\n" \
    "        value, i) {\n" \
    "        if (value->type != SOL_HTTP_PARAM_POST_DATA)\n" \
    "            continue;\n" \
    "        contents = value->value.data.value;\n" \
    "        r = _process_json(node, contents);\n" \
    "        break;\n" \
    "    }\n" \
    "    return r;\n" \
    "}\n" \
    "static int\n" \
    "_process_get(struct http_composed_server_data *hdata,\n" \
    "    struct sol_http_response *response)\n" \
    "{\n" \
    "    int r;\n" \
    "    uint16_t i;\n" \
    "    r = sol_buffer_append_char(&response->content, \'[\');\n" \
    "    SOL_INT_CHECK(r, < 0, r);\n" \
    "    if (sol_http_params_add(&response->param,\n" \
    "        SOL_HTTP_REQUEST_PARAM_HEADER(HTTP_HEADER_CONTENT_TYPE,\n" \
    "        HTTP_HEADER_CONTENT_TYPE_JSON)) < 0) {\n" \
    "        return -ENOMEM;\n" \
    "    }\n" \
    "    for (i = 0; i < hdata->inputs_len; i++) {\n" \
    "        const struct sol_flow_packet_type *packet_type;\n" \
    "        if (!hdata->inputs[i])\n" \
    "            return -EINVAL;\n" \
    "        if (i) {\n" \
    "            r = sol_buffer_append_char(&response->content, \',\');\n" \
    "            SOL_INT_CHECK(r, < 0, r);\n" \
    "        }\n" \
    "        packet_type = sol_flow_packet_get_type(hdata->inputs[i]);\n" \
    "        if (packet_type == SOL_FLOW_PACKET_TYPE_STRING) {\n" \
    "            const char *val;\n" \
    "            r = sol_flow_packet_get_string(hdata->inputs[i], &val);\n" \
    "            SOL_INT_CHECK(r, < 0, r);\n" \
    "            r = sol_json_serialize_string(&response->content, val);\n" \
    "        } else if (packet_type == SOL_FLOW_PACKET_TYPE_BOOL) {\n" \
    "            bool val;\n" \
    "            r = sol_flow_packet_get_bool(hdata->inputs[i], &val);\n" \
    "            SOL_INT_CHECK(r, < 0, r);\n" \
    "            r = sol_json_serialize_bool(&response->content, val);\n" \
    "        } else if (packet_type == SOL_FLOW_PACKET_TYPE_IRANGE) {\n" \
    "            int32_t val;\n" \
    "            r = sol_flow_packet_get_irange_value(hdata->inputs[i], &val);\n" \
    "            SOL_INT_CHECK(r, < 0, r);\n" \
    "            r = sol_json_serialize_int32(&response->content, val);\n" \
    "        } else if (packet_type == SOL_FLOW_PACKET_TYPE_DRANGE) {\n" \
    "            double val;\n" \
    "            r = sol_flow_packet_get_drange_value(hdata->inputs[i], &val);\n" \
    "            SOL_INT_CHECK(r, < 0, r);\n" \
    "            r = sol_json_serialize_double(&response->content, val);\n" \
    "        } else {\n" \
    "            return -EINVAL;\n" \
    "        }\n" \
    "        SOL_INT_CHECK(r, < 0, r);\n" \
    "    }\n" \
    "    r = sol_buffer_append_char(&response->content, \']\');\n" \
    "    return r;\n" \
    "}\n" \
    "static int\n" \
    "http_response_cb(void *data, struct sol_http_request *request)\n" \
    "{\n" \
    "    int r = -EINVAL;\n" \
    "    enum sol_http_method method;\n" \
    "    struct sol_flow_node *node = data;\n" \
    "    struct http_composed_server_data *hdata;\n" \
    "    struct sol_http_response response = {\n" \
    "        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )\n" \
    "        .content = SOL_BUFFER_INIT_EMPTY,\n" \
    "        .param = SOL_HTTP_REQUEST_PARAMS_INIT,\n" \
    "        .response_code = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR\n" \
    "    };\n" \
    "    hdata = sol_flow_node_get_private_data(node);\n" \
    "    SOL_NULL_CHECK_GOTO(hdata, end);\n" \
    "    method = sol_http_request_get_method(request);\n" \
    "    switch (method) {\n" \
    "    case SOL_HTTP_METHOD_POST:\n" \
    "        r = _process_post(node, request);\n" \
    "        break;\n" \
    "    case SOL_HTTP_METHOD_GET:\n" \
    "        r = _process_get(hdata, &response);\n" \
    "        break;\n" \
    "    default:\n" \
    "        SOL_WRN(\"Invalid method: %d\", method);\n" \
    "        break;\n" \
    "    }\n" \
    "end:\n" \
    "    if (r < 0) {\n" \
    "        sol_buffer_reset(&response.content);\n" \
    "        sol_http_params_clear(&response.param);\n" \
    "        sol_buffer_append_printf(&response.content,\n" \
    "            \"Could not serve request: %s\", sol_util_strerrora(-r));\n" \
    "        if (sol_http_params_add(&response.param, SOL_HTTP_REQUEST_PARAM_HEADER(\n" \
    "            HTTP_HEADER_CONTENT_TYPE, HTTP_HEADER_CONTENT_TYPE_TEXT)) < 0) {\n" \
    "            SOL_WRN(\"could not set response content-type: text/plain: %s\",\n" \
    "                sol_util_strerrora(-r));\n" \
    "        }\n" \
    "    } else {\n" \
    "        response.response_code = SOL_HTTP_STATUS_OK;\n" \
    "    }\n" \
    "    sol_http_server_send_response(request, &response);\n" \
    "    sol_buffer_fini(&response.content);\n" \
    "    sol_http_params_clear(&response.param);\n" \
    "    return r;\n" \
    "}\n" \
    "static struct http_server *\n" \
    "server_ref(int32_t port)\n" \
    "{\n" \
    "    struct http_server *idata, *sdata = NULL;\n" \
    "    uint16_t i;\n" \
    "    if ((port > UINT16_MAX) || port < 0) {\n" \
    "        SOL_WRN(\"Invalid server port (%\" PRId32 \"). It must be in range \"\n" \
    "            \"0 - (%\" PRId32 \"). Using default port  (%\" PRId32 \").\",\n" \
    "            port, UINT16_MAX, HTTP_SERVER_PORT);\n" \
    "        port = HTTP_SERVER_PORT;\n" \
    "    }\n" \
    "    SOL_PTR_VECTOR_FOREACH_IDX (&servers, idata, i) {\n" \
    "        if (idata->port == port) {\n" \
    "            sdata = idata;\n" \
    "            break;\n" \
    "        }\n" \
    "    }\n" \
    "    if (!sdata) {\n" \
    "        int r;\n" \
    "        sdata = calloc(1, sizeof(struct http_server));\n" \
    "        SOL_NULL_CHECK_GOTO(sdata, err_sdata);\n" \
    "        r = sol_ptr_vector_append(&servers, sdata);\n" \
    "        SOL_INT_CHECK_GOTO(r, < 0, err_vec);\n" \
    "        sdata->server = sol_http_server_new(&(struct sol_http_server_config) {\n" \
    "            SOL_SET_API_VERSION(.api_version = SOL_HTTP_SERVER_CONFIG_API_VERSION, )\n" \
    "            .port = port\n" \
    "        });\n" \
    "        SOL_NULL_CHECK_GOTO(sdata->server, err_server);\n" \
    "        sdata->port = port;\n" \
    "    }\n" \
    "    sdata->refcount++;\n" \
    "    return sdata;\n" \
    "err_server:\n" \
    "    sol_ptr_vector_remove(&servers, sdata);\n" \
    "err_vec:\n" \
    "    free(sdata);\n" \
    "err_sdata:\n" \
    "    return NULL;\n" \
    "}\n" \
    "static void\n" \
    "server_unref(struct http_server *sdata)\n" \
    "{\n" \
    "    sdata->refcount--;\n" \
    "    if (sdata->refcount > 0)\n" \
    "        return;\n" \
    "    sol_ptr_vector_remove(&servers, sdata);\n" \
    "    sol_http_server_del(sdata->server);\n" \
    "    free(sdata);\n" \
    "}\n" \
    "static int http_composed_server_open(struct sol_flow_node *node, void *data,\n" \
    "    const struct sol_flow_node_options *options)\n {\n" \
    "    int r;\n" \
    "    struct http_composed_server_data *cdata = data;\n" \
    "    const struct sol_flow_node_type *self;\n" \
    "    const struct http_composed_server_options *opts;\n" \
    "    const struct http_composed_server_port_out *out;\n" \
    "    opts = (struct http_composed_server_options *)options;\n" \
    "    cdata->path = strdup(opts->path ?: \"/\");\n" \
    "    SOL_NULL_CHECK(cdata->path, -ENOMEM);\n" \
    "    self = sol_flow_node_get_type(node);\n" \
    "    cdata->server = server_ref(opts->port);\n" \
    "    SOL_NULL_CHECK_GOTO(cdata->server, err);\n" \
    "    r = sol_http_server_register_handler(cdata->server->server, cdata->path,\n" \
    "        http_response_cb, node);\n" \
    "    SOL_INT_CHECK_GOTO(r, < 0, err_handler);\n" \
    "    cdata->inputs_len = self->ports_in_count - 1;\n" \
    "    cdata->inputs = calloc(cdata->inputs_len, sizeof(struct sol_flow_packet *));\n" \
    "    SOL_NULL_CHECK_GOTO(cdata->inputs, err_inputs);\n" \
    "    out = (struct http_composed_server_port_out *)self->get_port_out(self, 0);\n" \
    "    cdata->composed_type = out->base.packet_type;\n" \
    "    return 0;\n" \
    "err_inputs:\n" \
    "    sol_http_server_unregister_handler(cdata->server->server, cdata->path);\n" \
    "err_handler:\n" \
    "    server_unref(cdata->server);\n" \
    "err:\n" \
    "    free(cdata->path);\n" \
    "    return -ENOMEM;\n" \
    "}\n" \
    "static void\n" \
    "http_composed_server_close(struct sol_flow_node *node, void *data)\n" \
    "{\n" \
    "    struct http_composed_server_data *cdata = data;\n" \
    "    uint16_t i;\n" \
    "    for (i = 0; i < cdata->inputs_len; i++)\n" \
    "        if (cdata->inputs[i])\n" \
    "            sol_flow_packet_del(cdata->inputs[i]);\n" \
    "    sol_http_server_unregister_handler(cdata->server->server, cdata->path);\n" \
    "    server_unref(cdata->server);\n" \
    "    free(cdata->path);\n" \
    "    free(cdata->inputs);\n" \
    "}\n" \
    "static int\n" \
    "http_composed_server_simple_process(struct sol_flow_node *node, void *data, uint16_t port,\n" \
    "    uint16_t conn_id, const struct sol_flow_packet *packet)\n" \
    "{\n" \
    "    struct http_composed_server_data *hdata = data;\n" \
    "    if (hdata->inputs[port])\n" \
    "        sol_flow_packet_del(hdata->inputs[port]);\n" \
    "    hdata->inputs[port] = sol_flow_packet_dup(packet);\n" \
    "    SOL_NULL_CHECK(hdata->inputs[port], -ENOMEM);\n" \
    "    return 0;\n" \
    "}\n" \
    "static int\n" \
    "http_composed_server_in_process(struct sol_flow_node *node, void *data, uint16_t port,\n" \
    "    uint16_t conn_id, const struct sol_flow_packet *packet)\n" \
    "{\n" \
    "    int r;\n" \
    "    uint16_t len, i;\n" \
    "    struct http_composed_server_data *cdata = data;\n" \
    "    struct sol_flow_packet **children;\n" \
    "    r = sol_flow_packet_get_composed_members(packet, &children, &len);\n" \
    "    SOL_INT_CHECK(r, < 0, r);\n" \
    "    for (i = 0; i < len; i++) {\n" \
    "        sol_flow_packet_del(cdata->inputs[i]);\n" \
    "        cdata->inputs[i] = sol_flow_packet_dup(children[i]);\n" \
    "        SOL_NULL_CHECK(cdata->inputs[i], -ENOMEM);\n" \
    "    }\n" \
    "    return 0;\n" \
    "}\n"
