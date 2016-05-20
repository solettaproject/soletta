#!/usr/bin/env python3

# This file is part of the Soletta Project
#
# Copyright (C) 2015 Intel Corporation. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import json
import sys
import traceback
import re
from collections import OrderedDict

def merge_ref(directory, definitions, ref_link):
    path, link = ref_link.split('#')
    ref = load_json_schema(directory, path)
    defnref = link.split('/')[-1]

    definitions.update(ref[defnref])

def merge_schema(directory, definitions, to_merge):
    for schema in to_merge:
        if not '$ref' in schema:
            raise ValueError("no $ref in allOf")

        merge_ref(directory, definitions, schema['$ref'])

def expand_json_schema(directory, schema):
    if 'allOf' in schema:
        merge_schema(directory, schema, schema['allOf'])
        del schema['allOf']
    if '$ref' in schema:
        merge_ref(directory, schema, schema['$ref'])
        del schema['$ref']
    for key, value in schema.items():
        if type(value) == dict and value.get('type', '') != 'array':
                expand_json_schema(directory, value)


def load_json_schema(directory, path, schemas={}):
    if path in schemas:
        return schemas[path]

    data = json.load(open(os.path.join(directory, path), "r", encoding='UTF-8'))
    if not data['$schema'].startswith("http://json-schema.org/"):
        raise ValueError("not a JSON schema")

    definitions = data.get("definitions", {})
    if not definitions:
        raise ValueError("empty definition block")

    # if title is missing get it from file name
    if not 'title' in data:
        title = path
        if title.startswith('oic.r.'):
            title = title[len('oic.r.'):]
        elif title.startswith('core.'):
            title = title[len('core.'):]
        if title.endswith('.json'):
            title = title[:-len('.json')]
        title = title.replace(".", " ").title()
    else:
        title = data['title']

    required = set(data.get('required', []))

    for rt, descr in definitions.items():
        if '$ref' in descr.get('properties', ()):
            new_dict = {}
            merge_ref(directory, new_dict, descr['properties']['$ref']);
            descr['properties'].update(new_dict['properties'])
            del descr['properties']['$ref']
        expand_json_schema(directory, descr)
        if 'properties' in descr:
            for field, props in descr['properties'].items():
                doc = props.get('description', '')
                props['read_only'] = doc.startswith('ReadOnly,')
                props['required'] = field in required

                if props['read_only']:
                    props['short_description'] = doc[len('ReadOnly,'):].strip()
                else:
                    props['short_description'] = props.get('description', None)

        descr['title'] = title

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
JSON_TO_C_TMP['number'] = "double"

JSON_TO_FLOW_GET_PKT = {
    "string": "sol_flow_packet_get_string",
    "integer": "sol_flow_packet_get_irange_value",
    "boolean": "sol_flow_packet_get_boolean",
    "number": "sol_flow_packet_get_drange_value"
}

JSON_TO_FLOW_CHECK_UPDATED = {
    "string": "check_updated_string",
    "integer": "check_updated_int32",
    "boolean": "check_updated_boolean",
    "number": "check_updated_number"
}

JSON_TO_FLOW_SEND_PKT = {
    "string": "send_string_packet",
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
    "number": "float"
}

def props_are_equivalent(p1, p2):
    # This disconsiders comments
    p1 = {k: get_type_from_property(v) for k, v in p1.items()}
    p2 = {k: get_type_from_property(v) for k, v in p2.items()}
    return p1 == p2

def object_fields_common_c(state_struct_name, name, props):
    fields = []
    for prop_name, descr in props.items():
        doc = '/* %s */' % descr.get('short_description', '???')
        if 'enum' in descr:
            var_type = 'enum %s_%s' % (state_struct_name, prop_name)
        else:
            var_type = JSON_TO_C[descr['type']]

        fields.append("%s %s; %s" % (var_type, prop_name, doc))

    return '\n'.join(fields)

def generate_object_to_repr_vec_fn_common_c(state_struct_name, name, props, client):
    fields = []
    for prop_name, prop_descr in props.items():
        if client and prop_descr['read_only']:
            continue

        if 'enum' in prop_descr:
            tbl = '%s_%s_tbl' % (state_struct_name, prop_name)
            val = '%s[state->state.%s].key' % (tbl, prop_name)
            vallen = '%s[state->state.%s].len' % (tbl, prop_name)

            ftype = 'SOL_OIC_REPR_TEXT_STRING'
            fargs = (val, vallen)
        elif prop_descr['type'] == 'boolean':
            val = 'state->state.%s' % prop_name

            ftype = 'SOL_OIC_REPR_BOOLEAN'
            fargs = (val, )
        elif prop_descr['type'] == 'string':
            val = 'state->state.%s' % prop_name
            vallen = '%s ? strlen(%s) : 0' % (val, val)

            ftype = 'SOL_OIC_REPR_TEXT_STRING'
            fargs = (val, vallen)
        elif prop_descr['type'] == 'integer':
            val = 'state->state.%s' % prop_name

            ftype = 'SOL_OIC_REPR_INT'
            fargs = (val, )
        elif prop_descr['type'] == 'number':
            val = 'state->state.%s' % prop_name

            ftype = 'SOL_OIC_REPR_DOUBLE'
            fargs = (val, )
        else:
            raise ValueError('unknown field type: %s' % prop['type'])

        vars = {
            'ftype': ftype,
            'key': prop_name,
            'fargs': ', '.join(fargs)
        }
        fields.append('''r = sol_oic_map_append(repr_map, &%(ftype)s("%(key)s", %(fargs)s));
        SOL_INT_CHECK(r, < 0, false);
''' % vars)

    if not fields:
        return ''

    return '''static bool
%(struct_name)s_to_repr_vec(void *data, struct sol_oic_map_writer *repr_map)
{
    struct %(struct_name)s *state = (struct %(struct_name)s *)data;
    int r;

    %(fields)s

    return true;
}
''' % {
    'type': 'client' if client else 'server',
    'struct_name': name,
    'fields': '\n'.join(fields)
    }

def get_type_from_property(prop):
    if 'type' in prop:
        return prop['type']
    if 'enum' in prop:
        return 'enum:%s' % ','.join(prop['enum'])
    raise ValueError('Unknown type for property')

