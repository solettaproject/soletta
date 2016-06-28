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
#include <float.h>
#include <stdio.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-metatype-http-composed-client");

#include "sol-arena.h"
#include "sol-flow-metatype.h"
#include "sol-http.h"
#include "sol-http-client.h"
#include "sol-json.h"
#include "sol-log.h"
#include "sol-util.h"
#include "sol-util-internal.h"

#include "http-composed-client-code.h"

#define DELIM ("|")

#define INPUT_PORT_NAME ("IN")
#define INPUT_URL_PORT_NAME ("URL")
#define INPUT_GET_PORT_NAME ("GET")
#define INPUT_POST_PORT_NAME ("POST")
#define INPUT_FIXED_PORTS_LEN 4

#define OUTPUT_PORT_NAME ("OUT")

struct http_composed_client_type {
    struct sol_flow_node_type base;

    struct sol_vector ports_in;
    struct sol_vector ports_out;
};

struct http_composed_client_port_in {
    struct sol_flow_port_type_in base;
    char *name;
};

struct http_composed_client_port_out {
    struct sol_flow_port_type_out base;
    char *name;
};

struct http_composed_client_data {
    uint16_t inputs_len;
    char *url;
    struct sol_ptr_vector pending_conns;
    const struct sol_flow_packet_type *composed_type;
    struct sol_flow_packet **inputs;
};

struct http_composed_client_options {
    struct sol_flow_node_options base;
#define SOL_FLOW_NODE_TYPE_HTTP_COMPOSED_CLIENT_OPTIONS_API_VERSION (1)
    const char *url; /**< The url used on requests (optional) */
};

static void
http_composed_client_close(struct sol_flow_node *node, void *data)
{
    struct sol_http_client_connection *connection;
    struct http_composed_client_data *cdata = data;
    uint16_t i;

    for (i = 0; i < cdata->inputs_len; i++)
        if (cdata->inputs[i])
            sol_flow_packet_del(cdata->inputs[i]);

    SOL_PTR_VECTOR_FOREACH_IDX (&cdata->pending_conns, connection, i)
        sol_http_client_connection_cancel(connection);
    sol_ptr_vector_clear(&cdata->pending_conns);

    free(cdata->url);
    free(cdata->inputs);
}

static int
http_composed_client_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct http_composed_client_data *cdata = data;
    const struct http_composed_client_type *self;
    const struct http_composed_client_options *opts;
    const struct http_composed_client_port_out *out;

    opts = (struct http_composed_client_options *)options;

    if (opts->url) {
        cdata->url = strdup(opts->url);
        SOL_NULL_CHECK(cdata->url, -ENOMEM);
    }

    self = (const struct http_composed_client_type *)
        sol_flow_node_get_type(node);

    sol_ptr_vector_init(&cdata->pending_conns);

    cdata->inputs_len = self->ports_in.len - INPUT_FIXED_PORTS_LEN;
    cdata->inputs = calloc(cdata->inputs_len, sizeof(struct sol_flow_packet *));
    SOL_NULL_CHECK_GOTO(cdata->inputs, err);

    out = sol_vector_get(&self->ports_out, 0);
    cdata->composed_type = out->base.packet_type;

    return 0;

err:
    free(cdata->url);
    return -ENOMEM;
}

static void
http_composed_client_type_dispose(struct sol_flow_node_type *type)
{
    struct http_composed_client_type *self = (struct http_composed_client_type *)type;
    struct http_composed_client_port_out *port_out;
    struct http_composed_client_port_in *port_in;
    uint16_t i;

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    struct sol_flow_node_type_description *desc;

    desc = (struct sol_flow_node_type_description *)self->base.description;
    if (desc) {
        if (desc->ports_in) {
            for (i = 0; i < self->ports_in.len; i++)
                free((struct sol_flow_port_description *)desc->ports_in[i]);
            free((struct sol_flow_port_description **)desc->ports_in);
        }
        if (desc->ports_out) {
            for (i = 0; i < self->ports_out.len; i++)
                free((struct sol_flow_port_description *)desc->ports_out[i]);
            free((struct sol_flow_port_description **)desc->ports_out);
        }
        free(desc);
    }
#endif

    SOL_VECTOR_FOREACH_IDX (&self->ports_in, port_in, i)
        free(port_in->name);
    SOL_VECTOR_FOREACH_IDX (&self->ports_out, port_out, i)
        free(port_out->name);

    sol_vector_clear(&self->ports_in);
    sol_vector_clear(&self->ports_out);
    free(self);
}

static int
http_composed_client_simple_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_composed_client_data *hdata = data;

    if (hdata->inputs[port])
        sol_flow_packet_del(hdata->inputs[port]);

    hdata->inputs[port] = sol_flow_packet_dup(packet);
    SOL_NULL_CHECK(hdata->inputs[port], -ENOMEM);

    return 0;
}

static bool
http_composed_client_data_check(struct http_composed_client_data *cdata)
{
    uint16_t i;

    if (!cdata->url)
        return false;

    for (i = 0; i < cdata->inputs_len; i++) {
        if (!cdata->inputs[i])
            break;
    }

    if (i != cdata->inputs_len)
        return false;

    return true;
}

