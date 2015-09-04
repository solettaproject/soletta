#!/usr/bin/env python3

# This file is part of the Soletta Project
#
# Copyright (C) 2015 Intel Corporation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#   * Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#   * Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in
#     the documentation and/or other materials provided with the
#     distribution.
#   * Neither the name of Intel Corporation nor the names of its
#     contributors may be used to endorse or promote products derived
#     from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import json
import sys
import traceback
from collections import OrderedDict

def merge_schema(directory, definitions, to_merge):
    for schema in to_merge:
        if not '$ref' in schema:
            raise ValueError("no $ref in allOf")

        path, link = schema['$ref'].split('#')
        ref = load_json_schema(directory, path)
        defnref = link.split('/')[-1]
        definitions.update(ref[defnref])

def load_json_schema(directory, path, schemas={}):
    if path in schemas:
        return schemas[path]

    data = json.load(open(os.path.join(directory, path), "r"))
    if not data['$schema'].startswith("http://json-schema.org/schema"):
        raise ValueError("not a JSON schema")

    definitions = data.get("definitions", {})
    if not definitions:
        raise ValueError("empty definition block")

    if not 'title' in data:
        raise ValueError("JSON schema without title")

    required = set(data.get('required', []))

    for rt, descr in definitions.items():
        if 'allOf' in descr:
            merge_schema(directory, descr, descr['allOf'])
            del descr['allOf']
        if 'properties' in descr:
            for field, props in descr['properties'].items():
                doc = props.get('description', '')
                props['read_only'] = doc.startswith('ReadOnly,')
                props['required'] = field in required

                if props['read_only']:
                    props['description'] = props['description'][len('ReadOnly,'):].strip()

        descr['title'] = data['title']

    schemas[path] = definitions
    return definitions

JSON_TO_C = {
    "string": "char *",
    "integer": "int32_t",
    "boolean": "bool",
    "number": "double"
}

JSON_TO_C_TMP = {}
JSON_TO_C_TMP.update(JSON_TO_C)
JSON_TO_C_TMP['string'] = "const char *"
JSON_TO_C_TMP['number'] = "double"

JSON_TO_FLOW_GET_PKT = {
    "string": "sol_flow_packet_get_string",
    "integer": "sol_flow_packet_get_irange_value",
    "boolean": "sol_flow_packet_get_boolean",
    "number": "sol_flow_packet_get_drange_value"
}

JSON_TO_FLOW_SEND_PKT = {
    "string": "sol_flow_send_string_packet",
    "integer": "sol_flow_send_irange_value_packet",
    "boolean": "sol_flow_send_boolean_packet",
    "number": "sol_flow_send_drange_value_packet"
}

JSON_TO_INIT = {
    "string": "NULL",
    "integer": "0",
    "boolean": "false",
    "number": "0.0f"
}

JSON_TO_SOL_JSON = {
    "string": "string",
    "integer": "int",
    "boolean": "boolean",
    "number": "double"
}

def props_are_equivalent(p1, p2):
    # This disconsiders comments
    p1 = {k: get_type_from_property(v) for k, v in p1.items()}
    p2 = {k: get_type_from_property(v) for k, v in p2.items()}
    return p1 == p2

def object_fields_common_c(state_struct_name, name, props):
    fields = []
    for prop_name, descr in props.items():
        doc = '/* %s */' % descr.get('description', '???')
        if 'enum' in descr:
            var_type = 'enum %s_%s' % (state_struct_name, prop_name)
        else:
            var_type = JSON_TO_C[descr['type']]

        fields.append("%s %s; %s" % (var_type, prop_name, doc))

    return '\n'.join(fields)

def generate_object_serialize_fn_common_c(state_struct_name, name, props, client):
    fmtstrings = []
    for prop_name, prop_descr in props.items():
        if client and prop_descr['read_only']:
            continue

        if 'enum' in prop_descr:
            fmtstrings.append('\\"%s\\":\\"%%s\\"' % prop_name)
        elif prop_descr['type'] == 'string':
            fmtstrings.append('\\"%s\\":\\"%%s\\"' % prop_name)
        elif prop_descr['type'] == 'boolean':
            fmtstrings.append('\\"%s\\":%%s' % prop_name)
        elif prop_descr['type'] == 'integer':
            fmtstrings.append('\\"%s\\":%%d' % prop_name)
        elif prop_descr['type'] == 'number':
            fmtstrings.append('\\"%s\\":%%f' % prop_name)
        else:
            raise ValueError("invalid property type: %s" % prop_descr['type'])

    fields = []
    for prop_name, prop_descr in props.items():
        if client and prop_descr['read_only']:
            continue

        if 'enum' in prop_descr:
            fields.append('%s_%s_tbl[state->state.%s].key' % (state_struct_name, prop_name, prop_name))
        elif prop_descr['type'] == 'boolean':
            fields.append('(state->state.%s)?"true":"false"' % prop_name)
        elif prop_descr['type'] == 'string':
            fields.append('ESCAPE_STRING(state->state.%s)' % prop_name)
        else:
            fields.append('state->state.%s' % prop_name)

    if not fields:
        return ''

    return '''static uint8_t *
%(struct_name)s_serialize(struct %(type)s_resource *resource, uint16_t *length)
{
    struct %(struct_name)s *state = (struct %(struct_name)s *)resource;
    char *payload;
    int r;

    r = asprintf(&payload, "{%(fmtstrings)s}", %(fields)s);
    if (r < 0)
        return NULL;

    if (r >= 0xffff) {
        free(payload);
        errno = -ENOMEM;
        return NULL;
    }

    *length = (uint16_t)r;
    return (uint8_t *)payload;
}
''' % {
    'type': 'client' if client else 'server',
    'struct_name': name,
    'fmtstrings': ','.join(fmtstrings),
    'fields': ','.join(fields)
    }

def get_type_from_property(prop):
    if 'type' in prop:
        return prop['type']
    if 'enum' in prop:
        return 'enum:%s' % ','.join(prop['enum'])
    raise ValueError('Unknown type for property')

