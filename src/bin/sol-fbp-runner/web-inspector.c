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

#include <inttypes.h>
#include <stdio.h>
#include <sys/stat.h>

#include "sol-flow.h"
#include "sol-flow-inspector.h"
#include "sol-http-server.h"
#include "sol-json.h"
#include "sol-mainloop.h"
#include "sol-util-file.h"
#include "sol-util.h"
#include "sol-util-internal.h"

#include "web-inspector.h"

static struct sol_http_server *server;
static struct sol_http_progressive_response *events_response;

static struct timespec start;

static int
web_inspector_send_sse_data(struct sol_http_progressive_response *client, struct sol_buffer *buf)
{
    int r;
    struct sol_blob *blob;

    blob = sol_buffer_to_blob(buf);
    if (!blob)
        return -ENOMEM;

    r = sol_http_progressive_response_sse_feed(client, blob);
    sol_blob_unref(blob);
    return r;
}

static int
web_inspector_add_json_key_value(struct sol_buffer *buf, const char *k, const char *v)
{
    int r;

    r = sol_buffer_append_printf(buf, "\"%s\":", k);
    if (r < 0)
        return r;

    return sol_json_serialize_string(buf, v ? v : "");
}

static int
web_inspector_add_key_value_array(struct sol_buffer *buf, const struct sol_vector *vector)
{
    uint16_t i;
    const struct sol_key_value *kv;
    int r;

    SOL_VECTOR_FOREACH_IDX (vector, kv, i) {
        if (i > 0) {
            r = sol_buffer_append_char(buf, ',');
            if (r < 0)
                return r;
        }

        r = sol_buffer_append_char(buf, '[');
        if (r < 0)
            return r;

        r = sol_json_serialize_string(buf, kv->key);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, ',');
        if (r < 0)
            return r;

        r = sol_json_serialize_string(buf, kv->value);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, ']');
        if (r < 0)
            return r;
    }

    return 0;
}

static int
web_inspector_open_event(struct sol_buffer *buf, const char *event)
{
    struct timespec now = sol_util_timespec_get_current();
    struct timespec diff;

    sol_util_timespec_sub(&now, &start, &diff);

    return sol_buffer_append_printf(buf,
        "{\"event\": \"%s\",\"timestamp\":%ld.%010ld,\"payload\":", event, diff.tv_sec, diff.tv_nsec);
}

static int
web_inspector_close_event(struct sol_buffer *buf)
{
    return sol_buffer_append_slice(buf, sol_str_slice_from_str("}"));
}

static const char *
web_inspector_get_node_typename(const struct sol_flow_node *node)
{
    const struct sol_flow_node_type *type = sol_flow_node_get_type(node);

    if (!type)
        return NULL;
    return type->description ? type->description->name : NULL;
}

static int
web_inspector_add_port_descriptions(struct sol_buffer *buf, const struct sol_flow_port_description *const *descs)
{
    int r;
    bool first = true;

    r = sol_buffer_append_char(buf, '[');
    if (r < 0)
        return r;

    if (!descs)
        goto end;

    for (; *descs; descs++) {
        const struct sol_flow_port_description *desc = *descs;

        if (first)
            first = false;
        else {
            r = sol_buffer_append_char(buf, ',');
            if (r < 0)
                return r;
        }

        r = sol_buffer_append_char(buf, '{');
        if (r < 0)
            return r;

        r = web_inspector_add_json_key_value(buf, "name", desc->name);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, ',');
        if (r < 0)
            return r;

        r = web_inspector_add_json_key_value(buf, "description", desc->description);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, ',');
        if (r < 0)
            return r;

        r = web_inspector_add_json_key_value(buf, "data_type", desc->data_type);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"array_size\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint32(buf, desc->array_size);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"base_port_idx\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint32(buf, desc->base_port_idx);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"required\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_bool(buf, desc->required);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '}');
        if (r < 0)
            return r;
    }

end:
    r = sol_buffer_append_char(buf, ']');
    if (r < 0)
        return r;

    return 0;
}

static int
web_inspector_add_port_without_description(struct sol_buffer *buf, uint16_t idx, const struct sol_flow_packet_type *packet_type)
{
    int r;

    r = sol_buffer_append_printf(buf, "{\"name\", \"%" PRIu16
        "\",\"description\":\"\",array_size\":0,base_port_idx:%" PRIu16 ",required:false,\"data_type\":",
        idx, idx);
    if (r < 0)
        return r;

    r = sol_json_serialize_string(buf, packet_type ? packet_type->name : "any");
    if (r < 0)
        return r;

    return sol_buffer_append_char(buf, '}');
}