def all_props_are_read_only(props):
    for prop_name, prop_descr in props.items():
        if not prop_descr['read_only']:
            return False

    return True


def object_to_repr_vec_fn_common_c(state_struct_name, name, props, client, equivalent={}):
    if client and all_props_are_read_only(props):
        return '';

    for item_name, item_props in equivalent.items():
        if item_props[0] == client and props_are_equivalent(props, item_props[1]):
            return '''static bool
%(struct_name)s_to_repr_vec(void *data, struct sol_oic_map_writer *repr_map_encoder)
{
    return %(item_name)s_to_repr_vec(data, repr_map_encoder); /* %(item_name)s is equivalent to %(struct_name)s */
}
''' % {
        'item_name': item_name,
        'struct_name': name,
        'type': 'client' if client else 'server'
    }

    equivalent[name] = (client, props)
    return generate_object_to_repr_vec_fn_common_c(state_struct_name, name, props, client)

def object_to_repr_vec_fn_client_c(state_struct_name, name, props):
    return object_to_repr_vec_fn_common_c(state_struct_name, name, props, True)

def object_to_repr_vec_fn_server_c(state_struct_name, name, props):
    return object_to_repr_vec_fn_common_c(state_struct_name, name, props, False)

def get_field_integer_client_c(id, name, prop):
    return '''
        if (decode_mask & (1<<%(id)d) && streq(field.key, "%(field_name)s")) {
            if (field.type == SOL_OIC_REPR_TYPE_UINT)
                fields.%(field_name)s = field.v_uint;
            else if (field.type == SOL_OIC_REPR_TYPE_INT)
                fields.%(field_name)s = field.v_int;
            else if (field.type == SOL_OIC_REPR_TYPE_SIMPLE)
                fields.%(field_name)s = field.v_simple;
            else
                RETURN_ERROR(-EINVAL);
            decode_mask &= ~(1<<%(id)d);
            continue;
        }
''' % {
        'field_name': name,
        'field_name_len': len(name),
        'id': id
    }

def get_field_number_client_c(id, name, prop):
    return '''
        if (decode_mask & (1<<%(id)d) && streq(field.key, "%(field_name)s")) {
            if (field.type == SOL_OIC_REPR_TYPE_DOUBLE)
                fields.%(field_name)s = field.v_double;
            else if (field.type == SOL_OIC_REPR_TYPE_FLOAT)
                fields.%(field_name)s = field.v_float;
            else
                RETURN_ERROR(-EINVAL);
            decode_mask &= ~(1<<%(id)d);
            continue;
        }
''' % {
        'field_name': name,
        'field_name_len': len(name),
        'id': id
    }

def get_field_string_client_c(id, name, prop):
    return '''
        if (decode_mask & (1<<%(id)d) && streq(field.key, "%(field_name)s")) {
            if (field.type != SOL_OIC_REPR_TYPE_TEXT_STRING)
                RETURN_ERROR(-EINVAL);
            if (sol_util_replace_str_from_slice_if_changed(&fields.%(field_name)s, field.v_slice) < 0)
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
    return '''
        if (decode_mask & (1<<%(id)d) && streq(field.key, "%(field_name)s")) {
            if (field.type != SOL_OIC_REPR_TYPE_BOOLEAN)
                RETURN_ERROR(-EINVAL);
            fields.%(field_name)s = field.v_boolean;
            decode_mask &= ~(1<<%(id)d);
            continue;
        }