def object_serialize_fn_common_c(state_struct_name, name, props, client, equivalent={}):
    for item_name, item_props in equivalent.items():
        if item_props[0] == client and props_are_equivalent(props, item_props[1]):
            return '''static uint8_t *
%(struct_name)s_serialize(struct %(type)s_resource *resource, uint16_t *length)
{
    return %(item_name)s_serialize(resource, length); /* %(item_name)s is equivalent to %(struct_name)s */
}
''' % {
        'item_name': item_name,
        'struct_name': name,
        'type': 'client' if client else 'server'
    }

    equivalent[name] = (client, props)
    return generate_object_serialize_fn_common_c(state_struct_name, name, props, client)

def object_serialize_fn_client_c(state_struct_name, name, props):
    return object_serialize_fn_common_c(state_struct_name, name, props, True)

def object_serialize_fn_server_c(state_struct_name, name, props):
    return object_serialize_fn_common_c(state_struct_name, name, props, False)

def get_field_integer_client_c(id, name, prop):
    return '''if (decode_mask & (1<<%(id)d) && sol_json_token_str_eq(&key, "%(field_name)s", %(field_name_len)d)) {
    int r = sol_json_token_get_int32(&value, &fields.%(field_name)s);
    if (r < 0)
        RETURN_ERROR(r);
    decode_mask &= ~(1<<%(id)d);
    continue;
}
''' % {
        'field_name': name,
        'field_name_len': len(name),
        'id': id
    }

def get_field_number_client_c(id, name, prop):
    return '''if (decode_mask & (1<<%(id)d) && sol_json_token_str_eq(&key, "%(field_name)s", %(field_name_len)d)) {
    int r = sol_json_token_get_double(&value, &fields.%(field_name)s);
    if (r < 0)
        RETURN_ERROR(r);
    decode_mask &= ~(1<<%(id)d);
    continue;
}
''' % {
        'field_name': name,
        'field_name_len': len(name),
        'id': id
    }

def get_field_string_client_c(id, name, prop):
    return '''if (decode_mask & (1<<%(id)d) && sol_json_token_str_eq(&key, "%(field_name)s", %(field_name_len)d)) {
    if (!json_token_to_string(&value, &fields.%(field_name)s))
        RETURN_ERROR(-EINVAL);
    decode_mask &= ~(1<<%(id)d);
    continue;
}
''' % {
        'field_name': name,
        'field_name_len': len(name),
        'id': id
    }

def get_field_boolean_client_c(id, name, prop):
    return '''if (decode_mask & (1<<%(id)d) && sol_json_token_str_eq(&key, "%(field_name)s", %(field_name_len)d)) {
    if (!json_token_to_bool(&value, &fields.%(field_name)s))
        RETURN_ERROR(-EINVAL);
    decode_mask &= ~(1<<%(id)d);
    continue;
}
''' % {
        'field_name': name,
        'field_name_len': len(name),
        'id': id
    }

def get_field_enum_client_c(id, struct_name, name, prop):
    return '''if (decode_mask & (1<<%(id)d) && sol_json_token_str_eq(&key, "%(field_name)s", %(field_name_len)d)) {
    int16_t val = sol_str_table_lookup_fallback(%(struct_name)s_%(field_name)s_tbl,
        SOL_STR_SLICE_STR(value.start, value.end - value.start), -1);
    if (val < 0)
        RETURN_ERROR(-EINVAL);
    fields.%(field_name)s = (enum %(struct_name)s_%(field_name)s)val;
    decode_mask &= ~(1<<%(id)d);
    continue;
}
''' % {
        'struct_name': struct_name,
        'field_name': name,
        'field_name_len': len(name),
        'id': id
    }

def object_fields_deserializer(name, props):
    id = 0
    fields = []
    for prop_name, prop in props.items():
        if 'enum' in prop:
            fields.append(get_field_enum_client_c(id, name, prop_name, prop))
        elif prop['type'] == 'string':
            fields.append(get_field_string_client_c(id, prop_name, prop))
        elif prop['type'] == 'integer':
            fields.append(get_field_integer_client_c(id, prop_name, prop))
        elif prop['type'] == 'number':
            fields.append(get_field_number_client_c(id, prop_name, prop))
        elif prop['type'] == 'boolean':
            fields.append(get_field_boolean_client_c(id, prop_name, prop))
        else:
            raise ValueError('unknown field type: %s' % prop['type'])
        id += 1
    return '\n'.join(fields)

def generate_object_deserialize_fn_common_c(name, props):
    fields_init = []
    for field_name, field_props in props.items():
        if 'enum' in field_props:
            fields_init.append('.%s = state->%s,' % (field_name, field_name))
        elif field_props['type'] == 'string':
            fields_init.append('.%s = strdup(state->%s),' % (field_name, field_name))
        else:
            fields_init.append('.%s = state->%s,' % (field_name, field_name))

    fields_free = []
    for field_name, field_props in props.items():
        if 'enum' in field_props:
            continue
        if field_props.get('type') == 'string':
            fields_free.append('free(fields.%s);' % (field_name))

    update_state = []
    for field_name, field_props in props.items():
        if not 'enum' in field_props and field_props.get('type') == 'string':
            update_state.append('free(state->%s);' % field_name)
        update_state.append('state->%s = fields.%s;' % (field_name, field_name))

    return '''static int
%(struct_name)s_deserialize(struct %(struct_name)s *state,
    const uint8_t *payload, uint16_t payload_len, uint32_t decode_mask)
{
#define RETURN_ERROR(errcode) do { err = (errcode); goto out; } while(0)

    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;
    int err = 0;
    struct %(struct_name)s fields = {
        %(fields_init)s
    };

    sol_json_scanner_init(&scanner, payload, payload_len);
    SOL_JSON_SCANNER_OBJECT_LOOP(&scanner, &token, &key, &value, reason) {
        %(deserializers)s
    }
    if (reason != SOL_JSON_LOOP_REASON_OK)
        RETURN_ERROR(-EINVAL);

    %(update_state)s

    return 0;

out:
    %(free_fields)s
    return err;

#undef RETURN_ERROR
}
''' % {
        'struct_name': name,
        'fields': object_fields_common_c(name, name, props),
        'fields_init': '\n'.join(fields_init),
        'deserializers': object_fields_deserializer(name, props),
        'free_fields': '\n'.join(fields_free),
        'update_state': '\n'.join(update_state)
    }