static struct sol_flow_packet *
http_composed_client_create_packet_number(const struct sol_flow_packet_type *type,
    const struct sol_json_token *token)
{
    int r;

    if (type == SOL_FLOW_PACKET_TYPE_IRANGE) {
        int32_t value;

        r = sol_json_token_get_int32(token, &value);
        SOL_INT_CHECK(r, < 0, NULL);

        return sol_flow_packet_new_irange_value(value);
    } else if (type == SOL_FLOW_PACKET_TYPE_DRANGE) {
        double value;

        r = sol_json_token_get_double(token, &value);
        SOL_INT_CHECK(r, < 0, NULL);

        return sol_flow_packet_new_drange_value(value);
    } else if (type == SOL_FLOW_PACKET_TYPE_BYTE) {
        int32_t value;

        r = sol_json_token_get_int32(token, &value);
        SOL_INT_CHECK(r, < 0, NULL);

        if (value < 0)
            value = 0;
        else if (value > UINT8_MAX)
            value = UINT8_MAX;

        return sol_flow_packet_new_byte((uint8_t)value);
    }

    return NULL;
}

static struct sol_flow_packet *
http_composed_client_create_packet(const struct sol_flow_packet_type *type,
    const struct sol_json_token *token)
{
    enum sol_json_type json_type;

    json_type = sol_json_token_get_type(token);
    if (json_type == SOL_JSON_TYPE_FALSE) {
        return sol_flow_packet_new_bool(false);
    } else if (json_type == SOL_JSON_TYPE_TRUE) {
        return sol_flow_packet_new_bool(true);
    } else if (json_type == SOL_JSON_TYPE_NUMBER) {
        return http_composed_client_create_packet_number(type, token);
    } else if (json_type == SOL_JSON_TYPE_STRING) {
        int r;
        struct sol_buffer buffer;

        r = sol_json_token_get_unescaped_string(token, &buffer);
        SOL_INT_CHECK(r, < 0, NULL);

        return sol_flow_packet_new_string_slice(sol_buffer_get_slice(&buffer));
    }

    return NULL;
}

static void
http_composed_client_request_finished(void *data,
    struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    int r = 0;
    uint16_t i = 0;
    struct sol_flow_node *node = data;
    struct http_composed_client_type *ntype =
        (struct http_composed_client_type *)sol_flow_node_get_type(node);
    struct http_composed_client_data *cdata = sol_flow_node_get_private_data(node);

    if (sol_ptr_vector_remove(&cdata->pending_conns, connection) < 0)
        SOL_WRN("Failed to find pending connection %p", connection);

    if (!response) {
        sol_flow_send_error_packet(node, EINVAL,
            "Error while reaching %s", cdata->url);
        return;
    }

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        sol_flow_send_error_packet(node, EINVAL,
            "%s returned an unhandled response code: %d",
            cdata->url, response->response_code);
        return;
    }

    if (!response->content.used)
        return;

    if (streq(response->content_type, "application/json")) {
        struct sol_json_scanner scanner;
        struct sol_json_token token;
        enum sol_json_loop_status reason;

        sol_json_scanner_init(&scanner, response->content.data, response->content.used);
        SOL_JSON_SCANNER_ARRAY_LOOP(&scanner, &token, reason) {
            struct http_composed_client_port_in *in = sol_vector_get(&ntype->ports_in, i);

            SOL_NULL_CHECK_GOTO(in, err);

            if (cdata->inputs[i])
                sol_flow_packet_del(cdata->inputs[i]);
            cdata->inputs[i] = http_composed_client_create_packet(in->base.packet_type, &token);
            SOL_NULL_CHECK_GOTO(cdata->inputs[i], err);
            i++;
        }
        SOL_INT_CHECK_GOTO(i, != cdata->inputs_len, err);

        sol_flow_send_composed_packet(node, 0, cdata->composed_type,
            cdata->inputs);
    }

    return;

err:
    sol_flow_send_error_packet(node, r,
        "%s Could not parse url contents ", cdata->url);
}

static int
http_composed_client_get_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_http_params params;
    struct sol_http_client_connection *connection;
    struct http_composed_client_data *cdata = data;

    if (!cdata->url)
        return -EINVAL;

    sol_http_params_init(&params);
    if (sol_http_params_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json")) < 0) {
        SOL_WRN("Failed to set query params");
        sol_http_params_clear(&params);
        return -ENOMEM;
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_GET, cdata->url,
        &params, http_composed_client_request_finished, node);
    sol_http_params_clear(&params);
    SOL_NULL_CHECK(connection, -ENOTCONN);

    r = sol_ptr_vector_append(&cdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection.");
        sol_http_client_connection_cancel(connection);
        return r;
    }

    return 0;
}