''' % {
        'field_name': name,
        'field_name_len': len(name),
        'id': id
    }

def get_field_enum_client_c(id, struct_name, name, prop):
    return '''
        if (decode_mask & (1<<%(id)d) && streq(field.key, "%(field_name)s")) {
            int val;

            if (field.type != SOL_OIC_REPR_TYPE_TEXT_STRING)
                RETURN_ERROR(-EINVAL);

            val = sol_str_table_lookup_fallback(%(struct_name)s_%(field_name)s_tbl,
                field.v_slice, -1);
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

def object_fields_from_repr_vec(name, props):
    fields = []
    type_to_fn = {
        'string': get_field_string_client_c,
        'integer': get_field_integer_client_c,
        'number': get_field_number_client_c,
        'boolean': get_field_boolean_client_c,
    }

    for id, (prop_name, prop) in enumerate(props.items()):
        if 'enum' in prop:
            fields.append(get_field_enum_client_c(id, name, prop_name, prop))
        else:
            fields.append(type_to_fn[prop['type']](id, prop_name, prop))
    return '\n'.join(fields)

def generate_object_from_repr_vec_fn_common_c(name, props):
    fields_init = []
    for field_name, field_props in props.items():
        if 'enum' in field_props:
            fields_init.append('        .%(name)s = state->%(name)s,' % {"name": field_name})
        elif field_props['type'] == 'string':
            fields_init.append('        .%(name)s = state->%(name)s ? strdup(state->%(name)s) : NULL,' % {"name": field_name})
        else:
            fields_init.append('        .%(name)s = state->%(name)s,' % {"name": field_name})

    fields_free = []
    for field_name, field_props in props.items():
        if 'enum' in field_props:
            continue
        if field_props.get('type') == 'string':
            fields_free.append('    free(fields.%s);' % (field_name))

    update_state = []
    for field_name, field_props in props.items():
        if not 'enum' in field_props:
            if field_props.get('type') == 'string':
                update_state.append("""\
    if (check_updated_string(state->%(name)s, fields.%(name)s)) {
        free(state->%(name)s);\
""" % {"name": field_name})

            else:
                update_state.append("""\
    if (%(c_check_updated)s(state->%(name)s, fields.%(name)s)) {\
""" % {"name": field_name,
       "c_check_updated": JSON_TO_FLOW_CHECK_UPDATED[field_props['type']]})
        else:
            update_state.append("""\
    if (%(c_check_updated)s(state->%(name)s, fields.%(name)s)) {\
""" % {"name": field_name,
       "c_check_updated": JSON_TO_FLOW_CHECK_UPDATED["integer"]})

        update_state.append("""\
        state->%(name)s = fields.%(name)s;""" % {"name": field_name})
        if not 'enum' in field_props and field_props.get('type') == 'string':
            update_state.append("""\
        fields.%(name)s = NULL;""" % {"name": field_name})
        update_state.append("""\
        updated = true;
    }
""")

    return '''static int
%(struct_name)s_from_repr_vec(struct %(struct_name)s *state,
    const struct sol_oic_map_reader *repr_vec, uint32_t decode_mask)
{
    struct sol_oic_repr_field field;
    enum sol_oic_map_loop_status end_status;
    struct sol_oic_map_reader iterator;
    struct %(struct_name)s fields = {
%(fields_init)s
    };
    bool updated = false;
    int ret = 0;

    SOL_OIC_MAP_LOOP(repr_vec, &field, &iterator, end_status) {
%(fields)s
    }
    if (end_status != SOL_OIC_MAP_LOOP_OK)
        goto out;

%(update_state)s

    ret = updated ? 1 : 0;

out:
%(free_fields)s
    return ret;
}
''' % {
        'struct_name': name,
        'fields_init': '\n'.join(fields_init),
        'fields': object_fields_from_repr_vec(name, props),
        'free_fields': '\n'.join(fields_free),
        'update_state': '\n'.join(update_state)
    }

def object_from_repr_vec_fn_common_c(name, props, equivalent={}):
    for item_name, item_props in equivalent.items():
        if props_are_equivalent(props, item_props):
            return '''static int
%(struct_name)s_from_repr_vec(struct %(struct_name)s *state,
    const struct sol_oic_map_reader *repr_map, uint32_t decode_mask)
{
    /* %(item_name)s is equivalent to %(struct_name)s */
    return %(item_name)s_from_repr_vec((struct %(item_name)s *)state, repr_map, decode_mask);
}
''' % {
        'item_name': item_name,
        'struct_name': name
    }

    equivalent[name] = props
    return generate_object_from_repr_vec_fn_common_c(name, props)


def object_from_repr_vec_fn_client_c(state_struct_name, name, props):
    return '''static int
%(struct_name)s_from_repr_vec(struct client_resource *resource, const struct sol_oic_map_reader *repr_vec)
{
    struct %(struct_name)s *res = (struct %(struct_name)s *)resource;
    return %(state_struct_name)s_from_repr_vec(&res->state, repr_vec, ~0);
}
''' % {
        'struct_name': name,
        'state_struct_name': state_struct_name
    }

def object_from_repr_vec_fn_server_c(state_struct_name, name, props):
    decode_mask = 0
    id = 0
    for field_name, field_props in props.items():
        if not field_props['read_only']:
            decode_mask |= 1<<id
        id += 1

    if not decode_mask:
        return ''

    return '''static int
%(struct_name)s_from_repr_vec(struct server_resource *resource, const struct sol_oic_map_reader *repr_vec)
{
    struct %(struct_name)s *res = (struct %(struct_name)s *)resource;
    return %(state_struct_name)s_from_repr_vec(&res->state, repr_vec, 0x%(decode_mask)x);
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

        send_flow_pkts.append('''%(flow_send_fn)s(resource->node, SOL_FLOW_NODE_TYPE_%(STRUCT_NAME)s__OUT__%(FIELD_NAME)s, %(val)s);''' % {
            'flow_send_fn': fn,
            'STRUCT_NAME': name.upper(),
            'FIELD_NAME': get_port_name(field_name),
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
        to_repr_vec_fn = 'NULL'
    else:
        to_repr_vec_fn = '%s_to_repr_vec' % name

    return '''static int
%(struct_name)s_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_%(struct_name)s_options *node_opts =
        (const struct sol_flow_node_type_%(struct_name)s_options *)options;
    static const struct client_resource_funcs funcs = {
        .to_repr_vec = %(to_repr_vec_fn)s,
        .from_repr_vec = %(struct_name)s_from_repr_vec,
        .inform_flow = %(struct_name)s_inform_flow,
        .found_port = SOL_FLOW_NODE_TYPE_%(STRUCT_NAME)s__OUT__FOUND,
        .device_id_port = SOL_FLOW_NODE_TYPE_%(STRUCT_NAME)s__OUT__DEVICE_ID
    };
    struct %(struct_name)s *resource = data;
    int r;

    r = client_resource_init(node, &resource->base, "%(resource_type)s", &funcs);
    SOL_INT_CHECK(r, < 0, r);
    %(field_init)s

    return client_connect(&resource->base, node_opts->device_id);
}
''' % {
        'struct_name': name,
        'STRUCT_NAME': get_port_name(name),
        'resource_type': resource_type,
        'field_init': '\n'.join(field_init),
        'to_repr_vec_fn': to_repr_vec_fn
    }

def object_open_fn_server_c(state_struct_name, resource_type, name, props, definitions={'id':0}):
    def_id = definitions['id']
    definitions['id'] += 1

    no_inputs = all(field_props['read_only'] for field_name, field_props in props.items())
    if no_inputs:
        from_repr_vec_fn_name = 'NULL'
        inform_flow_fn_name = 'NULL'
    else:
        from_repr_vec_fn_name = '%s_from_repr_vec' % name
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
    static const struct server_resource_funcs funcs = {
        .to_repr_vec = %(struct_name)s_to_repr_vec,
        .from_repr_vec = %(from_repr_vec_fn_name)s,
        .inform_flow = %(inform_flow_fn_name)s
    };
    struct %(struct_name)s *resource = data;
    int r;

    r = server_resource_init(&resource->base, node, rt_slice, &funcs);
    if (!r) {
        %(field_init)s
    }

    return r;
}
''' % {
        'struct_name': name,
        'resource_type': resource_type,
        'def_id': def_id,
        'from_repr_vec_fn_name': from_repr_vec_fn_name,
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

        elif descr['type'] == 'string':
            fields.append('''static int
%(struct_name)s_set_%(field_name)s(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct %(struct_name)s *resource = data;
    const char *var;
    int r;

    r = sol_flow_packet_get_string(packet, &var);
    if (!r) {
        r = sol_util_replace_str_if_changed(&resource->state.%(field_name)s, var);
        SOL_INT_CHECK(r, < 0, r);
        if (r > 0) {
            %(type)s_resource_schedule_update(&resource->base);
            r = 0;
        }
    }
    return r;
}
''' % {
        'struct_name': name,
        'field_name': field,
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
        if (%(c_check_updated)s(resource->state.%(field_name)s, (%(c_type)s) var)) {
            resource->state.%(field_name)s = (%(c_type)s) var;
            %(type)s_resource_schedule_update(&resource->base);
        }
    }
    return r;
}
''' % {
        'struct_name': name,
        'field_name': field,
        'c_type': JSON_TO_C[descr['type']],
        'c_type_tmp': JSON_TO_C_TMP[descr['type']],
        'c_getter': JSON_TO_FLOW_GET_PKT[descr['type']],
        'c_check_updated': JSON_TO_FLOW_CHECK_UPDATED[descr['type']],
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
            if 'short_description' in descr:
                output.append('''/* %s */''' % descr['short_description'])
            output.append('''enum %(struct_name)s_%(field_name)s { %(items)s };''' % {
                'struct_name': name,
                'field_name': field,
                'items': ', '.join(('%s_%s_%s' % (name, field, remove_special_chars(item))).upper() for item in descr['enum'])
            })

            output.append('''static const struct sol_str_table %(struct_name)s_%(field_name)s_tbl[] = {
    %(items)s,
    { }
};''' % {
                'struct_name': name,
                'field_name': field,
                'items': ',\n'.join('SOL_STR_TABLE_ITEM(\"%s\", %s_%s_%s)' % (
                    remove_special_chars(item), name.upper(), field.upper(), remove_special_chars(item).upper()) for item in descr['enum'])
            })

    return '\n'.join(output)

def generate_object_client_c(resource_type, state_struct_name, name, props):
    return """struct %(struct_name)s {
    struct client_resource base;
    struct %(state_struct_name)s state;
};

%(to_repr_vec_fn)s
%(from_repr_vec_fn)s
%(inform_flow_fn)s
%(open_fn)s
%(close_fn)s
%(setters_fn)s
""" % {
    'state_struct_name': state_struct_name,
    'struct_name': name,
    'to_repr_vec_fn': object_to_repr_vec_fn_client_c(state_struct_name, name, props),
    'from_repr_vec_fn': object_from_repr_vec_fn_client_c(state_struct_name, name, props),
    'inform_flow_fn': object_inform_flow_fn_client_c(state_struct_name, name, props),
    'open_fn': object_open_fn_client_c(state_struct_name, resource_type, name, props),
    'close_fn': object_close_fn_client_c(name, props),
    'setters_fn': object_setters_fn_client_c(state_struct_name, name, props),
    }

def generate_object_server_c(resource_type, state_struct_name, name, props):
    return """struct %(struct_name)s {
    struct server_resource base;
    struct %(state_struct_name)s state;
};

%(to_repr_vec_fn)s
%(from_repr_vec_fn)s
%(inform_flow_fn)s
%(open_fn)s
%(close_fn)s
%(setters_fn)s
""" % {
    'struct_name': name,
    'state_struct_name': state_struct_name,
    'to_repr_vec_fn': object_to_repr_vec_fn_server_c(state_struct_name, name, props),
    'from_repr_vec_fn': object_from_repr_vec_fn_server_c(state_struct_name, name, props),
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
%(from_repr_vec_fn)s
""" % {
        'enums': generate_enums_common_c(name, props),
        'struct_name': name,
        'struct_fields': object_fields_common_c(name, name, props),
        'from_repr_vec_fn': object_from_repr_vec_fn_common_c(name, props),
    }

# handle port_name, portName and PortName
def get_port_name(name):
    return re.sub('(?!^)([A-Z]+)', r'_\1', name).upper()

# handle props name with '-' or other special chars
def remove_special_chars(name):
    return re.sub(r'[\W]+', '_', name)

def generate_object_json(resource_type, struct_name, node_name, title, props, server):
    if server:
        in_ports = []
    else:
        in_ports = [{
            'data_type': 'any',
            'description':
                'Scan all reachable resources that matches the interface. '
                'Packets with IDs are sent through output port DEVICE_ID.',
            'methods': {
                'process': 'scan'
            },
            'name': 'SCAN'
        },
        {
            'data_type': 'string',
            'description':
                'Set current server device ID to connect to. Override device ID set in device_id option.',
            'methods': {
                'process': 'device_id_process'
            },
            'name': 'DEVICE_ID'
        }]

    for prop_name, prop_descr in props.items():
        if not server and prop_descr['read_only']:
            continue

        in_ports.append({
            'data_type': JSON_TO_SOL_JSON[prop_descr.get('type', 'string')],
            'description': prop_descr.get('short_description', '???'),
            'methods': {
                'process': '%s_set_%s' % (struct_name, prop_name)
            },
            'name': '%s' % get_port_name(prop_name)
        })

    if server:
        out_ports = []
    else:
        out_ports = [{
            'data_type': 'boolean',
            'description': 'Outputs true if resource was found, false if not, or if unreachable',
            'name': 'FOUND'
        },
        {
            'data_type': 'string',
            'description': 'Send packets with IDs for all servers that respond to scan request. Such IDs can be used to connect to a client to a different server through input port DEVICE_ID',
            'name': 'DEVICE_ID'
        }]
    for prop_name, prop_descr in props.items():
        out_ports.append({
            'data_type': JSON_TO_SOL_JSON[prop_descr.get('type', 'string')],
            'description': prop_descr.get('short_description', '???'),
            'name': '%s' % get_port_name(prop_name)
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
                        'name': 'device_id',
                        'default': ''
                    }
                ]
            }
        })

    if in_ports:
        output['in_ports'] = in_ports
    if out_ports:
        output['out_ports'] = out_ports

    return output

def generate_object(rt, title, props, json_name):
    def type_value(item):
        return '%s %s' % (get_type_from_property(item[1]), item[0])

    resource_type = rt

    if rt.startswith('oic.r.'):
        rt = rt[len('oic.r.'):]
    elif rt.startswith('core.'):
        rt = rt[len('core.'):]

    c_identifier = rt.replace(".", "_").replace("-", "_").lower()
    c_json_name = json_name.replace(".", "_").replace("-", "_").lower()
    flow_identifier = rt.replace(".", "-").replace("_", "-").lower()
    flow_json_name = json_name.replace(".", "-").replace("_", "-").lower()

    client_node_name = "%s/client-%s" % (flow_json_name, flow_identifier)
    client_struct_name = "%s_client_%s" % (c_json_name, c_identifier)
    server_node_name = "%s/server-%s" % (flow_json_name, flow_identifier)
    server_struct_name = "%s_server_%s" % (c_json_name, c_identifier)
    state_struct_name = "%s_state_%s" % (c_json_name, c_identifier)

    new_props = OrderedDict()
    for k, v in sorted(props.items(), key=type_value):
        new_props[remove_special_chars(k)] = v
    props = new_props

    retval = {
        'c_common': generate_object_common_c(state_struct_name, props),
        'c_client': generate_object_client_c(resource_type, state_struct_name, client_struct_name, props),
        'c_server': generate_object_server_c(resource_type, state_struct_name, server_struct_name, props),
        'json_client': generate_object_json(resource_type, client_struct_name, client_node_name, title, props, False),
        'json_server': generate_object_json(resource_type, server_struct_name, server_node_name, title, props, True)
    }
    return retval

def generate_for_schema(directory, path, json_name):
    j = load_json_schema(directory, path)

    for rt, defn in j.items():
        if not (rt.startswith("oic.r.") or rt.startswith("core.")):
            raise ValueError("not an OIC resource definition")

        if defn.get('type') == 'object':
            yield generate_object(rt, defn['title'], defn['properties'],
                    json_name)

def master_json_as_string(generated, json_name):
    master_json = {
        '$schema': 'http://solettaproject.github.io/soletta/schemas/node-type-genspec.schema',
        'name': json_name,
        'meta': {
            'author': 'Intel Corporation',
            'license': 'Apache-2.0',
            'version': '1'
        },
        'types': [t['json_server'] for t in generated] + [t['json_client'] for t in generated]
    }
    return json.dumps(master_json, indent=4)

def master_c_as_string(generated, oic_gen_c, oic_gen_h):
    generated = list(generated)
    code = '''
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "%(oic_gen_h)s"

#include "sol-coap.h"
#include "sol-mainloop.h"
#include "sol-oic.h"
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

#define DEVICE_ID_LEN (16)

#define streq(a, b) (strcmp((a), (b)) == 0)
#define likely(x)   __builtin_expect(!!(x), 1)

struct client_resource;
struct server_resource;

struct client_resource_funcs {
    bool (*to_repr_vec)(void *data, struct sol_oic_map_writer *repr_map);
    int (*from_repr_vec)(struct client_resource *resource, const struct sol_oic_map_reader *repr_vec);
    void (*inform_flow)(struct client_resource *resource);
    int found_port;
    int device_id_port;
};

struct server_resource_funcs {
    bool (*to_repr_vec)(void *data, struct sol_oic_map_writer *repr_map);
    int (*from_repr_vec)(struct server_resource *resource, const struct sol_oic_map_reader *repr);
    void (*inform_flow)(struct server_resource *resource);
};

struct client_resource {
    struct sol_flow_node *node;
    const struct client_resource_funcs *funcs;

    struct sol_oic_resource *resource;

    struct pending {
        struct sol_oic_pending *ipv4, *ipv6_local, *ipv6_site;
    } discover_pending, scan_pending;
    /* discover registered device IDs or scan for unknown IDs */

    struct sol_timeout *find_timeout;
    struct sol_timeout *update_schedule_timeout;

    struct sol_oic_client *client;

    const char *rt;
    char device_id[DEVICE_ID_LEN];
    struct sol_ptr_vector scanned_ids;
};

struct server_resource {
    struct sol_flow_node *node;
    const struct server_resource_funcs *funcs;

    struct sol_oic_server_resource *resource;
    struct sol_timeout *update_schedule_timeout;

    struct sol_oic_resource_type type;
};

static struct sol_network_link_addr multicast_ipv4, multicast_ipv6_local, multicast_ipv6_site;

static bool
initialize_multicast_addresses_once(void)
{
    static bool multicast_addresses_initialized = false;

    if (multicast_addresses_initialized)
        return true;

    multicast_ipv4 = (struct sol_network_link_addr) { .family = SOL_NETWORK_FAMILY_INET, .port = DEFAULT_UDP_PORT };
    if (!sol_network_link_addr_from_str(&multicast_ipv4, MULTICAST_ADDRESS_IPv4)) {
        SOL_WRN("Could not parse multicast IP address");
        multicast_ipv4.family = SOL_NETWORK_FAMILY_UNSPEC;
    }
    multicast_ipv6_local = (struct sol_network_link_addr) { .family = SOL_NETWORK_FAMILY_INET6, .port = DEFAULT_UDP_PORT };
    if (!sol_network_link_addr_from_str(&multicast_ipv6_local, MULTICAST_ADDRESS_IPv6_LOCAL)) {
        SOL_WRN("Could not parse multicast IP address");
        return false;
    }
    multicast_ipv6_site = (struct sol_network_link_addr) { .family = SOL_NETWORK_FAMILY_INET6, .port = DEFAULT_UDP_PORT };
    if (!sol_network_link_addr_from_str(&multicast_ipv6_site, MULTICAST_ADDRESS_IPv6_SITE)) {
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
state_changed(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *oic_cli, const struct sol_network_link_addr *cliaddr,
    const struct sol_oic_map_reader *repr_vec)
{
    struct client_resource *resource = data;

    if (!cliaddr || !repr_vec)
        return;

    if (!sol_network_link_addr_eq(cliaddr, &resource->resource->addr)) {
        SOL_BUFFER_DECLARE_STATIC(resaddr, SOL_INET_ADDR_STRLEN);
        SOL_BUFFER_DECLARE_STATIC(respaddr, SOL_INET_ADDR_STRLEN);

        if (!sol_network_link_addr_to_str(&resource->resource->addr, &resaddr)) {
            SOL_WRN("Could not convert network address to string");
            return;
        }
        if (!sol_network_link_addr_to_str(cliaddr, &respaddr)) {
            SOL_WRN("Could not convert network address to string");
            return;
        }

        SOL_WRN("Expecting response from %%.*s, got from %%.*s, ignoring",
            SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&resaddr)),
            SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&respaddr)));
        return;
    }

    if (resource->funcs->from_repr_vec(resource, repr_vec) > 0)
        resource->funcs->inform_flow(resource);
}