static int
web_inspector_add_port_in_without_descriptions(struct sol_buffer *buf, const struct sol_flow_node_type *type)
{
    unsigned int i;
    int r;

    r = sol_buffer_append_char(buf, '[');
    if (r < 0)
        return r;

    for (i = 0; i < type->ports_in_count; i++) {
        const struct sol_flow_port_type_in *port;

        if (i > 0) {
            r = sol_buffer_append_char(buf, ',');
            if (r < 0)
                return r;
        }

        port = sol_flow_node_type_get_port_in(type, i);
        if (!port)
            return -ENOENT;

        r = web_inspector_add_port_without_description(buf, i, port->packet_type);
        if (r < 0)
            return r;
    }

    r = sol_buffer_append_char(buf, ']');
    if (r < 0)
        return r;

    return 0;
}

static int
web_inspector_add_port_out_without_descriptions(struct sol_buffer *buf, const struct sol_flow_node_type *type)
{
    int r;

    r = sol_buffer_append_char(buf, ']');
    if (r < 0)
        return r;

    return 0;
}

static int
web_inspector_add_option_value(struct sol_buffer *buf, const char *data_type, const void *mem)
{
    int r;

    if (streq(data_type, "string")) {
        const char *const *s = mem;

        if (*s)
            r = sol_json_serialize_string(buf, *s);
        else
            r = sol_json_serialize_null(buf);
    } else if (streq(data_type, "boolean")) {
        const bool *b = mem;

        r = sol_json_serialize_bool(buf, *b);
    } else if (streq(data_type, "byte")) {
        const uint8_t *b = mem;

        r = sol_json_serialize_uint32(buf, *b);
    } else if (streq(data_type, "int")) {
        const int32_t *i = mem;

        r = sol_json_serialize_int32(buf, *i);
    } else if (streq(data_type, "float")) {
        const double *d = mem;

        r = sol_json_serialize_double(buf, *d);
    } else if (streq(data_type, "irange-spec")) {
        const struct sol_irange_spec *i = mem;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("{\"min\":"));
        if (r < 0)
            return r;
        r = sol_json_serialize_int32(buf, i->min);
        if (r < 0)
            return r;
        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"max\":"));
        if (r < 0)
            return r;
        r = sol_json_serialize_int32(buf, i->max);
        if (r < 0)
            return r;
        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"step\":"));
        if (r < 0)
            return r;
        r = sol_json_serialize_int32(buf, i->step);
        if (r < 0)
            return r;
        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("}"));
    } else if (streq(data_type, "drange-spec")) {
        const struct sol_drange_spec *d = mem;


        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("{\"min\":"));
        if (r < 0)
            return r;
        r = sol_json_serialize_double(buf, d->min);
        if (r < 0)
            return r;
        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"max\":"));
        if (r < 0)
            return r;
        r = sol_json_serialize_double(buf, d->max);
        if (r < 0)
            return r;
        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"step\":"));
        if (r < 0)
            return r;
        r = sol_json_serialize_double(buf, d->step);
        if (r < 0)
            return r;
        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("}"));
    } else {
        r = sol_json_serialize_null(buf);
    }

    return r;
}