def object_deserialize_fn_common_c(name, props, equivalent={}):

    for item_name, item_props in equivalent.items():
        if props_are_equivalent(props, item_props):
            return '''static int
%(struct_name)s_deserialize(struct %(struct_name)s *state,
    const uint8_t *payload, uint16_t payload_len, uint32_t decode_mask)
{
    /* %(item_name)s is equivalent to %(struct_name)s */
    return %(item_name)s_deserialize((struct %(item_name)s *)state, payload, payload_len, decode_mask);
}
''' % {
        'item_name': item_name,
        'struct_name': name
    }

    equivalent[name] = props
    return generate_object_deserialize_fn_common_c(name, props)


def object_deserialize_fn_client_c(state_struct_name, name, props):
    return '''static int
%(struct_name)s_deserialize(struct client_resource *resource, const uint8_t *payload, uint16_t payload_len)
{
    struct %(struct_name)s *res = (struct %(struct_name)s *)resource;
    return %(state_struct_name)s_deserialize(&res->state, payload, payload_len, ~0);
}
''' % {
        'struct_name': name,
        'state_struct_name': state_struct_name
    }

def object_deserialize_fn_server_c(state_struct_name, name, props):
    decode_mask = 0
    id = 0
    for field_name, field_props in props.items():
        if not field_props['read_only']:
            decode_mask |= 1<<id
        id += 1

    if not decode_mask:
        return ''

    return '''static int
%(struct_name)s_deserialize(struct server_resource *resource, const uint8_t *payload, uint16_t payload_len)
{
    struct %(struct_name)s *res = (struct %(struct_name)s *)resource;
    return %(state_struct_name)s_deserialize(&res->state, payload, payload_len, 0x%(decode_mask)x);
}
''' % {
        'struct_name': name,
        'state_struct_name': state_struct_name,
        'decode_mask': decode_mask
    }

def object_inform_flow_fn_common_c(state_struct_name, name, props, client):
    send_flow_pkts = []
    for field_name, field_props in props.items():
        if 'enum' in field_props:
            fn = 'sol_flow_send_string_packet'
            val = '%(struct_name)s_%(field_name)s_tbl[state->state.%(field_name)s].key' % {
                'struct_name': state_struct_name,
                'field_name': field_name
            }
        else:
            fn = JSON_TO_FLOW_SEND_PKT[field_props['type']]
            val = 'state->state.%(field_name)s' % {
                'field_name': field_name
            }

        send_flow_pkts.append('''%(flow_send_fn)s(resource->node, SOL_FLOW_NODE_TYPE_%(STRUCT_NAME)s__OUT__OUT_%(FIELD_NAME)s, %(val)s);''' % {
            'flow_send_fn': fn,
            'STRUCT_NAME': name.upper(),
            'FIELD_NAME': field_name.upper(),
            'val': val
        })

    return '''static void %(struct_name)s_inform_flow(struct %(type)s_resource *resource)
{
    struct %(struct_name)s *state = (struct %(struct_name)s *)resource;
    %(send_flow_pkts)s
}
''' % {
        'type': 'client' if client else 'server',
        'struct_name': name,
        'send_flow_pkts': '\n'.join(send_flow_pkts)
    }

def object_inform_flow_fn_client_c(state_struct_name, name, props):
    return object_inform_flow_fn_common_c(state_struct_name, name, props, True)

def object_inform_flow_fn_server_c(state_struct_name, name, props):
    read_only = all(field_props['read_only'] for field_name, field_props in props.items())
    return '' if read_only else object_inform_flow_fn_common_c(state_struct_name, name, props, False)

def object_open_fn_client_c(state_struct_name, resource_type, name, props):
    field_init = []
    for field_name, field_props in props.items():
        if 'enum' in field_props:
            init = '(enum %s_%s)0' % (state_struct_name, field_name)
        else:
            init = JSON_TO_INIT[field_props.get('type', 'integer')]
        field_init.append('''resource->state.%(field_name)s = %(init)s;''' % {
            'field_name': field_name,
            'init': init
        })

    no_inputs = all(field_props['read_only'] for field_name, field_props in props.items())
    if no_inputs:
        serialize_fn = 'NULL'
    else:
        serialize_fn = '%s_serialize' % name

    return '''static int
%(struct_name)s_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_%(struct_name)s_options *node_opts =
        (const struct sol_flow_node_type_%(struct_name)s_options *)options;
    static const struct client_resource_funcs funcs = {
        .serialize = %(serialize_fn)s,
        .deserialize = %(struct_name)s_deserialize,
        .inform_flow = %(struct_name)s_inform_flow,
        .found_port = SOL_FLOW_NODE_TYPE_%(STRUCT_NAME)s__OUT__FOUND
    };
    struct %(struct_name)s *resource = data;
    int r;

    r = client_resource_init(node, &resource->base, "%(resource_type)s", node_opts->device_id, &funcs);
    if (!r) {
        %(field_init)s
    }

    return 0;
}
''' % {
        'struct_name': name,
        'STRUCT_NAME': name.upper(),
        'resource_type': resource_type,
        'field_init': '\n'.join(field_init),
        'serialize_fn': serialize_fn
    }

def object_open_fn_server_c(state_struct_name, resource_type, name, props, definitions={'id':0}):
    def_id = definitions['id']
    definitions['id'] += 1

    no_inputs = all(field_props['read_only'] for field_name, field_props in props.items())
    if no_inputs:
        deserialize_fn_name = 'NULL'
        inform_flow_fn_name = 'NULL'
    else:
        deserialize_fn_name = '%s_deserialize' % name
        inform_flow_fn_name = '%s_inform_flow' % name

    field_init = []
    for field_name, field_props in props.items():
        if 'enum' in field_props:
            init = '(enum %s_%s)0' % (state_struct_name, field_name)
        else:
            init = JSON_TO_INIT[field_props.get('type', 'integer')]
        field_init.append('''resource->state.%(field_name)s = %(init)s;''' % {
            'field_name': field_name,
            'init': init
        })

    return '''static int
%(struct_name)s_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    static const struct sol_str_slice rt_slice = SOL_STR_SLICE_LITERAL("%(resource_type)s");
    static const struct sol_str_slice def_slice = SOL_STR_SLICE_LITERAL("/etta/%(def_id)x");
    static const struct server_resource_funcs funcs = {
        .serialize = %(struct_name)s_serialize,
        .deserialize = %(deserialize_fn_name)s,
        .inform_flow = %(inform_flow_fn_name)s
    };
    struct %(struct_name)s *resource = data;
    int r;

    r = server_resource_init(&resource->base, node, rt_slice, def_slice, &funcs);
    if (!r) {
        %(field_init)s
    }

    return r;
}
''' % {
        'struct_name': name,
        'resource_type': resource_type,
        'def_id': def_id,
        'deserialize_fn_name': deserialize_fn_name,
        'inform_flow_fn_name': inform_flow_fn_name,
        'field_init': '\n'.join(field_init)
    }