static int
http_composed_client_post_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r = 0;
    uint16_t i;
    struct sol_http_params params;
    struct http_composed_client_data *cdata = data;
    struct sol_http_client_connection *connection;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;

    if (!http_composed_client_data_check(cdata))
        return -EINVAL;

    sol_http_params_init(&params);

    r = sol_buffer_append_char(&buffer, '[');
    SOL_INT_CHECK_GOTO(r, > 0, end);

    for (i = 0; i < cdata->inputs_len; i++) {
        SOL_INT_CHECK_GOTO(r, > 0, end);

        if (sol_flow_packet_get_type(cdata->inputs[i]) == SOL_FLOW_PACKET_TYPE_IRANGE) {
            struct sol_irange value;

            r = sol_flow_packet_get_irange(cdata->inputs[i], &value);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_json_serialize_int32(&buffer, value.val);
            SOL_INT_CHECK_GOTO(r, < 0, end);
        } else if (sol_flow_packet_get_type(cdata->inputs[i]) == SOL_FLOW_PACKET_TYPE_BYTE) {
            uint8_t value;

            r = sol_flow_packet_get_byte(cdata->inputs[i], &value);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_json_serialize_int32(&buffer, value);
            SOL_INT_CHECK_GOTO(r, < 0, end);
        } else if (sol_flow_packet_get_type(cdata->inputs[i]) == SOL_FLOW_PACKET_TYPE_DRANGE) {
            struct sol_drange value;

            r = sol_flow_packet_get_drange(cdata->inputs[i], &value);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_json_serialize_double(&buffer, value.val);
            SOL_INT_CHECK_GOTO(r, < 0, end);
        } else if (sol_flow_packet_get_type(cdata->inputs[i]) == SOL_FLOW_PACKET_TYPE_BOOL) {
            bool value;

            r = sol_flow_packet_get_bool(cdata->inputs[i], &value);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_json_serialize_bool(&buffer, value);
            SOL_INT_CHECK_GOTO(r, < 0, end);
        } else if (sol_flow_packet_get_type(cdata->inputs[i]) == SOL_FLOW_PACKET_TYPE_STRING) {
            const char *value;

            r = sol_flow_packet_get_string(cdata->inputs[i], &value);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_json_serialize_string(&buffer, value);
            SOL_INT_CHECK_GOTO(r, < 0, end);
        }

        if (i == cdata->inputs_len - 1)
            r = sol_buffer_append_slice(&buffer, sol_str_slice_from_str("]"));
        else
            r = sol_buffer_append_slice(&buffer, sol_str_slice_from_str(","));
        SOL_INT_CHECK_GOTO(r, > 0, end);
    }

    if (sol_http_params_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_DATA_CONTENTS("json", sol_buffer_get_slice(&buffer))) < 0) {
        SOL_WRN("Failed to set params");
        r = -ENOMEM;
        goto end;
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, cdata->url,
        &params, http_composed_client_request_finished, node);
    if (connection == NULL) {
        SOL_WRN("Could not create the request for: %s", cdata->url);
        r = -ENOTCONN;
        goto end;
    }

    r = sol_ptr_vector_append(&cdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection (%s).", cdata->url);
        sol_http_client_connection_cancel(connection);
    }

end:
    sol_http_params_clear(&params);
    sol_buffer_fini(&buffer);
    return r;
}

static int
http_composed_client_url_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    const char *url;
    struct http_composed_client_data *cdata = data;

    r = sol_flow_packet_get_string(packet, &url);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_if_changed(&cdata->url, url);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
http_composed_client_in_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    uint16_t len, i;
    struct http_composed_client_data *cdata = data;
    struct sol_flow_packet **children;

    r = sol_flow_packet_get_composed_members(packet, &children, &len);
    SOL_INT_CHECK(r, < 0, r);

    for (i = 0; i < len; i++) {
        if (cdata->inputs[i])
            sol_flow_packet_del(cdata->inputs[i]);
        cdata->inputs[i] = sol_flow_packet_dup(children[i]);
        SOL_NULL_CHECK(cdata->inputs[i], -ENOMEM);
    }

    return 0;
}

static const struct sol_flow_port_type_in *
http_composed_client_get_port_in(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct http_composed_client_type *http_type = (struct http_composed_client_type *)type;
    const struct http_composed_client_port_in *p = sol_vector_get(&http_type->ports_in, port);

    return p ? &p->base : NULL;
}

static const struct sol_flow_port_type_out *
http_composed_client_get_port_out(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct http_composed_client_type *http_type = (struct http_composed_client_type *)type;
    const struct http_composed_client_port_out *p = sol_vector_get(&http_type->ports_out, port);

    return p ? &p->base : NULL;
}

static const struct http_composed_client_options
    http_composed_client_options_defaults = {
    .base = {
        SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_OPTIONS_API_VERSION, ) \
        SOL_SET_API_VERSION(.sub_api = SOL_FLOW_NODE_TYPE_HTTP_COMPOSED_CLIENT_OPTIONS_API_VERSION) \
    },
};

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
static const struct sol_flow_node_type_description sol_flow_node_type_http_client_composed_description = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION, )
    .name = "http-composed/client",
    .category = "http",
    .symbol = "SOL_FLOW_NODE_TYPE_HTTP_CLIENT_COMPOSED",
    .options_symbol = "http_composed_client_options",
    .description = "Provides an HTTP client that supports composed packets",
    .url = "http://solettaproject.org/doc/latest/components/http-composed-client.html",
    .license = "Apache-2.0",
    .version = "1",
    .options = &((const struct sol_flow_node_options_description){
        .data_size = sizeof(struct http_composed_client_options),
        SOL_SET_API_VERSION(.sub_api = SOL_FLOW_NODE_TYPE_HTTP_COMPOSED_CLIENT_OPTIONS_API_VERSION, )
        .required = true,
        .members = (const struct sol_flow_node_options_member_description[]){
            {
                .name = "url",
                .description = "The URL used on requests",
                .data_type = "string",
                .required = false,
                .offset = offsetof(struct http_composed_client_options, url),
                .size = sizeof(const char *),
            },
            {},
        },
    }),
};