static void
web_inspector_did_open_node(const struct sol_flow_inspector *inspector, const struct sol_flow_node *node, const struct sol_flow_node_options *options)
{
    const char *typename = web_inspector_get_node_typename(node);
    const struct sol_flow_node_type *type = sol_flow_node_get_type(node);
    const struct sol_flow_node_type_description *desc;
    const struct sol_flow_node_options_description *opt_desc;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    const struct sol_flow_node *n;
    struct sol_ptr_vector v = SOL_PTR_VECTOR_INIT;
    uint16_t idx;
    int r;

    if (!events_response)
        return;

    if (!type)
        return;

    desc = type->description;
    opt_desc = desc ? desc->options : NULL;

    r = web_inspector_open_event(&buf, "open");
    if (r < 0)
        goto end;

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str("{\"path\":["));
    if (r < 0)
        goto end;

    n = node;
    while (n) {
        r = sol_ptr_vector_append(&v, n);
        if (r < 0)
            goto end;
        n = sol_flow_node_get_parent(n);
    }

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&v, n, idx) {
        r = sol_json_serialize_uint64(&buf, (uintptr_t)n);
        if (r < 0)
            goto end;
        if (idx > 0) {
            r = sol_buffer_append_char(&buf, ',');
            if (r < 0)
                goto end;
        }
    }

    r = sol_buffer_append_char(&buf, ']');
    if (r < 0)
        goto end;

    if (sol_flow_node_get_id(node)) {
        r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"id\":"));
        if (r < 0)
            goto end;

        r = sol_json_serialize_string(&buf, sol_flow_node_get_id(node));
        if (r < 0)
            goto end;
    }

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"ports_in\":"));
    if (r < 0)
        goto end;

    if (type->description)
        r = web_inspector_add_port_descriptions(&buf, type->description->ports_in);
    else
        r = web_inspector_add_port_in_without_descriptions(&buf, type);
    if (r < 0)
        goto end;

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"ports_out\":"));
    if (r < 0)
        goto end;

    if (type->description)
        r = web_inspector_add_port_descriptions(&buf, type->description->ports_out);
    else
        r = web_inspector_add_port_out_without_descriptions(&buf, type);
    if (r < 0)
        goto end;

    if (type->description) {
        r = sol_buffer_append_char(&buf, ',');
        if (r < 0)
            goto end;

        r = web_inspector_add_json_key_value(&buf, "type", typename);
        if (r < 0)
            goto end;

        r = sol_buffer_append_char(&buf, ',');
        if (r < 0)
            goto end;

        r = web_inspector_add_json_key_value(&buf, "category", type->description->category);
        if (r < 0)
            goto end;

        r = sol_buffer_append_char(&buf, ',');
        if (r < 0)
            goto end;

        r = web_inspector_add_json_key_value(&buf, "description", type->description->description);
        if (r < 0)
            goto end;

        r = sol_buffer_append_char(&buf, ',');
        if (r < 0)
            goto end;

        r = web_inspector_add_json_key_value(&buf, "author", type->description->author);
        if (r < 0)
            goto end;

        r = sol_buffer_append_char(&buf, ',');
        if (r < 0)
            goto end;

        r = web_inspector_add_json_key_value(&buf, "url", type->description->url);
        if (r < 0)
            goto end;

        r = sol_buffer_append_char(&buf, ',');
        if (r < 0)
            goto end;

        r = web_inspector_add_json_key_value(&buf, "license", type->description->license);
        if (r < 0)
            goto end;

        r = sol_buffer_append_char(&buf, ',');
        if (r < 0)
            goto end;

        r = web_inspector_add_json_key_value(&buf, "version", type->description->version);
        if (r < 0)
            goto end;
    }

    if (opt_desc && opt_desc->members) {
        const struct sol_flow_node_options_member_description *itr;

        r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"options\":["));
        if (r < 0)
            goto end;

        for (itr = opt_desc->members; itr->name != NULL; itr++) {
            const void *mem = (const uint8_t *)options + itr->offset;

            if (itr > opt_desc->members) {
                r = sol_buffer_append_char(&buf, ',');
                if (r < 0)
                    goto end;
            }

            r = sol_buffer_append_char(&buf, '{');
            if (r < 0)
                goto end;

            r = web_inspector_add_json_key_value(&buf, "name", itr->name);
            if (r < 0)
                goto end;

            r = sol_buffer_append_char(&buf, ',');
            if (r < 0)
                goto end;

            r = web_inspector_add_json_key_value(&buf, "description", itr->description);
            if (r < 0)
                goto end;

            r = sol_buffer_append_char(&buf, ',');
            if (r < 0)
                goto end;

            r = web_inspector_add_json_key_value(&buf, "data_type", itr->data_type);
            if (r < 0)
                goto end;

            r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"required\":"));
            if (r < 0)
                goto end;

            r = sol_json_serialize_bool(&buf, itr->required);
            if (r < 0)
                goto end;

            r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"value\":"));
            if (r < 0)
                goto end;

            r = web_inspector_add_option_value(&buf, itr->data_type, mem);
            if (r < 0)
                goto end;

            r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"defvalue\":"));
            if (r < 0)
                goto end;

            r = web_inspector_add_option_value(&buf, itr->data_type, &itr->defvalue);
            if (r < 0)
                goto end;

            r = sol_buffer_append_char(&buf, '}');
            if (r < 0)
                goto end;
        }

        r = sol_buffer_append_char(&buf, ']');
        if (r < 0)
            goto end;
    }

    r = sol_buffer_append_char(&buf, '}');
    if (r < 0)
        goto end;

    r = web_inspector_close_event(&buf);
    if (r < 0)
        goto end;

    r = web_inspector_send_sse_data(events_response, &buf);
    if (r < 0)
        goto end;

end:
    if (r < 0) {
        fprintf(stderr, "Error: could not feed data to inspector: %s. Data: '%.*s'\n",
            sol_util_strerrora(-r), SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&buf)));
    }
    sol_ptr_vector_clear(&v);
    sol_buffer_fini(&buf);
}

static void
web_inspector_will_close_node(const struct sol_flow_inspector *inspector, const struct sol_flow_node *node)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    int r;

    if (!events_response)
        return;

    r = web_inspector_open_event(&buf, "close");
    if (r < 0)
        goto end;

    r = sol_json_serialize_uint64(&buf, (uintptr_t)node);
    if (r < 0)
        goto end;

    r = web_inspector_close_event(&buf);
    if (r < 0)
        goto end;

    r = web_inspector_send_sse_data(events_response, &buf);
    if (r < 0)
        goto end;

end:
    if (r < 0) {
        fprintf(stderr, "Error: could not feed data to inspector: %s. Data: '%.*s'\n",
            sol_util_strerrora(-r), SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&buf)));
    }

    sol_buffer_fini(&buf);
}