def object_close_fn_client_c(name, props):
    destroy_fields = []
    for field_name, field_props in props.items():
        if 'enum' in field_props:
            continue
        if field_props.get('type') == 'string':
            destroy_fields.append('free(resource->state.%s);' % field_name)

    return '''static void %(struct_name)s_close(struct sol_flow_node *node, void *data)
{
    struct %(struct_name)s *resource = data;
    %(destroy_fields)s
    client_resource_close(&resource->base);
}
''' % {
        'struct_name': name,
        'destroy_fields': '\n'.join(destroy_fields)
    }

def object_close_fn_server_c(name, props):
    destroy_fields = []
    for field_name, field_props in props.items():
        if 'enum' in field_props:
            continue
        if field_props.get('type') == 'string':
            destroy_fields.append('free(resource->state.%s);' % field_name)

    return '''static void %(struct_name)s_close(struct sol_flow_node *node, void *data)
{
    struct %(struct_name)s *resource = data;
    %(destroy_fields)s
    server_resource_close(&resource->base);
}
''' % {
        'struct_name': name,
        'destroy_fields': '\n'.join(destroy_fields)
    }

def object_setters_fn_common_c(state_struct_name, name, props, client):
    fields = []
    for field, descr in props.items():
        if client and descr['read_only']:
            continue

        if 'enum' in descr:
            fields.append('''static int
%(struct_name)s_set_%(field_name)s(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct %(struct_name)s *resource = data;
    const char *var;

    if (!sol_flow_packet_get_string(packet, &var)) {
        int16_t val = sol_str_table_lookup_fallback(%(state_struct_name)s_%(field_name)s_tbl,
            sol_str_slice_from_str(var), -1);
        if (val >= 0) {
            resource->state.%(field_name)s = (enum %(state_struct_name)s_%(field_name)s)val;
            %(type)s_resource_schedule_update(&resource->base);
            return 0;
        }
        return -ENOENT;
    }
    return -EINVAL;
}
''' % {
        'field_name': field,
        'FIELD_NAME': field.upper(),
        'state_struct_name': state_struct_name,
        'STATE_STRUCT_NAME': state_struct_name.upper(),
        'struct_name': name,
        'type': 'client' if client else 'server'
    })

        else:
            fields.append('''static int
%(struct_name)s_set_%(field_name)s(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct %(struct_name)s *resource = data;
    %(c_type_tmp)s var;
    int r;

    r = %(c_getter)s(packet, &var);
    if (!r) {
        resource->state.%(field_name)s = (%(c_type)s) var;
        %(type)s_resource_schedule_update(&resource->base);
    }
    return r;
}
''' % {
        'struct_name': name,
        'field_name': field,
        'c_type': JSON_TO_C[descr['type']],
        'c_type_tmp': JSON_TO_C_TMP[descr['type']],
        'c_getter': JSON_TO_FLOW_GET_PKT[descr['type']],
        'type': 'client' if client else 'server'
    })

    return '\n'.join(fields)

def object_setters_fn_client_c(state_struct_name, name, props):
    return object_setters_fn_common_c(state_struct_name, name, props, True)

def object_setters_fn_server_c(state_struct_name, name, props):
    return object_setters_fn_common_c(state_struct_name, name, props, False)

def generate_enums_common_c(name, props):
    output = []
    for field, descr in props.items():
        if 'enum' in descr:
            if 'description' in descr:
                output.append('''/* %s */''' % descr['description'])
            output.append('''enum %(struct_name)s_%(field_name)s { %(items)s };''' % {
                'struct_name': name,
                'field_name': field,
                'items': ', '.join(('%s_%s_%s' % (name, field, item)).upper() for item in descr['enum'])
            })

            output.append('''static const struct sol_str_table %(struct_name)s_%(field_name)s_tbl[] = {
    %(items)s,
    { }
};''' % {
                'struct_name': name,
                'field_name': field,
                'items': ',\n'.join('SOL_STR_TABLE_ITEM(\"%s\", %s_%s_%s)' % (
                    item, name.upper(), field.upper(), item.upper()) for item in descr['enum'])
            })

    return '\n'.join(output)

def generate_object_client_c(resource_type, state_struct_name, name, props):
    return """struct %(struct_name)s {
    struct client_resource base;
    struct %(state_struct_name)s state;
};

%(serialize_fn)s
%(deserialize_fn)s
%(inform_flow_fn)s
%(open_fn)s
%(close_fn)s
%(setters_fn)s
""" % {
    'state_struct_name': state_struct_name,
    'struct_name': name,
    'serialize_fn': object_serialize_fn_client_c(state_struct_name, name, props),
    'deserialize_fn': object_deserialize_fn_client_c(state_struct_name, name, props),
    'inform_flow_fn': object_inform_flow_fn_client_c(state_struct_name, name, props),
    'open_fn': object_open_fn_client_c(state_struct_name, resource_type, name, props),
    'close_fn': object_close_fn_client_c(name, props),
    'setters_fn': object_setters_fn_client_c(state_struct_name, name, props)
    }