static int
setup_description(struct http_composed_client_type *type)
{
    struct sol_flow_node_type_description *desc;
    struct sol_flow_port_description **p;
    struct http_composed_client_port_in *port_type_in;
    struct http_composed_client_port_out *port_type_out;
    int i, j;

    type->base.description = calloc(1, sizeof(struct sol_flow_node_type_description));
    SOL_NULL_CHECK(type->base.description, -ENOMEM);

    type->base.description = memcpy((struct sol_flow_node_type_description *)type->base.description,
        &sol_flow_node_type_http_client_composed_description, sizeof(struct sol_flow_node_type_description));

    desc = (struct sol_flow_node_type_description *)type->base.description;

    /* Extra slot for NULL sentinel at the end. */
    desc->ports_in = calloc(type->ports_in.len + 1, sizeof(struct sol_flow_port_description *));
    SOL_NULL_CHECK_GOTO(desc->ports_in, fail_ports_in);

    p = (struct sol_flow_port_description **)desc->ports_in;
    for (i = 0; i < type->ports_in.len; i++) {
        p[i] = calloc(1, sizeof(struct sol_flow_port_description));
        SOL_NULL_CHECK_GOTO(p[i], fail_ports_in_desc);

        port_type_in = sol_vector_get(&type->ports_in, i);

        p[i]->name = port_type_in->name;
        p[i]->description = "Input port";
        p[i]->data_type = port_type_in->base.packet_type->name;
        p[i]->array_size = 0;
        p[i]->base_port_idx = i;
        p[i]->required = false;
    }

    /* Extra slot for NULL sentinel at the end. */
    desc->ports_out = calloc(type->ports_out.len + 1, sizeof(struct sol_flow_port_description *));
    SOL_NULL_CHECK_GOTO(desc->ports_out, fail_ports_in_desc);

    p = (struct sol_flow_port_description **)desc->ports_out;
    for (j = 0; j < type->ports_out.len; j++) {
        p[j] = calloc(1, sizeof(struct sol_flow_port_description));
        SOL_NULL_CHECK_GOTO(p[j], fail_ports_out_desc);

        port_type_out = sol_vector_get(&type->ports_out, j);

        p[j]->name = port_type_out->name;
        p[j]->description = "Output port";
        p[j]->data_type = port_type_out->base.packet_type->name;
        p[j]->array_size = 0;
        p[j]->base_port_idx = j;
        p[j]->required = false;
    }

    return 0;

fail_ports_out_desc:
    if (j > 0) {
        for (; j >= 0; j--) {
            free((struct sol_flow_port_description *)desc->ports_out[j]);
        }
    }
    free((struct sol_flow_port_description **)desc->ports_out);
fail_ports_in_desc:
    if (i > 0) {
        for (; i >= 0; i--) {
            free((struct sol_flow_port_description *)desc->ports_in[i]);
        }
    }
    free((struct sol_flow_port_description **)desc->ports_in);
fail_ports_in:
    free(desc);
    return -ENOMEM;
}

static void
free_description(struct http_composed_client_type *type)
{
    struct sol_flow_node_type_description *desc;
    uint16_t i;

    desc = (struct sol_flow_node_type_description *)type->base.description;

    for (i = 0; i < type->ports_in.len; i++)
        free((struct sol_flow_port_description *)desc->ports_in[i]);
    free((struct sol_flow_port_description **)desc->ports_in);

    for (i = 0; i < type->ports_out.len; i++)
        free((struct sol_flow_port_description *)desc->ports_out[i]);
    free((struct sol_flow_port_description **)desc->ports_out);

    free(desc);
}
#endif

static void
http_composed_client_type_fini(struct http_composed_client_type *type)
{
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    if (type->base.description)
        free_description(type);
#endif

    sol_vector_clear(&type->ports_in);
    sol_vector_clear(&type->ports_out);
}

static int
get_name_and_type_from_token(const struct sol_str_slice *token, char **name,
    struct sol_str_slice *type)
{
    char *start, *end;

    start = memchr(token->data, '(', token->len);
    SOL_NULL_CHECK(start, -EINVAL);

    end = memrchr(token->data, ')', token->len);
    SOL_NULL_CHECK(end, -EINVAL);

    *name = strndup(token->data, start - token->data);
    SOL_NULL_CHECK(*name, -ENOMEM);

    type->data = start + 1;
    type->len = end - start - 1;
    return 0;
}

static int
setup_composed_ports(struct http_composed_client_port_out *port, struct sol_vector *ports_in)
{
    struct http_composed_client_port_in *port_in;
    const struct sol_flow_packet_type *composed_type;
    const struct sol_flow_packet_type **types;
    uint16_t i;

    types = alloca(sizeof(struct sol_flow_packet_type *) *
        (ports_in->len + 1));
    SOL_VECTOR_FOREACH_IDX (ports_in, port_in, i)
        types[i] = port_in->base.packet_type;

    types[i] = NULL;
    composed_type = sol_flow_packet_type_composed_new(types);
    SOL_NULL_CHECK(composed_type, -ENOMEM);

    port->name = strdup(OUTPUT_PORT_NAME);
    SOL_SET_API_VERSION(port->base.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION; )
    port->base.packet_type = composed_type;
    SOL_NULL_CHECK(port->name, -ENOMEM);

    port_in = sol_vector_append(ports_in);
    SOL_NULL_CHECK(port_in, -ENOMEM);

    port_in->name = strdup(INPUT_PORT_NAME);
    SOL_SET_API_VERSION(port_in->base.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION; )
    port_in->base.packet_type = composed_type;
    port_in->base.process = http_composed_client_in_process;
    SOL_NULL_CHECK(port_in->name, -ENOMEM);

    return 0;
}