static int
web_inspector_add_node_port_conn_id(struct sol_buffer *buf, const struct sol_flow_node *node, uint16_t port, uint16_t conn_id, const struct sol_flow_port_description *port_desc)
{
    int r;

    r = sol_buffer_append_slice(buf, sol_str_slice_from_str("{\"node\":"));
    if (r < 0)
        return r;

    r = sol_json_serialize_uint64(buf, (uintptr_t)node);
    if (r < 0)
        return r;

    r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"port_idx\":"));
    if (r < 0)
        return r;

    r = sol_json_serialize_uint32(buf, port);
    if (r < 0)
        return r;

    if (port_desc && port_desc->name) {
        r = sol_buffer_append_char(buf, ',');
        if (r < 0)
            return r;

        r = web_inspector_add_json_key_value(buf, "port_name", port_desc->name);
        if (r < 0)
            return r;
    }

    r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"conn_id\":"));
    if (r < 0)
        return r;

    r = sol_json_serialize_uint32(buf, conn_id);
    if (r < 0)
        return r;

    return sol_buffer_append_char(buf, '}');
}

static void
web_inspector_did_connect_port(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, uint16_t src_conn_id, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id)
{
    const struct sol_flow_port_description *port_desc;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    int r;

    if (!events_response)
        return;

    r = web_inspector_open_event(&buf, "connect");
    if (r < 0)
        goto end;

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str("{\"src\":"));
    if (r < 0)
        goto end;

    port_desc = sol_flow_node_get_description_port_out(sol_flow_node_get_type(src_node), src_port);
    r = web_inspector_add_node_port_conn_id(&buf, src_node, src_port, src_conn_id, port_desc);
    if (r < 0)
        goto end;

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"dst\":"));
    if (r < 0)
        goto end;

    port_desc = sol_flow_node_get_description_port_in(sol_flow_node_get_type(dst_node), dst_port);
    r = web_inspector_add_node_port_conn_id(&buf, dst_node, dst_port, dst_conn_id, port_desc);
    if (r < 0)
        goto end;

    r = sol_buffer_append_char(&buf, '}');
    if (r < 0)
        goto end;

    r = web_inspector_close_event(&buf);
    if (r < 0)
        goto end;

    r = web_inspector_send_sse_data(events_response, &buf);
    if (r < 0)
        goto end;

end:
    if (r < 0) {
        fprintf(stderr, "Error: could not feed data to inspector: %s. Data: '%.*s'\n",
            sol_util_strerrora(-r), SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&buf)));
    }

    sol_buffer_fini(&buf);
}

static void
web_inspector_will_disconnect_port(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, uint16_t src_conn_id, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id)
{
    const struct sol_flow_port_description *port_desc;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    int r;

    if (!events_response)
        return;

    r = web_inspector_open_event(&buf, "disconnect");
    if (r < 0)
        goto end;

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str("{\"src\":"));
    if (r < 0)
        goto end;

    port_desc = sol_flow_node_get_description_port_out(sol_flow_node_get_type(src_node), src_port);
    r = web_inspector_add_node_port_conn_id(&buf, src_node, src_port, src_conn_id, port_desc);
    if (r < 0)
        goto end;

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"dst\":"));
    if (r < 0)
        goto end;

    port_desc = sol_flow_node_get_description_port_in(sol_flow_node_get_type(dst_node), dst_port);
    r = web_inspector_add_node_port_conn_id(&buf, dst_node, dst_port, dst_conn_id, port_desc);
    if (r < 0)
        goto end;

    r = sol_buffer_append_char(&buf, '}');
    if (r < 0)
        goto end;

    r = web_inspector_close_event(&buf);
    if (r < 0)
        goto end;

    r = web_inspector_send_sse_data(events_response, &buf);
    if (r < 0)
        goto end;

end:
    if (r < 0) {
        fprintf(stderr, "Error: could not feed data to inspector: %s. Data: '%.*s'\n",
            sol_util_strerrora(-r), SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&buf)));
    }

    sol_buffer_fini(&buf);
}