def generate_object_server_c(resource_type, state_struct_name, name, props):
    return """struct %(struct_name)s {
    struct server_resource base;
    struct %(state_struct_name)s state;
};

%(serialize_fn)s
%(deserialize_fn)s
%(inform_flow_fn)s
%(open_fn)s
%(close_fn)s
%(setters_fn)s
""" % {
    'struct_name': name,
    'state_struct_name': state_struct_name,
    'serialize_fn': object_serialize_fn_server_c(state_struct_name, name, props),
    'deserialize_fn': object_deserialize_fn_server_c(state_struct_name, name, props),
    'inform_flow_fn': object_inform_flow_fn_server_c(state_struct_name, name, props),
    'open_fn': object_open_fn_server_c(state_struct_name, resource_type, name, props),
    'close_fn': object_close_fn_server_c(name, props),
    'setters_fn': object_setters_fn_server_c(state_struct_name, name, props)
    }

def generate_object_common_c(name, props):
    return """%(enums)s
struct %(struct_name)s {
    %(struct_fields)s
};
%(deserialize_fn)s
""" % {
        'enums': generate_enums_common_c(name, props),
        'struct_name': name,
        'struct_fields': object_fields_common_c(name, name, props),
        'deserialize_fn': object_deserialize_fn_common_c(name, props),
    }

def generate_object_json(resource_type, struct_name, node_name, title, props, server):
    in_ports = []
    for prop_name, prop_descr in props.items():
        if not server and prop_descr['read_only']:
            continue

        in_ports.append({
            'data_type': JSON_TO_SOL_JSON[prop_descr.get('type', 'string')],
            'description': prop_descr.get('description', '???'),
            'methods': {
                'process': '%s_set_%s' % (struct_name, prop_name)
            },
            'name': 'IN_%s' % prop_name.upper()
        })

    if server:
        out_ports = []
    else:
        out_ports = [{
            'data_type': 'boolean',
            'description': 'Outputs true if resource was found, false if not, or if unreachable',
            'name': 'FOUND'
        }]
    for prop_name, prop_descr in props.items():
        out_ports.append({
            'data_type': JSON_TO_SOL_JSON[prop_descr.get('type', 'string')],
            'description': prop_descr.get('description', '???'),
            'name': 'OUT_%s' % prop_name.upper()
        })

    output = {
        'methods': {
            'open': '%s_open' % struct_name,
            'close': '%s_close' % struct_name
        },
        'private_data_type': struct_name,
        'name': node_name,
        'url': 'http://solettaproject.org/doc/latest/components/%s.html' % node_name.replace('/', '-')
    }
    if server:
        output.update({
            'category': 'iot/server',
            'description': 'OIC Server (%s)' % title
        })
    else:
        output.update({
            'category': 'iot/client',
            'description': 'OIC Client (%s)' % title,
            'options': {
                'version': 1,
                'members': [
                    {
                        'data_type': 'string',
                        'description': 'Unique device ID (UUID, MAC address, etc)',
                        'name': 'device_id'
                    }
                ]
            }
        })

    if in_ports:
        output['in_ports'] = in_ports
    if out_ports:
        output['out_ports'] = out_ports

    return output

def generate_object(rt, title, props):
    def type_value(item):
        return '%s %s' % (get_type_from_property(item[1]), item[0])

    resource_type = rt

    if rt.startswith('oic.r.'):
        rt = rt[len('oic.r.'):]
    elif rt.startswith('core.'):
        rt = rt[len('core.'):]

    c_identifier = rt.replace(".", "_").lower()
    flow_identifier = rt.replace(".", "-").lower()

    client_node_name = "oic/client-%s" % flow_identifier
    client_struct_name = "oic_client_%s" % c_identifier
    server_node_name = "oic/server-%s" % flow_identifier
    server_struct_name = "oic_server_%s" % c_identifier
    state_struct_name = "oic_state_%s" % c_identifier

    new_props = OrderedDict()
    for k, v in sorted(props.items(), key=type_value):
        new_props[k] = v
    props = new_props

    retval = {
        'c_common': generate_object_common_c(state_struct_name, props),
        'c_client': generate_object_client_c(resource_type, state_struct_name, client_struct_name, props),
        'c_server': generate_object_server_c(resource_type, state_struct_name, server_struct_name, props),
        'json_client': generate_object_json(resource_type, client_struct_name, client_node_name, title, props, False),
        'json_server': generate_object_json(resource_type, server_struct_name, server_node_name, title, props, True)
    }
    return retval

def generate_for_schema(directory, path):
    j = load_json_schema(directory, path)

    for rt, defn in j.items():
        if not (rt.startswith("oic.r.") or rt.startswith("core.")):
            raise ValueError("not an OIC resource definition")

        if defn.get('type') == 'object':
            yield generate_object(rt, defn['title'], defn['properties'])

def master_json_as_string(generated):
    master_json = {
        '$schema': 'http://solettaproject.github.io/soletta/schemas/node-type-genspec.schema',
        'name': 'oic',
        'meta': {
            'author': 'Intel Corporation',
            'license': 'BSD 3-Clause',
            'version': '1'
        },
        'types': [t['json_server'] for t in generated] + [t['json_client'] for t in generated]
    }
    return json.dumps(master_json, indent=4)