static int
get_context_tokens(const struct sol_str_slice *contents,
    struct sol_buffer *buffer, struct sol_vector *tokens)
{
    struct sol_str_slice pending_slice;
    size_t i_slice;
    int r;

    sol_buffer_init(buffer);

    pending_slice.data = contents->data;
    pending_slice.len = 0;

    for (i_slice = 0; i_slice < contents->len; i_slice++) {
        if (isspace(contents->data[(uint8_t)i_slice])) {
            if (pending_slice.len != 0) {
                r = sol_buffer_append_slice(buffer, pending_slice);
                if (r) {
                    SOL_ERR("Could not append a slice in the buffer");
                    sol_buffer_fini(buffer);
                    return r;
                }
            }
            pending_slice.data = contents->data + i_slice + 1;
            pending_slice.len = 0;
        } else
            pending_slice.len++;
    }

    if (pending_slice.len > 0) {
        r = sol_buffer_append_slice(buffer, pending_slice);
        if (r) {
            SOL_ERR("Could not append slice to the buffer");
            sol_buffer_fini(buffer);
            return r;
        }
    }

    *tokens = sol_str_slice_split(sol_buffer_get_slice(buffer), DELIM, 0);
    return 0;
}

static int
setup_ports(struct sol_vector *in_ports, struct sol_vector *ports_out,
    const struct sol_str_slice contents)
{
    const struct sol_flow_packet_type *packet_type;
    struct http_composed_client_port_out *out;
    struct http_composed_client_port_in *port;
    struct sol_str_slice *slice;
    struct sol_vector tokens;
    struct sol_buffer buf;
    uint16_t i;
    int r;

    r = get_context_tokens(&contents, &buf, &tokens);
    SOL_INT_CHECK(r, < 0, r);

    SOL_VECTOR_FOREACH_IDX (&tokens, slice, i) {
        struct sol_str_slice type_slice;
        char *name = NULL;

        r = get_name_and_type_from_token(slice, &name, &type_slice);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        port = sol_vector_append(in_ports);
        if (!port) {
            r = -ENOMEM;
            SOL_ERR("Could not create a port");
            free(name);
            goto err_exit;
        }

        packet_type = sol_flow_packet_type_from_string(type_slice);
        if (!packet_type) {
            r = -EINVAL;
            SOL_ERR("It's not possible to use %.*s as a port type.",
                SOL_STR_SLICE_PRINT(type_slice));
            free(name);
            goto err_exit;
        }

        SOL_SET_API_VERSION(port->base.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION; )
        port->base.packet_type = packet_type;
        port->base.process = http_composed_client_simple_process;
        port->name = name;
    }

    sol_vector_clear(&tokens);
    sol_buffer_fini(&buf);

    out = sol_vector_append(ports_out);
    SOL_NULL_CHECK(out, -ENOMEM);

    r = setup_composed_ports(out, in_ports);
    SOL_INT_CHECK(r, < 0, r);

#define SETUP_PORT(_packet, _process, _name) \
    do { \
        port = sol_vector_append(in_ports); \
        SOL_NULL_CHECK(port, -ENOMEM); \
        SOL_SET_API_VERSION(port->base.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION; ) \
        port->base.packet_type = SOL_FLOW_PACKET_TYPE_ ## _packet; \
        port->base.process = _process; \
        port->name = strdup(INPUT_ ## _name ## _PORT_NAME); \
        SOL_NULL_CHECK(port->name, -ENOMEM); \
    } while (0)

    SETUP_PORT(ANY, http_composed_client_get_process, GET);
    SETUP_PORT(ANY, http_composed_client_post_process, POST);
    SETUP_PORT(STRING, http_composed_client_url_process, URL);
#undef SETUP_PORT

    return 0;

err_exit:
    sol_vector_clear(&tokens);
    sol_buffer_fini(&buf);
    return r;
}

static int
http_composed_client_type_init(struct http_composed_client_type *type, const struct sol_str_slice contents)
{
    int r;
    uint16_t idx;
    struct http_composed_client_port_in *port_in;
    struct http_composed_client_port_out *port_out;

    *type = (const struct http_composed_client_type) {
        .base = {
            SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_API_VERSION, )
            .data_size = sizeof(struct http_composed_client_data),
            .get_port_in = http_composed_client_get_port_in,
            .get_port_out = http_composed_client_get_port_out,
            .open = http_composed_client_open,
            .close = http_composed_client_close,
            .dispose_type = http_composed_client_type_dispose,
            .options_size = sizeof(struct http_composed_client_options),
            .default_options = &http_composed_client_options_defaults,
        },
    };

    sol_vector_init(&type->ports_out, sizeof(struct http_composed_client_port_out));
    sol_vector_init(&type->ports_in, sizeof(struct http_composed_client_port_in));

    r = setup_ports(&type->ports_in, &type->ports_out, contents);
    SOL_INT_CHECK_GOTO(r, < 0, err);

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    if (setup_description(type) < 0)
        SOL_WRN("Failed to setup description");
#endif

    return 0;

err:
    SOL_VECTOR_FOREACH_IDX (&type->ports_in, port_in, idx)
        free(port_in->name);
    SOL_VECTOR_FOREACH_IDX (&type->ports_out, port_out, idx)
        free(port_out->name);

    sol_vector_clear(&type->ports_in);
    sol_vector_clear(&type->ports_out);
    type = NULL;

    return r;
}