static int
web_inspector_add_packet_simple(struct sol_buffer *buf, const struct sol_flow_packet *packet)
{
    const struct sol_flow_packet_type *type = sol_flow_packet_get_type(packet);
    int r;

    r = sol_buffer_append_slice(buf, sol_str_slice_from_str("{\"packet_type\":"));
    if (r < 0)
        return r;

    r = sol_json_serialize_string(buf, type->name ? type->name : "unknown");
    if (r < 0)
        return r;

    r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"payload\":"));
    if (r < 0)
        return r;

    if (type == SOL_FLOW_PACKET_TYPE_EMPTY || type == SOL_FLOW_PACKET_TYPE_ANY) {
        r = sol_json_serialize_null(buf);
    } else if (type == SOL_FLOW_PACKET_TYPE_ERROR) {
        int code;
        const char *msg;

        r = sol_flow_packet_get_error(packet, &code, &msg);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '{');
        if (r < 0)
            return r;

        r = sol_buffer_append_printf(buf, "\"code\":%d,\"message\":", code);
        if (r < 0)
            return r;

        r = sol_json_serialize_string(buf, msg ? msg : "");
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '}');
    } else if (type == SOL_FLOW_PACKET_TYPE_BOOL) {
        bool v;

        r = sol_flow_packet_get_bool(packet, &v);
        if (r < 0)
            return r;

        r = sol_json_serialize_bool(buf, v);
    } else if (type == SOL_FLOW_PACKET_TYPE_BYTE) {
        unsigned char v;

        r = sol_flow_packet_get_byte(packet, &v);
        if (r < 0)
            return r;

        r = sol_json_serialize_uint32(buf, v);
    } else if (type == SOL_FLOW_PACKET_TYPE_IRANGE) {
        struct sol_irange v;

        r = sol_flow_packet_get_irange(packet, &v);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '{');
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("\"value\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_int32(buf, v.val);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"min\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_int32(buf, v.min);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"max\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_int32(buf, v.max);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"step\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_int32(buf, v.step);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '}');
    } else if (type == SOL_FLOW_PACKET_TYPE_DRANGE) {
        struct sol_drange v;

        r = sol_flow_packet_get_drange(packet, &v);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '{');
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("\"value\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.val);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"min\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.min);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"max\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.max);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"step\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.step);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '}');
    } else if (type == SOL_FLOW_PACKET_TYPE_STRING) {
        const char *v;

        r = sol_flow_packet_get_string(packet, &v);
        if (r < 0)
            return r;

        if (v)
            r = sol_json_serialize_string(buf, v);
        else
            r = sol_json_serialize_null(buf);
    } else if (type == SOL_FLOW_PACKET_TYPE_BLOB) {
        struct sol_blob *v;

        r = sol_flow_packet_get_blob(packet, &v);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '{');
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("\"mem\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint64(buf, (uintptr_t)v->mem);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"size\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint64(buf, v->size);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"refcnt\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint32(buf, v->refcnt);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"type\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint64(buf, (uintptr_t)v->type);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"parent\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint64(buf, (uintptr_t)v->parent);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '}');
    } else if (type == SOL_FLOW_PACKET_TYPE_JSON_OBJECT) {
        struct sol_blob *v;

        r = sol_flow_packet_get_json_object(packet, &v);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_blob(v));
    } else if (type == SOL_FLOW_PACKET_TYPE_JSON_ARRAY) {
        struct sol_blob *v;

        r = sol_flow_packet_get_json_array(packet, &v);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_blob(v));
    } else if (type == SOL_FLOW_PACKET_TYPE_RGB) {
        struct sol_rgb v;

        r = sol_flow_packet_get_rgb(packet, &v);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '{');
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("\"red\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint32(buf, v.red);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"blue\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint32(buf, v.blue);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"green\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint32(buf, v.green);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"red_max\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint32(buf, v.red_max);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"blue_max\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint32(buf, v.blue_max);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"green_max\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint32(buf, v.green_max);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '}');
    } else if (type == SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR) {
        struct sol_direction_vector v;

        r = sol_flow_packet_get_direction_vector(packet, &v);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '{');
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("\"x\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.x);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"y\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.y);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"z\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.z);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"min\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.min);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"max\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.max);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '}');
    } else if (type == SOL_FLOW_PACKET_TYPE_LOCATION) {
        struct sol_location v;

        r = sol_flow_packet_get_location(packet, &v);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '{');
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("\"lat\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.lat);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"lon\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.lon);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"alt\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_double(buf, v.alt);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '}');
    } else if (type == SOL_FLOW_PACKET_TYPE_TIMESTAMP) {
        struct timespec v;
        struct tm cur_time;

        r = sol_flow_packet_get_timestamp(packet, &v);
        if (r < 0)
            return r;

        tzset();
        if (!gmtime_r(&v.tv_sec, &cur_time))
            return -EINVAL;

        r = sol_buffer_expand(buf, 34);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '"');
        if (r < 0)
            return r;

        r = strftime(sol_buffer_at_end(buf), 32, "%Y-%m-%dT%H:%M:%SZ", &cur_time);
        if (r < 0)
            return -EINVAL;

        buf->used += r;

        r = sol_buffer_append_char(buf, '"');
    } else if (type == SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE) {
        int code;
        const char *url, *content_type;
        const struct sol_blob *content;
        struct sol_vector headers, cookies;

        r = sol_flow_packet_get_http_response(packet, &code, &url,
            &content_type, &content, &cookies, &headers);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '{');
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str("\"response_code\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint32(buf, code);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, ',');
        if (r < 0)
            return r;

        r = web_inspector_add_json_key_value(buf, "url", url);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, ',');
        if (r < 0)
            return r;

        r = web_inspector_add_json_key_value(buf, "content_type", content_type);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"content_length\":"));
        if (r < 0)
            return r;

        r = sol_json_serialize_uint64(buf, content->size);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"cookies\":"));
        if (r < 0)
            return r;

        r = web_inspector_add_key_value_array(buf, &cookies);
        if (r < 0)
            return r;

        r = sol_buffer_append_slice(buf, sol_str_slice_from_str(",\"headers\":"));
        if (r < 0)
            return r;

        r = web_inspector_add_key_value_array(buf, &headers);
        if (r < 0)
            return r;
        r = sol_buffer_append_char(buf, '"');
    } else {
        r = sol_json_serialize_uint64(buf, (uintptr_t)type);
    }

    if (r < 0)
        return r;

    return sol_buffer_append_char(buf, '}');
}