static bool
found_resource(void *data, struct sol_oic_client *oic_cli, struct sol_oic_resource *oic_res)
{
    struct client_resource *resource = data;
    int r;

    if (!oic_res) {
        SOL_WRN("resource discovery timeout");
        goto cancel;
    }

    /* Some OIC device sent this node a discovery response packet but node's already set up. */
    if (resource->resource) {
        SOL_DBG("Received discovery packet when resource already set up, ignoring");
        goto cancel;
    }

    if (memcmp(oic_res->device_id.data, resource->device_id, 16) != 0) {
        /* Not the droid we're looking for. */
        SOL_DBG("Received resource with an unknown device_id, ignoring");
        return true;
    }

    /* FIXME: Should this check move to sol-oic-client? Does it actually make sense? */
    if (resource->rt && !client_resource_implements_type(oic_res, resource->rt)) {
        SOL_DBG("Received resource that does not implement rt=%%s, ignoring", resource->rt);
        return true;
    }

    SOL_INF("Found resource matching device_id");
    resource->resource = sol_oic_resource_ref(oic_res);

    if (sol_oic_client_resource_set_observable(oic_cli, resource->resource,
        state_changed, resource, true) < 0) {
        SOL_WRN("Could not observe resource as requested, will try again");
    }

    r = sol_flow_send_boolean_packet(resource->node,
        resource->funcs->found_port, true);
    if (r < 0)
        SOL_WRN("Could not send flow packet, will try again");

cancel:
    resource->find_timeout = NULL;
    memset(&resource->discover_pending, 0, sizeof(resource->discover_pending));
    return false;
}