def master_c_as_string(generated):
    generated = list(generated)
    code = '''#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "sol-flow/oic.h"

#include "sol-coap.h"
#include "sol-json.h"
#include "sol-mainloop.h"
#include "sol-missing.h"
#include "sol-oic-client.h"
#include "sol-oic-server.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util.h"

#define DEFAULT_UDP_PORT 5683
#define MULTICAST_ADDRESS_IPv4 "224.0.1.187"
#define MULTICAST_ADDRESS_IPv6_LOCAL "ff02::fd"
#define MULTICAST_ADDRESS_IPv6_SITE "ff05::fd"
#define FIND_PERIOD_MS 5000
#define UPDATE_TIMEOUT_MS 50

struct client_resource;
struct server_resource;

struct client_resource_funcs {
    uint8_t *(*serialize)(struct client_resource *resource, uint16_t *length);
    int (*deserialize)(struct client_resource *resource, const uint8_t *payload, uint16_t payload_len);
    void (*inform_flow)(struct client_resource *resource);
    int found_port;
};

struct server_resource_funcs {
    uint8_t *(*serialize)(struct server_resource *resource, uint16_t *length);
    int (*deserialize)(struct server_resource *resource, const uint8_t *payload, uint16_t payload_len);
    void (*inform_flow)(struct server_resource *resource);
};

struct client_resource {
    struct sol_flow_node *node;
    const struct client_resource_funcs *funcs;

    struct sol_oic_resource *resource, *tentative_resource;

    struct sol_timeout *find_timeout;
    struct sol_timeout *update_schedule_timeout;

    struct sol_oic_client client;

    const char *rt;
    char *device_id;
};

struct server_resource {
    struct sol_flow_node *node;
    const struct server_resource_funcs *funcs;

    struct sol_coap_resource *coap;
    struct sol_timeout *update_schedule_timeout;
    char *endpoint;

    struct sol_oic_resource_type oic;
};

static struct sol_network_link_addr multicast_ipv4, multicast_ipv6_local, multicast_ipv6_site;
static bool multicast_addresses_initialized = false;

static bool
initialize_multicast_addresses_once(void)
{
    if (multicast_addresses_initialized)
        return true;

    multicast_ipv4 = (struct sol_network_link_addr) { .family = AF_INET, .port = DEFAULT_UDP_PORT };
    if (!sol_network_addr_from_str(&multicast_ipv4, MULTICAST_ADDRESS_IPv4)) {
        SOL_WRN("Could not parse multicast IP address");
        return false;
    }
    multicast_ipv6_local = (struct sol_network_link_addr) { .family = AF_INET6, .port = DEFAULT_UDP_PORT };
    if (!sol_network_addr_from_str(&multicast_ipv6_local, MULTICAST_ADDRESS_IPv6_LOCAL)) {
        SOL_WRN("Could not parse multicast IP address");
        return false;
    }
    multicast_ipv6_site = (struct sol_network_link_addr) { .family = AF_INET6, .port = DEFAULT_UDP_PORT };
    if (!sol_network_addr_from_str(&multicast_ipv6_site, MULTICAST_ADDRESS_IPv6_SITE)) {
        SOL_WRN("Could not parse multicast IP address");
        return false;
    }

    multicast_addresses_initialized = true;
    return true;
}

static bool
client_resource_implements_type(struct sol_oic_resource *oic_res, const char *resource_type)
{
    struct sol_str_slice rt = SOL_STR_SLICE_STR(resource_type, strlen(resource_type));
    struct sol_str_slice *type;
    uint16_t idx;

    SOL_VECTOR_FOREACH_IDX(&oic_res->types, type, idx) {
        if (sol_str_slice_eq(*type, rt))
            return true;
    }

    return false;
}

static void
state_changed(struct sol_oic_client *oic_cli, const struct sol_network_link_addr *cliaddr,
    const struct sol_str_slice *href, const struct sol_str_slice *payload, void *data)
{
    struct client_resource *resource = data;
    int r;

    if (!sol_str_slice_eq(*href, resource->resource->href)) {
        SOL_WRN("Received response to href=`%%.*s`, but resource href is `%%.*s`",
            SOL_STR_SLICE_PRINT(*href),
            SOL_STR_SLICE_PRINT(resource->resource->href));
        return;
    }

    if (!sol_network_link_addr_eq(cliaddr, &resource->resource->addr)) {
        char resaddr[SOL_INET_ADDR_STRLEN] = {0};
        char respaddr[SOL_INET_ADDR_STRLEN] = {0};

        if (!sol_network_addr_to_str(&resource->resource->addr, resaddr, sizeof(resaddr))) {
            SOL_WRN("Could not convert network address to string");
            return;
        }
        if (!sol_network_addr_to_str(cliaddr, respaddr, sizeof(respaddr))) {
            SOL_WRN("Could not convert network address to string");
            return;
        }

        SOL_WRN("Expecting response from %%s, got from %%s, ignoring", resaddr, respaddr);
        return;
    }

    r = resource->funcs->deserialize(resource, (const uint8_t *)payload->data, payload->len);
    if (r >= 0)
        resource->funcs->inform_flow(resource);
}

static void
server_info_received(struct sol_oic_client *oic_cli, const struct sol_oic_server_information *info,
    void *data)
{
    struct client_resource *resource = data;
    int r;

    /* Some OIC device sent this node a discovery response packet but node's already set up. */
    if (resource->resource)
        return;

    if (info->api_version != SOL_OIC_SERVER_INFORMATION_API_VERSION) {
        SOL_WRN("OIC server information API version mismatch");
        goto out;
    }

    if (!info->device.id.len) {
        SOL_WRN("No device ID present in server info");
        goto out;
    }

    if (!sol_str_slice_str_eq(info->device.id, resource->device_id)) {
        /* Not the droid we're looking for. */
        return;
    }

    SOL_INF("Found resource matching device_id %%s", resource->device_id);
    resource->resource = resource->tentative_resource;
    resource->tentative_resource = NULL;

    if (resource->find_timeout) {
        sol_timeout_del(resource->find_timeout);
        resource->find_timeout = NULL;
    }

    r = sol_oic_client_resource_set_observable(oic_cli, resource->resource, state_changed, resource, true);
    if (!r)
        SOL_WRN("Could not observe resource as requested, will try again");

out:
    r = sol_flow_send_boolean_packet(resource->node, resource->funcs->found_port, !!resource->resource);
    if (r < 0)
        SOL_WRN("Could not send flow packet, will try again");
}

static void
found_resource(struct sol_oic_client *oic_cli, struct sol_oic_resource *oic_res, void *data)
{
    struct client_resource *resource = data;
    int r;

    /* Some OIC device sent this node a discovery response packet but node's already set up. */
    if (resource->resource)
        goto out;

    /* FIXME: Should this check move to sol-oic-client? Does it actually make sense? */
    if (resource->rt && !client_resource_implements_type(oic_res, resource->rt)) {
        SOL_WRN("Received resource that does not implement rt=%%s, ignoring", resource->rt);
        goto out;
    }

    resource->tentative_resource = sol_oic_resource_ref(oic_res);
    if (sol_oic_client_get_server_info(oic_cli, oic_res, server_info_received, data))
        return;

    SOL_WRN("Could not create packet to obtain server information");

out:
    r = sol_flow_send_boolean_packet(resource->node, resource->funcs->found_port, false);
    if (r < 0)
        SOL_WRN("Could not send flow packet, will try again");
}

static void
send_discovery_packets(struct client_resource *resource)
{
    sol_oic_client_find_resource(&resource->client, &multicast_ipv4, resource->rt,
        found_resource, resource);
    sol_oic_client_find_resource(&resource->client, &multicast_ipv6_local, resource->rt,
        found_resource, resource);
    sol_oic_client_find_resource(&resource->client, &multicast_ipv6_site, resource->rt,
        found_resource, resource);
}

static bool
find_timer(void *data)
{
    struct client_resource *resource = data;

    if (resource->resource) {
        SOL_INF("Timer expired when node already configured; disabling");
        resource->find_timeout = NULL;
        return false;
    }

    send_discovery_packets(resource);
    return true;
}

static char *
create_endpoint(void)
{
    static int endpoint_id = 0;
    char *endpoint;

    if (asprintf(&endpoint, "/sol/%%x", endpoint_id) < 0)
        return NULL;

    endpoint_id++;
    return endpoint;
}

static bool
server_resource_perform_update(void *data)
{
    struct server_resource *resource = data;
    uint8_t *payload;
    uint16_t payload_len;

    SOL_NULL_CHECK(resource->funcs->serialize, false);
    payload = resource->funcs->serialize(resource, &payload_len);
    if (!payload) {
        SOL_WRN("Error while serializing update message");
    } else {
        resource->funcs->inform_flow(resource);
        sol_oic_notify_observers(resource->coap, payload, payload_len);
        free(payload);
    }

    resource->update_schedule_timeout = NULL;
    return false;
}

static void
server_resource_schedule_update(struct server_resource *resource)
{
    if (resource->update_schedule_timeout)
        return;

    resource->update_schedule_timeout = sol_timeout_add(UPDATE_TIMEOUT_MS,
        server_resource_perform_update, resource);
}

static sol_coap_responsecode_t
server_handle_put(const struct sol_network_link_addr *cliaddr, const void *data,
    uint8_t *payload, uint16_t *payload_len)
{
    const struct server_resource *resource = data;
    int r;

    if (!resource->funcs->deserialize)
        return SOL_COAP_RSPCODE_NOT_IMPLEMENTED;

    r = resource->funcs->deserialize((struct server_resource *)resource, payload, *payload_len);
    if (!r) {
        server_resource_schedule_update((struct server_resource *)resource);
        *payload_len = 0;
        return SOL_COAP_RSPCODE_CHANGED;
    }
    return SOL_COAP_RSPCODE_PRECONDITION_FAILED;
}

static sol_coap_responsecode_t
server_handle_get(const struct sol_network_link_addr *cliaddr, const void *data,
    uint8_t *payload, uint16_t *payload_len)
{
    const struct server_resource *resource = data;
    uint16_t serialized_len;
    uint8_t *serialized;

    if (!resource->funcs->serialize)
        return SOL_COAP_RSPCODE_NOT_IMPLEMENTED;

    serialized = resource->funcs->serialize((struct server_resource*)resource, &serialized_len);
    if (!serialized)
        return SOL_COAP_RSPCODE_INTERNAL_ERROR;

    if (serialized_len > *payload_len) {
        free(serialized);
        return SOL_COAP_RSPCODE_INTERNAL_ERROR;
    }

    memcpy(payload, serialized, serialized_len);
    *payload_len = serialized_len;
    free(serialized);
    return SOL_COAP_RSPCODE_CONTENT;
}

// log_init() implementation happens within oic-gen.c
static void log_init(void);

static int
server_resource_init(struct server_resource *resource, struct sol_flow_node *node,
    struct sol_str_slice resource_type, struct sol_str_slice defn_endpoint,
    const struct server_resource_funcs *funcs)
{
    struct sol_oic_device_definition *def;

    log_init();

    if (sol_oic_server_init(DEFAULT_UDP_PORT) != 0) {
        SOL_WRN("Could not create %%.*s server", SOL_STR_SLICE_PRINT(resource_type));
        return -ENOTCONN;
    }

    resource->endpoint = create_endpoint();
    SOL_NULL_CHECK(resource->endpoint, -ENOMEM);

    resource->node = node;
    resource->update_schedule_timeout = NULL;
    resource->funcs = funcs;

    resource->oic = (struct sol_oic_resource_type) {
        .api_version = SOL_OIC_RESOURCE_TYPE_API_VERSION,
        .endpoint = sol_str_slice_from_str(resource->endpoint),
        .resource_type = resource_type,
        .iface = SOL_STR_SLICE_LITERAL("oc.mi.def"),
        .get = { .handle = server_handle_get },
        .put = { .handle = server_handle_put },
    };

    def = sol_oic_server_register_definition(defn_endpoint, resource_type,
        SOL_COAP_FLAGS_OC_CORE | SOL_COAP_FLAGS_WELL_KNOWN);
    if (!def)
        goto out;

    resource->coap = sol_oic_device_definition_register_resource_type(def,
        &resource->oic, resource, SOL_COAP_FLAGS_OC_CORE | SOL_COAP_FLAGS_OBSERVABLE);
    if (!resource->coap)
        goto out;

    return 0;

out:
    sol_oic_server_release();
    free(resource->endpoint);
    return -EINVAL;
}

static void
server_resource_close(struct server_resource *resource)
{
    if (resource->update_schedule_timeout)
        sol_timeout_del(resource->update_schedule_timeout);
    free(resource->endpoint);
    sol_oic_server_release();
}

static int
client_resource_init(struct sol_flow_node *node, struct client_resource *resource, const char *resource_type,
    const char *device_id, const struct client_resource_funcs *funcs)
{
    log_init();
    if (!initialize_multicast_addresses_once()) {
        SOL_ERR("Could not initialize multicast addresses");
        return -ENOTCONN;
    }

    assert(resource_type);

    if (!device_id)
        return -EINVAL;

    resource->client.api_version = SOL_OIC_CLIENT_API_VERSION;
    resource->client.server = sol_coap_server_new(0);
    SOL_NULL_CHECK(resource->client.server, -ENOMEM);

    resource->device_id = strdup(device_id);
    SOL_NULL_CHECK_GOTO(resource->device_id, nomem);

    resource->node = node;
    resource->find_timeout = NULL;
    resource->update_schedule_timeout = NULL;
    resource->resource = NULL;
    resource->funcs = funcs;
    resource->rt = resource_type;

    SOL_INF("Sending multicast packets to find resource with device_id %%s (rt=%%s)",
        resource->device_id, resource->rt);
    resource->find_timeout = sol_timeout_add(FIND_PERIOD_MS, find_timer, resource);
    if (resource->find_timeout) {
        /* Perform a find now instead of waiting FIND_PERIOD_MS the first time.  If the
         * resource is found in the mean time, the timeout will be automatically disabled. */
        send_discovery_packets(resource);
        return 0;
    }

    SOL_ERR("Could not create timeout to find resource");
    free(resource->device_id);

nomem:
    sol_coap_server_unref(resource->client.server);
    return -ENOMEM;
}

static void
client_resource_close(struct client_resource *resource)
{
    free(resource->device_id);

    if (resource->find_timeout)
        sol_timeout_del(resource->find_timeout);
    if (resource->update_schedule_timeout)
        sol_timeout_del(resource->update_schedule_timeout);

    if (resource->resource) {
        bool r = sol_oic_client_resource_set_observable(&resource->client, resource->resource,
            NULL, NULL, false);
        if (!r)
            SOL_WRN("Could not unobserve resource");

        sol_oic_resource_unref(resource->resource);
    }

    sol_coap_server_unref(resource->client.server);
}

static bool
client_resource_perform_update(void *data)
{
    struct client_resource *resource = data;
    uint8_t *payload;
    uint16_t payload_len;

    SOL_NULL_CHECK_GOTO(resource->resource, disable_timeout);
    SOL_NULL_CHECK_GOTO(resource->funcs->serialize, disable_timeout);
    payload = resource->funcs->serialize(resource, &payload_len);
    if (!payload) {
        SOL_WRN("Error while serializing update message");
    } else {
        int r = sol_oic_client_resource_request(&resource->client, resource->resource,
            SOL_COAP_METHOD_PUT, payload, payload_len, NULL, NULL);
        free(payload);
        if (r < 0) {
            SOL_WRN("Could not send update request to resource, will try again");
            return true;
        }
    }

disable_timeout:
    resource->update_schedule_timeout = NULL;
    return false;
}

static void
client_resource_schedule_update(struct client_resource *resource)
{
    if (resource->update_schedule_timeout)
        return;

    resource->update_schedule_timeout = sol_timeout_add(UPDATE_TIMEOUT_MS,
        client_resource_perform_update, resource);
}

static const char escapable_chars[] = {'\\\\', '\\"', '/', '\\b', '\\f', '\\n', '\\r', '\\t'};

SOL_ATTR_USED static size_t
calculate_escaped_len(const char *s)
{
    size_t len = 0;
    for (; *s; s++) {
        if (memchr(escapable_chars, *s, sizeof(escapable_chars)))
            len++;
        len++;
    }
    return len + 1;
}

SOL_ATTR_USED static char *
escape_json_string(const char *s, char *buf)
{
    char *out = buf;

    for (; *s; s++) {
        if (memchr(escapable_chars, *s, sizeof(escapable_chars))) {
            *buf++ = '\\\\';
            switch (*s) {
            case '"':  *buf++ = '"'; break;
            case '\\\\': *buf++ = '\\\\'; break;
            case '/':  *buf++ = '/'; break;
            case '\\b': *buf++ = 'b'; break;
            case '\\f': *buf++ = 'f'; break;
            case '\\n': *buf++ = 'n'; break;
            case '\\r': *buf++ = 'r'; break;
            case '\\t': *buf++ = 't'; break;
            }
        } else {
            *buf++ = *s;
        }
    }
    *buf++ = '\\0';
    return out;
}

#define ESCAPE_STRING(s) \
    escape_json_string(s, alloca(calculate_escaped_len(s)))

SOL_ATTR_USED static bool
json_token_to_string(struct sol_json_token *token, char **out)
{
    if (sol_json_token_get_type(token) != SOL_JSON_TYPE_STRING)
        return false;
    free(*out);
    *out = strndup(token->start, token->end - token->start);
    return !!*out;
}

SOL_ATTR_USED static bool
json_token_to_bool(struct sol_json_token *token, bool *out)
{
    if (sol_json_token_get_type(token) == SOL_JSON_TYPE_TRUE)
        *out = true;
    else if (sol_json_token_get_type(token) == SOL_JSON_TYPE_FALSE)
        *out = false;
    else
        return false;
    return true;
}

%(generated_c_common)s
%(generated_c_client)s
%(generated_c_server)s

#include "oic-gen.c"
''' % {
        'generated_c_common': '\n'.join(t['c_common'] for t in generated),
        'generated_c_client': '\n'.join(t['c_client'] for t in generated),
        'generated_c_server': '\n'.join(t['c_server'] for t in generated),
    }

    return code.replace('\n\n\n', '\n')

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema-dir",
                        help="Directory where JSON schemas are located",
                        required=True)
    parser.add_argument("--node-type-json",
                        help="Path to store the master JSON with node type information",
                        required=True)
    parser.add_argument("--node-type-impl",
                        help="Path to store the node type implementation",
                        required=True)
    args = parser.parse_args()

    def seems_schema(path):
        return path.endswith('.json') and (path.startswith('oic.r.') or path.startswith('core.'))

    generated = []
    print('Generating code for schemas: ', end='')
    for path in (f for f in os.listdir(args.schema_dir) if seems_schema(f)):
        print(path, end=', ')

        try:
            for code in generate_for_schema(args.schema_dir, path):
                generated.append(code)
        except KeyError as e:
            if e.args[0] == 'array':
                print("(arrays unsupported)", end=' ')
            else:
                raise e
        except Exception as e:
            print('Ignoring due to exception in generator. Traceback follows:')
            traceback.print_exc(e, file=sys.stderr)
            continue

    print('\nWriting master JSON: %s' % args.node_type_json)
    open(args.node_type_json, 'w+').write(master_json_as_string(generated))

    print('Writing C: %s' % args.node_type_impl)
    open(args.node_type_impl, 'w+').write(master_c_as_string(generated))
    if os.path.exists('/usr/bin/indent'):
        print('Indenting generated C.')
        os.system("/usr/bin/indent -kr -l120 '%s'" % args.node_type_impl)

    print('Done.')