static int
web_inspector_add_packet(struct sol_buffer *buf, const struct sol_flow_packet *packet)
{
    const struct sol_flow_packet_type *type = sol_flow_packet_get_type(packet);
    int r;

    if (sol_flow_packet_is_composed_type(type)) {
        uint16_t len, i;
        struct sol_flow_packet **packets;

        r = sol_flow_packet_get_composed_members(packet, &packets, &len);
        if (r < 0)
            return r;

        r = sol_buffer_append_char(buf, '[');
        for (i = 0; i < len; i++) {
            r = web_inspector_add_packet_simple(buf, packets[i]);
            if (r < 0)
                return r;
        }
        return sol_buffer_append_char(buf, ']');
    } else
        return web_inspector_add_packet_simple(buf, packet);
}

static void
web_inspector_will_send_packet(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, const struct sol_flow_packet *packet)
{
    const struct sol_flow_port_description *port_desc;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    int r;

    if (!events_response)
        return;

    r = web_inspector_open_event(&buf, "send");
    if (r < 0)
        goto end;

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str("{\"node\":"));
    if (r < 0)
        goto end;

    r = sol_json_serialize_uint64(&buf, (uintptr_t)src_node);
    if (r < 0)
        goto end;

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"port_idx\":"));
    if (r < 0)
        goto end;

    r = sol_json_serialize_uint32(&buf, src_port);
    if (r < 0)
        goto end;

    port_desc = sol_flow_node_get_description_port_out(sol_flow_node_get_type(src_node), src_port);
    if (port_desc && port_desc->name) {
        r = sol_buffer_append_char(&buf, ',');
        if (r < 0)
            goto end;

        r = web_inspector_add_json_key_value(&buf, "port_name", port_desc->name);
        if (r < 0)
            goto end;
    }

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"packet\":"));
    if (r < 0)
        goto end;

    r = web_inspector_add_packet(&buf, packet);
    if (r < 0)
        goto end;

    r = sol_buffer_append_char(&buf, '}');
    if (r < 0)
        goto end;

    r = web_inspector_close_event(&buf);
    if (r < 0)
        goto end;

    r = web_inspector_send_sse_data(events_response, &buf);
    if (r < 0)
        goto end;

end:
    if (r < 0) {
        fprintf(stderr, "Error: could not feed data to inspector: %s. Data: '%.*s'\n",
            sol_util_strerrora(-r), SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&buf)));
    }

    sol_buffer_fini(&buf);
}

static void
web_inspector_will_deliver_packet(const struct sol_flow_inspector *web_inspector, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id, const struct sol_flow_packet *packet)
{
    const struct sol_flow_port_description *port_desc;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    int r;

    if (!events_response)
        return;

    r = web_inspector_open_event(&buf, "deliver");
    if (r < 0)
        goto end;

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str("{\"node\":"));
    if (r < 0)
        goto end;

    r = sol_json_serialize_uint64(&buf, (uintptr_t)dst_node);
    if (r < 0)
        goto end;

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"port_idx\":"));
    if (r < 0)
        goto end;

    r = sol_json_serialize_uint32(&buf, dst_port);
    if (r < 0)
        goto end;

    port_desc = sol_flow_node_get_description_port_in(sol_flow_node_get_type(dst_node), dst_port);
    if (port_desc && port_desc->name) {
        r = sol_buffer_append_char(&buf, ',');
        if (r < 0)
            goto end;

        r = web_inspector_add_json_key_value(&buf, "port_name", port_desc->name);
        if (r < 0)
            goto end;
    }

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(",\"packet\":"));
    if (r < 0)
        goto end;

    r = web_inspector_add_packet(&buf, packet);
    if (r < 0)
        goto end;

    r = sol_buffer_append_char(&buf, '}');
    if (r < 0)
        goto end;

    r = web_inspector_close_event(&buf);
    if (r < 0)
        goto end;

    r = web_inspector_send_sse_data(events_response, &buf);
    if (r < 0)
        goto end;

end:
    if (r < 0) {
        fprintf(stderr, "Error: could not feed data to inspector: %s. Data: '%.*s'\n",
            sol_util_strerrora(-r), SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&buf)));
    }

    sol_buffer_fini(&buf);
}