static void
find_resources(struct client_resource *resource,
    bool (*resource_found_cb)(void *data,
    struct sol_oic_client *cli,
    struct sol_oic_resource *res),
    bool scan)
{
    struct pending *pending;

    if (scan)
        pending = &resource->scan_pending;
    else
        pending = &resource->discover_pending;

    if (multicast_ipv4.family != SOL_NETWORK_FAMILY_UNSPEC) {
        if (pending->ipv4)
            sol_oic_pending_cancel(pending->ipv4);
        pending->ipv4 = sol_oic_client_find_resources(resource->client,
            &multicast_ipv4, resource->rt, NULL, resource_found_cb, resource);
    }

    if (pending->ipv6_local)
        sol_oic_pending_cancel(pending->ipv6_local);
    pending->ipv6_local = sol_oic_client_find_resources(resource->client,
        &multicast_ipv6_local, resource->rt, NULL, resource_found_cb, resource);

    if (pending->ipv6_site)
        sol_oic_pending_cancel(pending->ipv6_site);
    pending->ipv6_site = sol_oic_client_find_resources(resource->client,
        &multicast_ipv6_site, resource->rt, NULL, resource_found_cb, resource);
}

static void
send_discovery_packets(struct client_resource *resource)
{
    if (resource->resource)
        return;

    sol_flow_send_boolean_packet(resource->node, resource->funcs->found_port, false);

    find_resources(resource, found_resource, false);
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

static inline char
base16_encode_digit(const uint8_t nibble, const char a)
{
    if (likely(nibble < 10))
        return '0' + nibble;
    return a + (nibble - 10);
}

static void
binary_to_hex_ascii(const char *binary, char *ascii)
{
    const uint8_t *input = (const uint8_t *)binary;
    size_t i, o = 0;

    for (i = 0; i < DEVICE_ID_LEN; i++) {
        const uint8_t b = input[i];
        uint8_t n;

        const uint8_t nibble[2] = {
            (b & 0xf0) >> 4,
            (b & 0x0f)
        };

        for (n = 0; n < 2; n++)
            ascii[o++] = base16_encode_digit(nibble[n], 'a');
    }

    ascii[o] = 0;
}

static bool
scan_callback(void *data, struct sol_oic_client *oic_cli, struct sol_oic_resource *oic_res)
{
    struct client_resource *resource = data;
    char ascii[DEVICE_ID_LEN * 2 + 1];
    char *id;
    uint16_t i;
    int r;

    if (!oic_res) {
        SOL_WRN("Scanning timeout");
        goto cancel;
    }

    /* FIXME: Should this check move to sol-oic-client? Does it actually make sense? */
    if (resource->rt && !client_resource_implements_type(oic_res, resource->rt)) {
        SOL_DBG("Received resource that does not implement rt=%%s, ignoring", resource->rt);
        return true;
    }

    SOL_PTR_VECTOR_FOREACH_IDX(&resource->scanned_ids, id, i)
        if (memcmp(id, oic_res->device_id.data, DEVICE_ID_LEN) == 0)
            return true;

    id = malloc(DEVICE_ID_LEN);
    SOL_NULL_CHECK(id, true);
    memcpy(id, oic_res->device_id.data, DEVICE_ID_LEN);
    r = sol_ptr_vector_append(&resource->scanned_ids, id);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    binary_to_hex_ascii(oic_res->device_id.data, ascii);

    r = sol_flow_send_string_packet(resource->node,
        resource->funcs->device_id_port, ascii);
    if (r < 0)
        SOL_WRN("Could not send server id.");

    return true;

error:
    SOL_WRN("Failed to process id.");
    free(id);
    return true;

cancel:
    memset(&resource->discover_pending, 0, sizeof(resource->discover_pending));
    return false;
}

static void
clear_scanned_ids(struct sol_ptr_vector *scanned_ids)
{
    char *id;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX(scanned_ids, id, i)
        free(id);
    sol_ptr_vector_clear(scanned_ids);
}

static void
send_scan_packets(struct client_resource *resource)
{
    clear_scanned_ids(&resource->scanned_ids);
    find_resources(resource, scan_callback, true);
}

static bool
server_resource_perform_update(void *data)
{
    struct server_resource *resource = data;
    struct sol_oic_response *notification;

    SOL_NULL_CHECK(resource->funcs->to_repr_vec, false);

    notification = sol_oic_server_notification_new(resource->resource);
    SOL_NULL_CHECK(notification, false);
    if (!resource->funcs->to_repr_vec(resource,
        sol_oic_server_response_get_writer(notification))) {
        sol_oic_server_response_free(notification);
        return false;
    }

    if (sol_oic_server_notify(notification) < 0) {
        SOL_WRN("Error while serializing update message");
    } else {
        resource->funcs->inform_flow(resource);
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

static int
server_handle_update(void *data, struct sol_oic_request *request)
{
    enum sol_coap_response_code code;
    struct server_resource *resource = (struct server_resource *)data;
    struct sol_oic_map_reader *input;
    int r;

    if (!resource->funcs->from_repr_vec) {
        code = SOL_COAP_RESPONSE_CODE_NOT_IMPLEMENTED;
        goto end;
    }

    input = sol_oic_server_request_get_reader(request);
    r = resource->funcs->from_repr_vec(resource, input);
    if (r > 0) {
        server_resource_schedule_update(resource);
        code = SOL_COAP_RESPONSE_CODE_CHANGED;
    } else if (r == 0)
        code = SOL_COAP_RESPONSE_CODE_OK;
    else
        code = SOL_COAP_RESPONSE_CODE_PRECONDITION_FAILED;

end:
    return sol_oic_server_send_response(request, NULL, code);
}

static int
server_handle_get(void *data, struct sol_oic_request *request)
{
    const struct server_resource *resource = data;
    struct sol_oic_map_writer *output;
    struct sol_oic_response *response;

    if (!resource->funcs->to_repr_vec)
        return sol_oic_server_send_response(request, NULL,
            SOL_COAP_RESPONSE_CODE_NOT_IMPLEMENTED);

    response = sol_oic_server_response_new(request);
    SOL_NULL_CHECK_GOTO(response, error);
    output = sol_oic_server_response_get_writer(response);
    SOL_NULL_CHECK_GOTO(output, error);
    if (!resource->funcs->to_repr_vec((void *)resource, output))
        goto error;

    return sol_oic_server_send_response(request, response,
        SOL_COAP_RESPONSE_CODE_CONTENT);

error:
    sol_oic_server_response_free(response);
    return sol_oic_server_send_response(request, NULL,
        SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR);
}

// log_init() implementation happens within oic-gen.c
static void log_init(void);

static int
server_resource_init(struct server_resource *resource, struct sol_flow_node *node,
    struct sol_str_slice resource_type, const struct server_resource_funcs *funcs)
{
    log_init();

    resource->node = node;
    resource->update_schedule_timeout = NULL;
    resource->funcs = funcs;

    resource->type = (struct sol_oic_resource_type) {
        SOL_SET_API_VERSION(.api_version = SOL_OIC_RESOURCE_TYPE_API_VERSION, )
        .resource_type = resource_type,
        .interface = SOL_STR_SLICE_LITERAL("oc.mi.def"),
        .get = { .handle = server_handle_get },
        .put = { .handle = server_handle_update },
        .post = { .handle = server_handle_update },
    };

    resource->resource = sol_oic_server_register_resource(&resource->type,
        resource, SOL_OIC_FLAG_DISCOVERABLE | SOL_OIC_FLAG_OBSERVABLE | SOL_OIC_FLAG_ACTIVE);
    if (resource->resource)
        return 0;

    return -EINVAL;
}

static void
server_resource_close(struct server_resource *resource)
{
    if (resource->update_schedule_timeout)
        sol_timeout_del(resource->update_schedule_timeout);
    sol_oic_server_unregister_resource(resource->resource);
}

static unsigned int
as_nibble(const char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    SOL_WRN("Invalid hex character: %%d", c);
    return 0;
}

static void
hex_ascii_to_binary(const char *ascii, char *binary)
{
    const char *p;
    size_t i;

    for (p = ascii, i = 0; i < DEVICE_ID_LEN; i++, p += 2)
        binary[i] = as_nibble(*p) << 4 | as_nibble(*(p + 1));
}

static int
client_connect(struct client_resource *resource, const char *device_id)
{
    if (!device_id || strlen(device_id) != 32) {
        SOL_DBG("Invalid or empty device_id. Not trying to connect.");
        return 0;
    }

    hex_ascii_to_binary(device_id, resource->device_id);
    SOL_NULL_CHECK(resource->device_id, -ENOMEM);

    if (resource->find_timeout)
        sol_timeout_del(resource->find_timeout);

    if (resource->resource) {
        if (sol_oic_client_resource_set_observable(resource->client,
            resource->resource, NULL, NULL, false) < 0) {
            SOL_WRN("Could not unobserve resource");
        }

        sol_oic_resource_unref(resource->resource);
        resource->resource = NULL;
    }

    SOL_INF("Sending multicast packets to find resource with device_id %%s (rt=%%s)",
        device_id, resource->rt);
    resource->find_timeout = sol_timeout_add(FIND_PERIOD_MS, find_timer, resource);
    if (resource->find_timeout) {
        /* Perform a find now instead of waiting FIND_PERIOD_MS the first time.  If the
         * resource is found in the mean time, the timeout will be automatically disabled. */
        send_discovery_packets(resource);
        return 0;
    }

    SOL_ERR("Could not create timeout to find resource");
    return -ENOMEM;
}

static int
client_resource_init(struct sol_flow_node *node, struct client_resource *resource, const char *resource_type,
    const struct client_resource_funcs *funcs)
{
    log_init();

    if (!initialize_multicast_addresses_once()) {
        SOL_ERR("Could not initialize multicast addresses");
        return -ENOTCONN;
    }

    assert(resource_type);

    resource->client = sol_oic_client_new();
    SOL_NULL_CHECK(resource->client, -ENOMEM);

    sol_ptr_vector_init(&resource->scanned_ids);
    resource->node = node;
    resource->find_timeout = NULL;
    resource->update_schedule_timeout = NULL;
    resource->resource = NULL;
    resource->funcs = funcs;
    resource->rt = resource_type;

    return 0;
}

static void
pending_free(struct pending *pending)
{
    if (pending->ipv4)
        sol_oic_pending_cancel(pending->ipv4);
    if (pending->ipv6_local)
        sol_oic_pending_cancel(pending->ipv6_local);
    if (pending->ipv6_site)
        sol_oic_pending_cancel(pending->ipv6_site);
}

static void
client_resource_close(struct client_resource *resource)
{
    if (resource->find_timeout)
        sol_timeout_del(resource->find_timeout);
    if (resource->update_schedule_timeout)
        sol_timeout_del(resource->update_schedule_timeout);

    pending_free(&resource->scan_pending);
    pending_free(&resource->discover_pending);

    if (resource->resource) {
        if (sol_oic_client_resource_set_observable(resource->client,
            resource->resource, NULL, NULL, false) < 0)
            SOL_WRN("Could not unobserve resource");

        sol_oic_resource_unref(resource->resource);
    }

    clear_scanned_ids(&resource->scanned_ids);
    sol_oic_client_del(resource->client);
}

static void
client_resource_update_ack(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec)
{
    struct client_resource *resource = data;

    resource->funcs->inform_flow(resource);
}

static bool
client_resource_perform_update(void *data)
{
    struct client_resource *resource = data;
    struct sol_oic_request *request;
    struct sol_oic_pending *pending;

    SOL_NULL_CHECK_GOTO(resource->resource, disable_timeout);
    SOL_NULL_CHECK_GOTO(resource->funcs->to_repr_vec, disable_timeout);

    request = sol_oic_client_request_new(SOL_COAP_METHOD_PUT, resource->resource);
    if (!request ||
        !resource->funcs->to_repr_vec(resource,
        sol_oic_client_request_get_writer(request))) {
        SOL_WRN("Failed to create request. Will try again");
        return true;
    }

    pending = sol_oic_client_request(resource->client, request,
        client_resource_update_ack, data);
    if (!pending) {
        SOL_WRN("Could not send update request to resource, will try again");
        return true;
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

static int
scan(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_resource *resource = data;

    send_scan_packets(resource);

    return 0;
}

static int
device_id_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_resource *resource = data;
    const char *device_id;
    int r;

    r = sol_flow_packet_get_string(packet, &device_id);
    SOL_INT_CHECK(r, < 0, r);

    return client_connect(resource, device_id);
}

static inline bool
check_updated_string(const char *a, const char *b)
{
    if (a && b)
        return strcmp(a, b) != 0;
    else if ((a && !b) || (!a && b))
       return true;
    else
       return false;
}

static inline bool
check_updated_int32(const int32_t a, const int32_t b)
{
    return a != b;
}

static inline bool
check_updated_boolean(const bool a, const bool b)
{
    return a != b;
}

static inline bool
check_updated_number(const double a, const double b)
{
    return !sol_util_double_equal(a, b);
}

static inline int
send_string_packet(struct sol_flow_node *src, uint16_t src_port, const char *value)
{
    return sol_flow_send_string_packet(src, src_port, value ? value : "");
}

#define RETURN_ERROR(errcode) do { ret = errcode; goto out; } while(0)

%(generated_c_common)s
%(generated_c_client)s
%(generated_c_server)s

#undef RETURN_ERROR

#include "%(oic_gen_c)s"
''' % {
        'generated_c_common': '\n'.join(t['c_common'] for t in generated),
        'generated_c_client': '\n'.join(t['c_client'] for t in generated),
        'generated_c_server': '\n'.join(t['c_server'] for t in generated),
        'oic_gen_c': oic_gen_c,
        'oic_gen_h': oic_gen_h,
    }

    return code.replace('\n\n\n', '\n')

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("schema_dirs",
                        help="Directories where JSON schemas are located. "
                        "Names must start with 'oic.r.' or 'core.' and use "
                        "extension '.json'",
                        nargs="+")
    parser.add_argument("--node-type-json",
                        help="Path to store the master JSON with node type information",
                        required=True)
    parser.add_argument("--node-type-impl",
                        help="Path to store the node type implementation",
                        required=True)
    parser.add_argument("--node-type-gen-c",
                        help="Relative path to source generated with "
                        "sol-flow-node-type-gen for inclusion purposes.",
                        required=True)
    parser.add_argument("--node-type-gen-h",
                        help="Relative path to header generated with "
                        "sol-flow-node-type-gen for inclusion purposes.",
                        required=True)
    args = parser.parse_args()

    def seems_schema(path):
        # TODO properly handle update, batch and error files
        if path.endswith('-Update.json') or path.endswith('-Error.json') or \
            path.endswith('-Batch.json'):
            return False
        return path.endswith('.json') and (path.startswith('oic.r.') or path.startswith('core.'))

    json_name = os.path.basename(args.node_type_json)
    if json_name.endswith(".json"):
        json_name = json_name[:-5]

    generated = []
    print('Generating code for schemas: ', end='')
    for schema_dir in args.schema_dirs:
        for path in (f for f in sorted(os.listdir(schema_dir)) if seems_schema(f)):
            print(path, end=', ')

            try:
                for code in generate_for_schema(schema_dir, path, \
                        json_name):
                    generated.append(code)
            except KeyError as e:
                if e.args[0] in ('array', 'object'):
                    print("(%ss unsupported)" % e.args[0], end=' ')
                else:
                    raise e
            except Exception as e:
                print('Ignoring %s due to exception in generator. '
                      'Traceback follows:' % path, file=sys.stderr)
                traceback.print_exc(file=sys.stderr)
                continue

    print('\nWriting master JSON: %s' % args.node_type_json)
    open(args.node_type_json, 'w+', encoding='UTF-8').write(
            master_json_as_string(generated, json_name))

    print('Writing C: %s' % args.node_type_impl)
    open(args.node_type_impl, 'w+', encoding='UTF-8').write(
            master_c_as_string(generated, args.node_type_gen_c,
            args.node_type_gen_h))
    if os.path.exists('/usr/bin/indent'):
        print('Indenting generated C.')
        os.system("/usr/bin/indent -kr -l120 '%s'" % args.node_type_impl)

    print('Done.')