static struct sol_flow_node_type *
http_composed_client_new_type(const struct sol_str_slice contents)
{
    int r;
    struct http_composed_client_type *type;

    SOL_LOG_INTERNAL_INIT_ONCE;

    type = calloc(1, sizeof(struct http_composed_client_type));
    SOL_NULL_CHECK(type, NULL);

    r = http_composed_client_type_init(type, contents);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    type->base.ports_in_count = type->ports_in.len;
    type->base.ports_out_count = type->ports_out.len;

    return &type->base;

err_exit:
    http_composed_client_type_fini(type);
    free(type);
    return NULL;
}

static int
http_composed_client_create_type(
    const struct sol_flow_metatype_context *ctx,
    struct sol_flow_node_type **type)
{
    struct sol_flow_node_type *result;
    int err;

    result = http_composed_client_new_type(ctx->contents);
    if (!result)
        return -EINVAL;

    err = ctx->store_type(ctx, result);
    if (err < 0) {
        sol_flow_node_type_del(result);
        return -err;
    }

    *type = result;

    return 0;
}

static void
metatype_port_description_clear(struct sol_vector *port_descriptions)
{
    uint16_t i;
    struct sol_flow_metatype_port_description *port;

    SOL_VECTOR_FOREACH_IDX (port_descriptions, port, i) {
        free(port->name);
        free(port->type);
    }
    sol_vector_clear(port_descriptions);
}