static const struct sol_flow_inspector web_inspector = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_INSPECTOR_API_VERSION, )
    .did_open_node = web_inspector_did_open_node,
    .will_close_node = web_inspector_will_close_node,
    .did_connect_port = web_inspector_did_connect_port,
    .will_disconnect_port = web_inspector_will_disconnect_port,
    .will_send_packet = web_inspector_will_send_packet,
    .will_deliver_packet = web_inspector_will_deliver_packet,
};

static int
url_from_addr(struct sol_buffer *url_buf, const struct sol_network_link_addr *addr, uint16_t port, const char *path)
{
    struct sol_http_url url = {
        .scheme = SOL_STR_SLICE_LITERAL("http"),
        .port = port,
        .path = sol_str_slice_from_str(path),
    };
    int r;

    SOL_BUFFER_DECLARE_STATIC(addr_buf, SOL_NETWORK_INET_ADDR_STR_LEN);

    if (!sol_network_link_addr_to_str(addr, &addr_buf))
        return -EINVAL;

    url.host = sol_buffer_get_slice(&addr_buf);

    r = sol_http_create_full_uri(url_buf, url, NULL);

    sol_buffer_fini(&addr_buf);

    return r;
}

static void
on_events_closed(void *data, const struct sol_http_progressive_response *h)
{
    events_response = NULL;
    sol_quit();
}

static int
on_events(void *data, struct sol_http_request *request)
{
    struct runner *the_runner = data;
    struct sol_http_response response = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
        .param = SOL_HTTP_REQUEST_PARAMS_INIT,
        .response_code = SOL_HTTP_STATUS_OK,
        .content = SOL_BUFFER_INIT_EMPTY
    };
    struct sol_http_server_progressive_config config = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_SERVER_PROGRESSIVE_CONFIG_API_VERSION, )
        .on_close = on_events_closed
    };
    int r;

    if (events_response) {
        const char busy_text[] = "The resource is exclusive and already being served to another host.";
        response.response_code = SOL_HTTP_STATUS_FORBIDDEN;
        response.content = SOL_BUFFER_INIT_CONST((char *)busy_text, sizeof(busy_text) - 1);
        response.content_type = "text/plain";
        return sol_http_server_send_response(request, &response);
    }

    r = sol_http_response_set_sse_headers(&response);
    if (r < 0) {
        fputs("Error: Cannot set HTTP headers for server-side-events.\n", stderr);
        return r;
    }

    events_response = sol_http_server_send_progressive_response(request, &response, &config);
    sol_http_params_clear(&response.param);
    if (!events_response)
        return -1;

    start = sol_util_timespec_get_current();
    sol_flow_set_inspector(&web_inspector);

    r = runner_run(the_runner);
    if (r < 0) {
        fputs("Error: Failed to run flow\n", stderr);
        sol_http_progressive_response_del(events_response, true);
        return r;
    }

    return 0;
}

static int
on_fallback_index(void *data, struct sol_http_request *request)
{
    static const char fallback_html[] = ""
        "<!DOCTYPE html>\n"
        "<html>"
        "<body>"
        "<h1>FBP Inspector</h1>"
        "<p style=\"color: #999; text-align: center; \">Note: This is a fallback version since static resources weren't found</p>"
        "<pre id=\"log\" style=\"border: 1px solid black; font-size: small\"></pre>"
        "<script>\n"
        "if (!!window.EventSource) {\n"
        "  var logElm = document.getElementById('log');\n"
        "  var source = new EventSource(window.location.origin + '/events');\n"
        "  source.onopen = function(e) {\n"
        "    logElm.textContent = '';\n"
        "  };\n"
        "  source.onmessage = function(e) {\n"
        "    var ev = JSON.parse(e.data);\n"
        "    logElm.textContent += JSON.stringify(ev, null, '\\t') + '\\n';\n"
        "  };\n"
        "  source.onerror = function(e) {\n"
        "    if (e.readyState == EventSource.CLOSED)\n"
        "      logElm.textContent += '-- connection closed --';\n"
        "    else\n"
        "      logElm.textContent += '-- connection failed -- ' + e.readyState + ' --';\n"
        "    source.close();\n"
        "  };\n"
        "} else {\n"
        "  logElm.textContent = 'Sorry, your browser does not support server-sent events...';\n"
        "}\n"
        "</script>"
        "</body>"
        "</html>"
        "";
    struct sol_http_response response = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
        .param = SOL_HTTP_REQUEST_PARAMS_INIT,
        .content = SOL_BUFFER_INIT_CONST((void *)fallback_html, sizeof(fallback_html) - 1),
        .content_type = "text/html",
        .response_code = SOL_HTTP_STATUS_OK,
    };

    return sol_http_server_send_response(request, &response);
}

static int
on_redirect_index(void *data, struct sol_http_request *request)
{
    static const char redirect_html[] = ""
        "<!DOCTYPE html>"
        "<html>"
        "<head><meta http-equiv=\"refresh\" content=\"1;URL='/static/web-inspector.html'\" /></head>"
        "<body>"
        "<h1>go to /static/web-inspector.html</h1>"
        "</body>"
        "</html>"
        "";
    struct sol_http_response response = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
        .param = SOL_HTTP_REQUEST_PARAMS_INIT,
        .content = SOL_BUFFER_INIT_CONST((void *)redirect_html, sizeof(redirect_html) - 1),
        .content_type = "text/html",
        .response_code = SOL_HTTP_STATUS_SEE_OTHER,
    };
    int r;
    struct sol_network_link_addr addr;
    struct sol_buffer url_buf = SOL_BUFFER_INIT_EMPTY;

    r = sol_http_request_get_interface_address(request, &addr);
    if (r < 0) {
        fputs("Error: Could not get address from HTTP request.\n", stderr);
        return r;
    }

    r = url_from_addr(&url_buf, &addr, addr.port, "/static/web-inspector.html");
    if (r < 0) {
        fputs("Error: Could not create URL for redirect.\n", stderr);
        return r;
    }

    r = sol_http_params_add_copy(&response.param, SOL_HTTP_REQUEST_PARAM_HEADER("Location", (const char *)url_buf.data));
    sol_buffer_fini(&url_buf);
    if (r < 0)
        return r;

    r = sol_http_params_add(&response.param, SOL_HTTP_REQUEST_PARAM_HEADER("Content-Type", "text/html"));
    if (r < 0) {
        goto end;
    }

    r = sol_http_server_send_response(request, &response);

end:
    sol_http_params_clear(&response.param);
    return r;
}

int
web_inspector_run(uint16_t port, struct runner *the_runner)
{
    const struct sol_vector *net_links;
    char rootdir[PATH_MAX];
    struct sol_buffer buf = SOL_BUFFER_INIT_CONST(rootdir, sizeof(rootdir));
    struct stat st;
    int r;

    // TODO: this should be a struct sol_buffer
    r = sol_util_get_rootdir(rootdir, sizeof(rootdir));
    if (r < 0) {
        fputs("Error: Cannot get installation directory.\n", stderr);
        goto error;
    }

    buf.used = r;
    r = sol_buffer_append_printf(&buf, "%s/web-inspector", SOL_DATADIR);
    if (r < 0) {
        fputs("Error: Cannot compute web-inspector static files path.\n", stderr);
        goto error;
    }

    server = sol_http_server_new(&(struct sol_http_server_config) {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_SERVER_CONFIG_API_VERSION, )
        .port = port,
    });
    if (!server) {
        fprintf(stderr, "Error: Cannot create HTTP server at port %" PRIu16 ".\n", port);
        r = -EINVAL;
        goto error;
    }

    r = sol_http_server_register_handler(server, "/events", on_events, the_runner);
    if (r < 0) {
        fprintf(stderr, "Warning: Cannot serve HTTP /events: %s\n", sol_util_strerrora(-r));
        goto error;
    }

    if (stat(rootdir, &st) == 0 && S_ISDIR(st.st_mode)) {
        r = sol_http_server_add_dir(server, "/static", rootdir);
        if (r < 0) {
            fprintf(stderr, "Warning: Cannot serve HTTP static files from %s: %s\n", rootdir, sol_util_strerrora(-r));
            r = sol_http_server_register_handler(server, "/", on_fallback_index, NULL);
        } else
            r = sol_http_server_register_handler(server, "/", on_redirect_index, NULL);
    } else {
        fprintf(stderr, "Warning: No directory %s to serve HTTP static files from! Use simpler fallback.\n", rootdir);
        r = sol_http_server_register_handler(server, "/", on_fallback_index, NULL);
    }
    sol_buffer_fini(&buf);
    if (r < 0) {
        fprintf(stderr, "Warning: Cannot serve HTTP /: %s\n", sol_util_strerrora(-r));
        goto error;
    }

    net_links = sol_network_get_available_links();
    if (net_links) {
        struct sol_network_link *nl;
        uint16_t idx;

        SOL_VECTOR_FOREACH_IDX (net_links, nl, idx) {
            struct sol_network_link_addr *addr;
            uint16_t aidx;

            if (!(nl->flags & SOL_NETWORK_LINK_RUNNING))
                continue;

            SOL_VECTOR_FOREACH_IDX (&nl->addrs, addr, aidx) {
                struct sol_buffer url_buf = SOL_BUFFER_INIT_EMPTY;

                r = url_from_addr(&url_buf, addr, port, "/");
                if (r >= 0)
                    printf("web-inspector at %s\n", (const char *)url_buf.data);
                sol_buffer_fini(&url_buf);
            }
        }
    }

    puts("\nweb-inspector will wait for the first connection to run the flow.\n");

    return 0;

error:
    sol_quit_with_code(EXIT_FAILURE);
    return r;
}

void
web_inspector_shutdown(void)
{
    if (!server)
        return;

    sol_http_server_del(server);
    server = NULL;
}