static int
setup_ports_description(const struct sol_str_slice *contents,
    struct sol_vector *in, struct sol_vector *out, struct sol_buffer *buf_out,
    const struct sol_str_slice *prefix)
{
    int r;
    uint16_t i;
    struct sol_buffer buffer, composed_type;
    struct sol_vector tokens;
    struct sol_str_slice *token;
    struct sol_flow_metatype_port_description *port;

    sol_vector_init(out,
        sizeof(struct sol_flow_metatype_port_description));
    sol_vector_init(in,
        sizeof(struct sol_flow_metatype_port_description));

    r = get_context_tokens(contents, &buffer, &tokens);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    sol_buffer_init(&composed_type);
    r = sol_buffer_append_slice(&composed_type, sol_str_slice_from_str("composed:"));
    SOL_INT_CHECK_GOTO(r, < 0, err);

    SOL_VECTOR_FOREACH_IDX (&tokens, token, i) {
        struct sol_str_slice type_slice;
        char *name = NULL;

        r = get_name_and_type_from_token(token, &name, &type_slice);
        SOL_INT_CHECK_GOTO(r, < 0, err_tokens);

        r = -ENOMEM;

        port = sol_vector_append(in);
        if (!port) {
            free(name);
            goto err_tokens;
        }

        port->name = name;
        port->type = strndup(type_slice.data, type_slice.len);
        SOL_NULL_CHECK_GOTO(port->type, err_tokens);

        port->idx = i;

        if (port->idx) {
            r = sol_buffer_append_char(&composed_type, ',');
            SOL_INT_CHECK_GOTO(r, < 0, err_tokens);
        }

        r = sol_buffer_append_slice(&composed_type, type_slice);
        SOL_INT_CHECK_GOTO(r, < 0, err_tokens);

        if (buf_out) {
            r = sol_buffer_append_printf(buf_out,
                "static struct http_composed_client_port_in http_composed_client_%.*s_%s_port = {\n"
                "    SOL_SET_API_VERSION(.base.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION, )\n"
                "    .base.connect = NULL,\n"
                "    .base.disconnect = NULL,\n"
                "    .base.process = http_composed_client_simple_process,\n"
                "    .name = \"%s\"\n"
                "};\n", SOL_STR_SLICE_PRINT(*prefix), port->name, port->name);
            SOL_INT_CHECK_GOTO(r, < 0, err_tokens);
        }
    }

    sol_vector_clear(&tokens);
    sol_buffer_fini(&buffer);

    r = sol_buffer_ensure_nul_byte(&composed_type);
    SOL_INT_CHECK_GOTO(r, < 0, err);

#define SETUP_PORT(_type, _name, _lowername) \
    do { \
        port = sol_vector_append(in); \
        SOL_NULL_CHECK_GOTO(port, err); \
        port->type = strdup(# _type); \
        SOL_NULL_CHECK_GOTO(port->type, err); \
        port->name = strdup(INPUT_ ## _name ## _PORT_NAME); \
        SOL_NULL_CHECK_GOTO(port->name, err); \
        port->idx = in->len - 1; \
        if (buf_out) { \
            r = sol_buffer_append_printf(buf_out, \
                "static struct http_composed_client_port_in http_composed_client_%.*s_%s_port = {\n" \
                "    SOL_SET_API_VERSION(.base.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION, )\n" \
                "    .base.connect = NULL,\n" \
                "    .base.disconnect = NULL,\n" \
                "    .base.process = http_composed_client_" # _lowername "_process,\n" \
                "    .name = \"%s\"\n" \
                "};\n", SOL_STR_SLICE_PRINT(*prefix), port->name, port->name); \
            SOL_INT_CHECK_GOTO(r, < 0, err_tokens); \
        } \
    } while (0)

    SETUP_PORT(any, GET, get);
    SETUP_PORT(any, POST, post);
    SETUP_PORT(string, URL, url);
#undef SETUP_PORT

    r = -ENOMEM;

    port = sol_vector_append(out);
    SOL_NULL_CHECK_GOTO(port, err);
    port->type = strdup(composed_type.data);
    SOL_NULL_CHECK_GOTO(port->type, err);
    port->name = strdup(OUTPUT_PORT_NAME);
    SOL_NULL_CHECK_GOTO(port->name, err);

    if (buf_out) {
        r = sol_buffer_append_printf(buf_out,
            "static struct http_composed_client_port_out http_composed_client_%.*s_%s_port = {\n"
            "    SOL_SET_API_VERSION(.base.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION, )\n"
            "    .base.connect = NULL,\n"
            "    .base.disconnect = NULL,\n"
            "    .name = \"%s\"\n"
            "};\n", SOL_STR_SLICE_PRINT(*prefix), port->name, port->name);
        SOL_INT_CHECK_GOTO(r, < 0, err_tokens);
    }

    port = sol_vector_append(in);
    SOL_NULL_CHECK_GOTO(port, err);
    port->name = strdup(INPUT_PORT_NAME);
    SOL_NULL_CHECK_GOTO(port->name, err);
    port->type = sol_buffer_steal(&composed_type, NULL);

    if (buf_out) {
        r = sol_buffer_append_printf(buf_out,
            "static struct http_composed_client_port_in http_composed_client_%.*s_%s_port = {\n"
            "    SOL_SET_API_VERSION(.base.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION, )\n"
            "    .base.connect = NULL,\n"
            "    .base.disconnect = NULL,\n"
            "    .base.process = http_composed_client_in_process,\n"
            "    .name = \"%s\"\n"
            "};\n", SOL_STR_SLICE_PRINT(*prefix), port->name, port->name);
        SOL_INT_CHECK_GOTO(r, < 0, err_tokens);
    }

    sol_buffer_fini(&composed_type);

    return 0;

err_tokens:
    sol_vector_clear(&tokens);
    sol_buffer_fini(&buffer);
err:
    sol_buffer_fini(&composed_type);
    metatype_port_description_clear(in);
    metatype_port_description_clear(out);
    return r;
}

static int
http_composed_client_ports_description(const struct sol_flow_metatype_context *ctx,
    struct sol_vector *in, struct sol_vector *out)
{
    SOL_NULL_CHECK(ctx, -EINVAL);
    SOL_NULL_CHECK(out, -EINVAL);
    SOL_NULL_CHECK(in, -EINVAL);

    return setup_ports_description(&ctx->contents, in, out, NULL,
        &(struct sol_str_slice){.len = 0, .data = "" });
}

static int
setup_get_port_function(struct sol_buffer *out, struct sol_vector *ports,
    const struct sol_str_slice prefix, const char *port_type)
{
    int r;
    uint16_t i;
    struct sol_flow_metatype_port_description *port;

    r = sol_buffer_append_printf(out,
        "static const struct sol_flow_port_type_%s *\n"
        "http_composed_client_%.*s_get_%s_port(const struct sol_flow_node_type *type, uint16_t port)\n"
        "{\n", port_type, SOL_STR_SLICE_PRINT(prefix), port_type);
    SOL_INT_CHECK(r, < 0, r);

    SOL_VECTOR_FOREACH_IDX (ports, port, i) {
        r = sol_buffer_append_printf(out, "    if (port == %u)\n"
            "        return &http_composed_client_%.*s_%s_port.base;\n",
            i, SOL_STR_SLICE_PRINT(prefix), port->name);
        SOL_INT_CHECK(r, < 0, r);
    }

    return sol_buffer_append_slice(out, sol_str_slice_from_str("    return NULL;\n}\n"));
}

static int
setup_composed_packet(struct sol_buffer *out, const struct sol_str_slice prefix,
    const struct sol_str_slice types, const char *port_name)
{
    int r;
    struct sol_vector tokens;
    struct sol_str_slice *token;
    uint16_t i;

    r = sol_buffer_append_slice(out,
        sol_str_slice_from_str("        const struct sol_flow_packet_type *types[] = {"));
    SOL_INT_CHECK(r, < 0, r);

    tokens = sol_str_slice_split(types, ",", 0);

    SOL_VECTOR_FOREACH_IDX (&tokens, token, i) {
        r = sol_buffer_append_printf(out, "%s,",
            sol_flow_get_packet_type_name(*token));
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = sol_buffer_append_printf(out, "NULL};\n"
        "        http_composed_client_%.*s_%s_port.base.packet_type = sol_flow_packet_type_composed_new(types);\n",
        SOL_STR_SLICE_PRINT(prefix), port_name);

exit:
    sol_vector_clear(&tokens);
    return r;
}

static int
setup_packet_type(struct sol_buffer *out, struct sol_vector *ports,
    const struct sol_str_slice prefix)
{
    int r;
    uint16_t i;
    struct sol_flow_metatype_port_description *port;


    SOL_VECTOR_FOREACH_IDX (ports, port, i) {
        r = sol_buffer_append_printf(out,
            "    if (!http_composed_client_%.*s_%s_port.base.packet_type) {\n",
            SOL_STR_SLICE_PRINT(prefix), port->name);
        SOL_INT_CHECK(r, < 0, r);

        if (strstartswith(port->type, "composed:")) {
            struct sol_str_slice types;
            //Removing the composed: prefix
            types.data = port->type + 9;
            types.len = strlen(port->type) - 9;
            r = setup_composed_packet(out, prefix, types, port->name);
        } else {
            r = sol_buffer_append_printf(out,
                "        http_composed_client_%.*s_%s_port.base.packet_type = %s;\n",
                SOL_STR_SLICE_PRINT(prefix), port->name,
                sol_flow_get_packet_type_name(
                sol_str_slice_from_str(port->type)));
        }
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_slice(out, sol_str_slice_from_str("    }\n"));
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

static int
setup_init_function(struct sol_buffer *out, struct sol_vector *in_ports,
    struct sol_vector *out_ports, const struct sol_str_slice prefix)
{
    int r;

    r = sol_buffer_append_printf(out,
        "static void\nhttp_composed_client_%.*s_init(void)\n{\n",
        SOL_STR_SLICE_PRINT(prefix));
    SOL_INT_CHECK(r, < 0, r);

    r = setup_packet_type(out, in_ports, prefix);
    SOL_INT_CHECK(r, < 0, r);
    r = setup_packet_type(out, out_ports, prefix);
    SOL_INT_CHECK(r, < 0, r);

    return sol_buffer_append_slice(out, sol_str_slice_from_str("}\n"));
}

static int
http_composed_client_generate_body(const struct sol_flow_metatype_context *ctx,
    struct sol_buffer *out)
{
    struct sol_vector in_ports, out_ports;
    int r;

    r = setup_ports_description(&ctx->contents, &in_ports, &out_ports, out, &ctx->name);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = setup_get_port_function(out, &in_ports, ctx->name, "in");
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = setup_get_port_function(out, &out_ports, ctx->name, "out");
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = setup_init_function(out, &in_ports, &out_ports, ctx->name);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = sol_buffer_append_printf(out, "#define %.*s_OPTIONS_DEFAULTS(...) { \\\n"
        "    .base = { \\\n"
        "        SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_OPTIONS_API_VERSION, ) \\\n"
        "        SOL_SET_API_VERSION(.sub_api = %d, ) \\\n"
        "    }, \\\n"
        "    .url = NULL, \\\n"
        "    __VA_ARGS__ \\\n"
        "}\n\n"
        "static const struct http_composed_client_options %.*s_options_defaults = %.*s_OPTIONS_DEFAULTS();\n\n",
        SOL_STR_SLICE_PRINT(ctx->name),
        SOL_FLOW_NODE_TYPE_HTTP_COMPOSED_CLIENT_OPTIONS_API_VERSION,
        SOL_STR_SLICE_PRINT(ctx->name),
        SOL_STR_SLICE_PRINT(ctx->name));
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = sol_buffer_append_printf(out,
        "static const struct sol_flow_node_type %.*s = {\n"
        "   SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_API_VERSION, )\n"
        "   .options_size = sizeof(struct http_composed_client_options),\n"
        "   .data_size = sizeof(struct http_composed_client_data),\n"
        "   .ports_out_count = %u,\n"
        "   .ports_in_count = %u,\n"
        "   .dispose_type = NULL,\n"
        "   .open = http_composed_client_open,\n"
        "   .close = http_composed_client_close,\n"
        "   .default_options = &%.*s_options_defaults,\n"
        "   .get_port_out = http_composed_client_%.*s_get_out_port,\n"
        "   .get_port_in = http_composed_client_%.*s_get_in_port,\n"
        "   .init_type = http_composed_client_%.*s_init,\n"
        "};\n",
        SOL_STR_SLICE_PRINT(ctx->name),
        out_ports.len,
        in_ports.len,
        SOL_STR_SLICE_PRINT(ctx->name),
        SOL_STR_SLICE_PRINT(ctx->name),
        SOL_STR_SLICE_PRINT(ctx->name),
        SOL_STR_SLICE_PRINT(ctx->name));

exit:
    metatype_port_description_clear(&in_ports);
    metatype_port_description_clear(&out_ports);
    return r;
}

static int
http_composed_client_generate_start(const struct sol_flow_metatype_context *ctx,
    struct sol_buffer *out)
{
    return sol_buffer_append_slice(out,
        sol_str_slice_from_str(HTTP_COMPOSED_CLIENT_CODE_START));
}

static int
http_composed_client_generate_end(const struct sol_flow_metatype_context *ctx,
    struct sol_buffer *out)
{
    return 0;
}

static int
http_composed_client_options_description(struct sol_vector *options)
{
    struct sol_flow_metatype_option_description *option;

    sol_vector_init(options,
        sizeof(struct sol_flow_metatype_option_description));

    option = sol_vector_append(options);
    SOL_NULL_CHECK(option, -ENOMEM);

    option->name = strdup("url");
    SOL_NULL_CHECK_GOTO(option->name, err);

    option->data_type = strdup("string");
    SOL_NULL_CHECK_GOTO(option->data_type, err_data);

    return 0;

err_data:
    free(option->name);
err:
    sol_vector_del_last(options);
    return -ENOMEM;
}

SOL_FLOW_METATYPE(HTTP_COMPOSED_CLIENT,
    .name = "http-composed-client",
    .options_symbol = "http_composed_client_options",
    .create_type = http_composed_client_create_type,
    .generate_type_start = http_composed_client_generate_start,
    .generate_type_body = http_composed_client_generate_body,
    .generate_type_end = http_composed_client_generate_end,
    .ports_description = http_composed_client_ports_description,
    .options_description = http_composed_client_options_description,
    );
