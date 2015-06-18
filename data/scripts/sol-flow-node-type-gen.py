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

import json
import jsonschema
import os
import re
import sys

# NOTE: this function is replicated in other files, update all copies!
def json_load_and_check(data_file, schema_file, context_lines=3, schema_max_depth=2, check_schema=True):
    """Full check of JSON, with meaningful error messages.

    This function will open data_file and schema_file, doing full
    validation and returning the object on success, or raising an
    exception on failure.

    On failures, a meaningful message is printed taking previous
    context_lines in log messages, making people able to figure out
    what was wrong.

    Messages are printed in the standard format:

        file:line:column: message

    making it possible for other tools (ie: editors) to locate it for
    users.
    """

    # json is bad and does not provide location as exception fields,
    # but at least it uses 2 fixed formats in its error messages
    # see json/decoder.py errmsg()
    re_single_loc = re.compile(r"^(?P<msg>.*): line (?P<line>\d+) column (?P<column>\d+) [(]char .*[)]$")
    re_range_loc = re.compile(r"^(?P<msg>.*): line (?P<line_start>\d+) column (?P<column_start>\d+) - line (?P<line_end>\d+) column (?P<column_end>\d+) [(]char .*[)]$")

    def show_file_context(lines, filename, lineno, colno, linefmt_size=0):
        if linefmt_size < 1:
            linefmt_size = len("%d" % (lineno,))
        for i in range(max(lineno - context_lines, 0), lineno):
            sys.stderr.write("%s:%0*d: %s\n" %
                             (filename, linefmt_size, i + 1, lines[i]))
        sys.stderr.write("%s:%0*d: %s^\n" %
                         (filename, linefmt_size, lineno, '-' * (colno - 1)))

    def show_json_load_exception(exc, contents, filename):
        excstr = str(exc)
        re_match = re_range_loc.match(excstr)
        lines = contents.split('\n')
        if re_match:
            lineno_start = int(re_match.group("line_start"))
            colno_start = int(re_match.group("column_start"))
            lineno_end = int(re_match.group("line_end"))
            colno_end = int(re_match.group("column_end"))
            colfmt_size = len("%d" % (max(colno_start, colno_end),))
            linefmt_size = len("%d" % (lineno_end,))
            msg = re_match.group("msg")

            show_file_context(lines, filename, lineno_start, colno_start,
                              linefmt_size=linefmt_size)
            sys.stderr.write("%s:%0*d:%0*d: error: start of %s\n" % (
                filename,
                linefmt_size, lineno_start,
                colfmt_size, colno_start, msg))
            show_file_context(lines, filename, lineno_end, colno_end,
                              linefmt_size=linefmt_size)
            sys.stderr.write("%s:%0*d:%0*d: error: end of %s\n" % (
                filename,
                linefmt_size, lineno_end,
                colfmt_size, colno_end, msg))
            return

        re_match = re_single_loc.match(excstr)
        if re_match:
            lineno = int(re_match.group("line"))
            colno = int(re_match.group("column"))
            location = "%s:%d:%d" % (filename, lineno, colno)
            msg = re_match.group("msg")

            char = lines[lineno - 1][colno - 1]
            show_file_context(lines, filename, lineno, colno)
            sys.stderr.write("%s: error: %s\n" % (location, msg))

            if (msg == "Expecting property name enclosed in double quotes" and char == '}') \
               or (msg == "Expecting value" and char == ']'):
                sys.stderr.write("%s: error: maybe trailing ',' is dangling prior to closing braces?\n" % (location))
            return
        else:
            sys.stderr.write("%s: error: %s\n" % (filename,  excstr))
            return

    def load_json(file):
        contents = file.read()
        try:
            return json.loads(contents)
        except ValueError as e:
            show_json_load_exception(e, contents, file.name)
            raise

    def show_schema_exception(exc, filename):
        if not exc.context:
            sys.stderr.write("%s: %s\n" % (filename, exc.message))
            return

        def path_to_str(path, varname="json"):
            s = "%s" % (varname,)
            for p in path:
                s += '[%r]' % p
            return s

        def show_obj(msg, obj, abspath):
            abspathstr = path_to_str(abspath)
            if isinstance(obj, dict):
                sys.stderr.write("%s: %s at %s = {\n" %
                                 (filename, msg, abspathstr))
                for k in sorted(obj.keys()):
                    klen = len(k)
                    val = json.dumps(obj[k], sort_keys=True)
                    if len(val) + klen > 50:
                        maxlen = max(50 - klen, 10)
                        val = "%s...%s" % (val[:maxlen], val[-1])
                    sys.stderr.write("%s:    %r: %s\n" % (filename, k, val))
                sys.stderr.write("%s: }\n" % (filename,))
            elif isinstance(obj, list):
                sys.stderr.write("%s: %s at %s = [\n" %
                                 (filename, msg, abspathstr))
                fmtlen = len("%d" % len(obj))
                for i, val in enumerate(obj):
                    val = json.dumps(val, sort_keys=True)
                    if len(val) > 50:
                        val = "%s...%s" % (val[:50], val[-1])
                    sys.stderr.write("%s:   %0*d: %s\n" %
                                     (filename, fmtlen, i, val))
                sys.stderr.write("%s: ]\n" % (filename,))
            else:
                parent_path = list(abspath)[:-1]
                parent_obj = exc.instance
                for p in parent_path:
                    parent_obj = parent_obj[p]
                show_obj("parent of " + msg, parent_obj, parent_path)

        def show_schema(schemaobj, abspath):
            abspathstr = path_to_str(abspath)
            sys.stderr.write("%s: schema at %s:\n" % (filename, abspathstr))

            def show_list(lst, indent=1):
                if schema_max_depth > 0 and indent > schema_max_depth:
                    return
                indentstr = "  " * indent
                for i, v in enumerate(lst):
                    if isinstance(v, dict):
                        show_dict(v, indent + 1)
                    elif isinstance(v, list):
                        show_list(v, indent + 1)
                    else:
                        sys.stderr.write("%s: %s%r\n" % (filename, indentstr, v))

            def show_dict(obj, indent=1):
                if schema_max_depth > 0 and indent > schema_max_depth:
                    return
                indentstr = "  " * indent
                for k in sorted(obj.keys()):
                    sys.stderr.write("%s: %s%s: " % (filename, indentstr, k))
                    v = obj[k]
                    if isinstance(v, str) and k == "$ref":
                        with validator.resolver.resolving(v) as resolved:
                            sys.stderr.write("%s (expanded below)\n" % (v,))
                            show_dict(resolved, indent + 1)

                    elif isinstance(v, dict):
                        sys.stderr.write("\n")
                        show_dict(v, indent + 1)
                    elif isinstance(v, list):
                        sys.stderr.write("\n")
                        show_list(v, indent + 1)
                    else:
                        sys.stderr.write("%r\n" % (v,))

            for k in sorted(schemaobj.keys()):
                v = schemaobj[k]
                sys.stderr.write("%s:   %s: " % (filename, k))
                if isinstance(v, list):
                    sys.stderr.write("\n")
                    show_list(v)
                elif isinstance(v, dict):
                    sys.stderr.write("\n")
                    show_dict(v)
                else:
                    sys.stderr.write("%s\n" % (v,))

        ctx = exc.context[-1]
        abspathstr = path_to_str(ctx.absolute_path)
        obj = ctx.instance
        show_obj("faulty object", obj, ctx.absolute_path)
        if schema_max_depth != 0:
            show_schema(ctx.schema, ctx.absolute_schema_path)
        sys.stderr.write("%s: error: %s: %s\n" % (filename, abspathstr, ctx.message))

    data = load_json(data_file)
    schema = load_json(schema_file)

    validator_cls = jsonschema.validators.validator_for(schema)
    try:
        if check_schema:
            validator_cls.check_schema(schema)
    except jsonschema.SchemaError as e:
        show_schema_exception(e, schema_file.name)
        raise

    validator = validator_cls(schema)
    e = None
    for e in sorted(validator.descend(data, schema), key=lambda e: e.schema_path):
        show_schema_exception(e, data_file.name)
    if e:
        raise e
    return data


def c_clean(string):
    return re.sub('[^A-Za-z0-9_]', '_', string)

data_type_to_c_map = {
    "boolean": "bool",
    "byte": "unsigned char",
    "int": "struct sol_irange",
    "float": "struct sol_drange",
    "rgb": "struct sol_rgb",
    "string": "const char *",
    }
def data_type_to_c(typename):
    return data_type_to_c_map[typename]

data_type_to_default_member_map = {
    "boolean": "b",
    "byte": "byte",
    "int": "i",
    "float": "f",
    "string": "s",
    "rgb": "rgb",
    }
def data_type_to_default_member(typename):
    return data_type_to_default_member_map.get(typename, "ptr")

data_type_to_packet_type_map = {
    "empty": "SOL_FLOW_PACKET_TYPE_EMPTY",
    "boolean": "SOL_FLOW_PACKET_TYPE_BOOLEAN",
    "int": "SOL_FLOW_PACKET_TYPE_IRANGE",
    "byte": "SOL_FLOW_PACKET_TYPE_BYTE",
    "string": "SOL_FLOW_PACKET_TYPE_STRING",
    "blob": "SOL_FLOW_PACKET_TYPE_BLOB",
    "float": "SOL_FLOW_PACKET_TYPE_DRANGE",
    "rgb": "SOL_FLOW_PACKET_TYPE_RGB",
    "any": "SOL_FLOW_PACKET_TYPE_ANY",
    "error": "SOL_FLOW_PACKET_TYPE_ERROR"
    }
def packet_type_from_data_type(typename):
    if typename.startswith("custom:"):
        return typename.split(':', 1)[1]
    return data_type_to_packet_type_map.get(typename, typename)

def port_name_and_size(orig_name):
    m = re.match(r"([A-Z0-9_]+)(?:\[([0-9]+)\])?", orig_name)
    portname, size = m.groups('0')
    return (portname, int(size))

def str_to_c(string):
    return '"' + string.replace('"', '\\"') + '"'

def str_to_c_or_null(string):
    if string is None:
        return "NULL";
    else:
        return str_to_c(string)

def bool_to_c(value):
    if value:
        return "true"
    else:
        return "false"

def value_to_c(value, data_type):
    if data_type == "string":
        return str_to_c_or_null(value)
    elif data_type == "boolean":
        return bool_to_c(value)
    elif data_type == "rgb":
        red = str(value.get("red", 0))
        green = str(value.get("green", 0))
        blue = str(value.get("blue", 0))
        red_max = str(value.get("red_max", 255))
        green_max = str(value.get("green_max", 255))
        blue_max = str(value.get("blue_max", 255))
        return "{ %s, %s, %s, %s, %s, %s }" % (red, green, blue,
                                               red_max, green_max, blue_max)
    elif data_type == "int":
        if type(value) is int:
            ival = value
            istep = 1
            imin = "INT32_MIN"
            imax = "INT32_MAX"
        else:
            ival = str(value.get("val", 0))
            istep = str(value.get("step", 1))
            imin = str(value.get("min", "INT32_MIN"))
            imax = str(value.get("max", "INT32_MAX"))
        return "{ %s, %s, %s, %s }" % (ival, imin, imax, istep)
    elif data_type == "float":
        if type(value) in [float, int]:
            ival = value
            istep = "DBL_MIN"
            imin = "-DBL_MAX"
            imax = "DBL_MAX"
        else:
            ival = str(value.get("val", 0))
            istep = str(value.get("step", "DBL_MIN"))
            imin = str(value.get("min", "-DBL_MAX"))
            imax = str(value.get("max", "DBL_MAX"))
        return "{ %s, %s, %s, %s }" % (ival, imin, imax, istep)
    else:
        return "%s" % (value)


def load_json(infile, schema, prefix):
    data = json_load_and_check(infile, schema)

    prefix_c = ""
    if prefix:
        prefix_c = c_clean(prefix)

    name_c = c_clean(data["name"].lower())
    if prefix_c:
        name_c = prefix_c + "_" + name_c

    data["name_c"] = name_c
    data["NAME_C"] = name_c.upper()
    data["prefix_c"] = prefix_c
    return data


def uses_float(data):
    if "types" in data:
        for t in data["types"]:
            header = uses_float(t)
            if header:
                return header
    elif "options" in data and "members" in data["options"]:
        for o in data["options"]["members"]:
            if o["data_type"] == "float":
                return "#include <float.h>\n"
    return ""


def generate_header_head(outfile, data):
    outfile.write("""\
#pragma once
/* this file was auto-generated from %(input)s */
#include <stdbool.h>
#include <stdint.h>
#include <sol-flow.h>
%(float_h)s

#ifdef __cplusplus
extern "C" {
#endif
""" % {
    "input": data["input"],
    "name_c": data["name_c"],
    "NAME_C": data["NAME_C"],
    "float_h": uses_float(data),
})

def generate_header_tail(outfile, data):
    outfile.write("""
#ifdef __cplusplus
}
#endif
""")


def generate_header_entry(outfile, data):
    outfile.write("""\

#define %(NAME_C)s_DEFINED 1
extern const struct sol_flow_node_type *%(NAME_C)s;
""" % {
    "input": data["input"],
    "NAME_C": data["NAME_C"],
})

    if "options" in data:
        outfile.write("""
struct %(name_c)s_options {
    struct sol_flow_node_options base;
#define %(NAME_C)s_OPTIONS_API_VERSION (%(options_version)d)
""" % {
    "name_c": data["name_c"],
    "NAME_C": data["NAME_C"],
    "options_version": data["options"]["version"],
})
        if "members" in data["options"]:
            for o in data["options"]["members"]:
                doc = o.get("description", "")
                default = o.get("default")
                if default is not None:
                    doc += " (default: %r)" % (default,)
                else:
                    doc += " (required)"

                outfile.write("""\
    %(type)s %(name)s; /**< %(doc)s */
""" % {
    "type": data_type_to_c(o["data_type"]),
    "name": o["name"],
    "doc": doc,
    })
        outfile.write("};\n")

        outfile.write("""
#define %(NAME_C)s_OPTIONS_DEFAULTS(...) { \\
    .base = { \\
        .api_version = SOL_FLOW_NODE_OPTIONS_API_VERSION, \\
        .sub_api = %(NAME_C)s_OPTIONS_API_VERSION \\
    }, \\
""" % {
    "name_c": data["name_c"],
    "NAME_C": data["NAME_C"]
    })
        if "members" in data["options"]:
            for o in data["options"]["members"]:
                if "default" in o:
                    outfile.write("""\
    .%(name)s = %(value)s, \\
""" % {
    "name": o["name"],
    "value": value_to_c(o["default"], o["data_type"])
    })
        outfile.write("""\
    __VA_ARGS__ \\
}""")

    if "in_ports" in data:
        port_number_offset = 0
        outfile.write("\n/* Input Ports */\n")
        for i, o in enumerate(data["in_ports"]):
            pname, psize = port_name_and_size(o["name"])
            outfile.write("#define %s__IN__%s (%d)\n" % (
                data["NAME_C"], c_clean(pname).upper(), i + port_number_offset))
            for port_idx in range(psize):
                outfile.write("#define %s__IN__%s_%d (%d)\n" % (
                    data["NAME_C"], c_clean(pname).upper(),
                    port_idx, i + port_number_offset))
                port_number_offset += 1
            else:
                if psize > 0: port_number_offset -= 1

    if "out_ports" in data:
        port_number_offset = 0
        outfile.write("\n/* Output Ports */\n")
        for i, o in enumerate(data["out_ports"]):
            pname, psize = port_name_and_size(o["name"])
            outfile.write("#define %s__OUT__%s (%d)\n" % (
                data["NAME_C"], c_clean(pname).upper(), i + port_number_offset))
            for port_idx in range(psize):
                outfile.write("#define %s__OUT__%s_%d (%d)\n" % (
                    data["NAME_C"], c_clean(pname).upper(),
                    port_idx, i + port_number_offset))
                port_number_offset += 1
            else:
                if psize > 0: port_number_offset -= 1


def generate_code_head(outfile, data):
    outfile.write("""\
/* this file was auto-generated from %(input)s.
 * include it from your implementation after your methods and
 * types are defined.
 */
#ifndef %(NAME_C)s_DEFINED
#include "%(header)s"
#endif
#include <sol-flow-packet.h>
#include <sol-macros.h>
#include <errno.h>
%(float_h)s
""" % {
    "input": data["input"],
    "header": data["header"],
    "NAME_C": data["NAME_C"],
    "float_h": uses_float(data),
})


def generate_code_tail(outfile, data):
    outfile.write("""

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED

#ifdef SOL_FLOW_NODE_TYPE_MODULE_EXTERNAL

void sol_flow_foreach_module_node_type(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data);

SOL_API void
sol_flow_foreach_module_node_type(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data)

#else

const struct sol_flow_node_type *sol_flow_foreach_builtin_node_type_%(name)s(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data);

SOL_API const struct sol_flow_node_type *
sol_flow_foreach_builtin_node_type_%(name)s(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data)

#endif // SOL_FLOW_NODE_TYPE_MODULE_EXTERNAL
{
    static const struct sol_flow_node_type **types[] = {
""" % {
    "name": c_clean(data["name"].lower())
})
    for node_type in data["types"]:
        outfile.write("""\
            &%(NAME_C)s,
""" % {
    "NAME_C" : node_type["NAME_C"]
})
    outfile.write("""\
            NULL
    }, ***itr;

    if (!cb)
#ifdef SOL_FLOW_NODE_TYPE_MODULE_EXTERNAL
        return;
#else
        return NULL;
#endif

    for (itr = types; *itr != NULL; itr++) {
        const struct sol_flow_node_type **type = &**itr;
        if ((*type)->init_type)
            (*type)->init_type();
        if (!cb((void *)data, *type))
#ifdef SOL_FLOW_NODE_TYPE_MODULE_EXTERNAL
            break;
#else
            return *type;
#endif
    }
#ifndef SOL_FLOW_NODE_TYPE_MODULE_EXTERNAL
    return NULL;
#endif
}
#else
#define sol_flow_foreach_module_node_type(cb, data)
#endif // SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
""")


def generate_code_entry(outfile, data):
    methods = data.get("methods", {})
    init_type = None

    in_ports_names = []
    in_ports_count = 0
    in_ports_get_port = ""
    in_data_types = []
    if "in_ports" in data:
        for o in data["in_ports"]:
            if "type" in o:
                continue
            pname, psize = port_name_and_size(o["name"])
            type_c = "%s__in__%s" % (data["name_c"], c_clean(pname))
            in_ports_names.append(type_c)
            if psize == 0: psize = 1
            in_ports_count += psize
            in_ports_get_port += """\
    if (port < %(port_limit)d)
        return &%(port_name)s;
""" % {
    "port_limit": in_ports_count,
    "port_name": type_c
    }
            in_data_types.append(o.get("data_type", "empty"))
            port_methods = o.get("methods", {})
            connect = port_methods.get("connect", "NULL")
            disconnect = port_methods.get("disconnect", "NULL")
            process = port_methods.get("process", "NULL")
            outfile.write("""
static struct sol_flow_port_type_in %(type_c)s = {
    .api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION,
    .process = %(process)s,
    .connect = %(connect)s,
    .disconnect = %(disconnect)s,
};
""" % {
    "type_c": type_c,
    "process": process,
    "connect": connect,
    "disconnect": disconnect,
    })

    out_ports_names = []
    out_ports_count = 0
    out_ports_get_port = ""
    out_data_types = []
    if "out_ports" in data:
        for o in data["out_ports"]:
            if "type" in o:
                continue
            pname, psize = port_name_and_size(o["name"])
            type_c = "%s__out__%s" % (data["name_c"], c_clean(pname))
            out_ports_names.append(type_c)
            if psize == 0: psize = 1
            out_ports_count += psize
            out_ports_get_port += """\
    if (port < %(port_limit)d)
        return &%(port_name)s;
""" % {
    "port_limit": out_ports_count,
    "port_name": type_c
    }
            out_data_types.append(o.get("data_type", "empty"))
            port_methods = o.get("methods", {})
            connect = port_methods.get("connect", "NULL")
            disconnect = port_methods.get("disconnect", "NULL")
            outfile.write("""
static struct sol_flow_port_type_out %(type_c)s = {
    .api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION,
    .flags = %(flags)s,
    .connect = %(connect)s,
    .disconnect = %(disconnect)s,
};
""" % {
    "type_c": type_c,
    "flags": o.get("flags", "0"),
    "connect": connect,
    "disconnect": disconnect,
    })

    if in_ports_names:
        outfile.write("""\
static const struct sol_flow_port_type_in *
%(name_c)s_get_port_in_internal(const struct sol_flow_node_type *type, uint16_t port)
{
%(get_ports)s
    return NULL; /* shouldn't happen, but compiler complains otherwise */
}
""" % {
    "name_c": data["name_c"],
    "get_ports": in_ports_get_port
    })

    if out_ports_names:
        outfile.write("""\
static const struct sol_flow_port_type_out *
%(name_c)s_get_port_out_internal(const struct sol_flow_node_type *type, uint16_t port)
{
%(get_ports)s
    return NULL; /* shouldn't happen, but compiler complains otherwise */
}
""" % {
    "name_c": data["name_c"],
    "get_ports": out_ports_get_port
    })

    outfile.write("""\
static void
%(name_c)s_get_ports_counts_internal(const struct sol_flow_node_type *type, uint16_t *ports_in_count, uint16_t *ports_out_count)
{
""" % {
    "name_c": data["name_c"],
    })

    if in_ports_names or out_ports_names:
        first_port = in_ports_names[0] if in_ports_names else out_ports_names[0]
        outfile.write("    if (%s.packet_type == NULL) {\n" % (first_port))

    for port, d_type in zip(in_ports_names, in_data_types):
        outfile.write("        %s.packet_type = %s;\n"  % (port, packet_type_from_data_type(d_type)))

    for port, d_type in zip(out_ports_names, out_data_types):
        outfile.write("        %s.packet_type = %s;\n"  % (port, packet_type_from_data_type(d_type)))

    if in_ports_names or out_ports_names:
        outfile.write("    }\n")

    outfile.write("""\
    if (ports_in_count)
        *ports_in_count = %(ports_in_count)d;
    if (ports_out_count)
        *ports_out_count = %(ports_out_count)d;
}
""" % {
    "name_c": data["name_c"],
    "ports_in_count": in_ports_count,
    "ports_out_count": out_ports_count,
    })

    outfile.write("""
static void
%(name_c)s_init_type_internal(void)
{
""" % {
    "name_c": data["name_c"],
    })

    if "init_type" in methods:
        outfile.write("    %s();\n" % methods.get("init_type"))

    outfile.write("""\
}
""")

    new_options_func = "NULL"
    free_options_func = "NULL"
    if "options" in data:
        outfile.write("""
static const struct %(name_c)s_options %(name_c)s_options_defaults = %(NAME_C)s_OPTIONS_DEFAULTS();
""" % {
    "name_c": data["name_c"],
    "NAME_C": data["NAME_C"],
    })
        new_options_func = None
        free_options_func = None
        if "methods" in data["options"]:
            new_options_func = data["options"]["methods"].get("new", None)
            free_options_func = data["options"]["methods"].get("free", None)

    if new_options_func is None:
        new_options_func = "%s_new_options_internal" % data["name_c"]
        outfile.write("""
static struct sol_flow_node_options *
%(name_func)s(const struct sol_flow_node_options *copy_from)
{
    struct %(name_c)s_options *opts;
    const struct %(name_c)s_options *from;

    if (!copy_from) from = &%(name_c)s_options_defaults;
    else from = (struct %(name_c)s_options *)copy_from;
    if (from->base.sub_api != %(NAME_C)s_OPTIONS_API_VERSION) {
        errno = -EINVAL;
        return NULL;
    }

    opts = malloc(sizeof(*opts));
    if (!opts) {
        errno = -ENOMEM;
        return NULL;
    }
    *opts = *from;
""" % {
    "name_c": data["name_c"],
    "NAME_C": data["NAME_C"],
    "name_func": new_options_func
    })
        members = data["options"].get("members", [])
        for m in members:
            if m["data_type"] == "string":
                outfile.write("""
    if (opts->%(member)s)
        opts->%(member)s = strdup(opts->%(member)s);
""" % {
    "member": m["name"]
    })
        outfile.write("""
    return &opts->base;
}
""")

    if free_options_func is None:
        free_options_func = "%s_free_options_internal" % data["name_c"]
        outfile.write("""
static void
%(name_func)s(struct sol_flow_node_options *options)
{
    struct %(name_c)s_options *opts;
    if (!options) return;
    opts = (struct %(name_c)s_options *)options;
""" % {
    "name_c": data["name_c"],
    "name_func": free_options_func
    })
        members = data["options"].get("members", [])
        for m in members:
            if m["data_type"] == "string":
                outfile.write("""
    free((void *)opts->%(member)s);
""" % {
    "member": m["name"]
    })
        outfile.write("""
    free(opts);
}
""")

    outfile.write("""
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
static const struct sol_flow_node_type_description %(name_c)s_description = {
    .api_version = SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION,
    .name = %(name)s,
    .category = %(category)s,
    .symbol = %(symbol)s,
    .options_symbol = "%(options_symbol)s_options",
    .description = %(description)s,
    .author = %(author)s,
    .url = %(url)s,
    .license = %(license)s,
    .version = %(version)s,
""" % {
    "name_c": data["name_c"],
    "name": str_to_c(data["name"]),
    "category": str_to_c(data["category"]),
    "symbol": str_to_c(data["NAME_C"]),
    "options_symbol": data["name_c"],
    "author": str_to_c_or_null(data.get("author")),
    "description": str_to_c_or_null(data.get("description")),
    "url": str_to_c_or_null(data.get("url")),
    "license": str_to_c_or_null(data.get("license")),
    "version": str_to_c_or_null(data.get("version")),
    })

    if "in_ports" in data:
        outfile.write("""\
    .ports_in = (const struct sol_flow_port_description * const []){
""")
        port_base = 0
        for o in data["in_ports"]:
            pname, psize = port_name_and_size(o["name"])
            outfile.write("""\
            &((const struct sol_flow_port_description){
            .name = %(name)s,
            .description=%(description)s,
            .data_type=%(data_type)s,
            .array_size=%(array_size)d,
            .base_port_idx=%(port_base)d,
            .required=%(required)s,
        }),
""" % {
    "name": str_to_c(pname),
    "description": str_to_c(o.get("description")),
    "data_type": str_to_c_or_null(o.get("data_type")),
    "array_size": psize,
    "port_base": port_base,
    "required": bool_to_c(o.get("required", False)),
    })
            port_base += psize if psize else 1;
        outfile.write("""\
        NULL
    },
""")

    if "out_ports" in data:
        outfile.write("""\
    .ports_out = (const struct sol_flow_port_description * const []){
""")
        port_base = 0
        for o in data["out_ports"]:
            pname, psize = port_name_and_size(o["name"])
            outfile.write("""\
        &((const struct sol_flow_port_description){
            .name = %(name)s,
            .description=%(description)s,
            .data_type=%(data_type)s,
            .array_size=%(array_size)d,
            .base_port_idx=%(port_base)d,
            .required=%(required)s,
        }),
""" % {
    "name": str_to_c(pname),
    "description": str_to_c_or_null(o.get("description")),
    "data_type": str_to_c_or_null(o.get("data_type")),
    "array_size": psize,
    "port_base": port_base,
    "required": bool_to_c(o.get("required", False)),
    })
            port_base += psize if psize else 1;
        outfile.write("""\
        NULL
    },
""")

    if "options" in data:
        required_member = False
        member_options = ""
        if "members" in data["options"]:
            for o in data["options"]["members"]:
                member_options += """\
        {
            .name="%(name)s",
            .description=%(description)s,
            .data_type=%(data_type)s,
            .required=%(required)s,
            .offset=offsetof(struct %(name_c)s_options, %(name)s),
            .size=sizeof(%(type_c)s),
""" % {
    "name": o["name"],
    "description": str_to_c_or_null(o.get("description")),
    "data_type": str_to_c(o["data_type"]),
    "required": "false" if "default" in o else "true",
    "name_c": data["name_c"],
    "type_c": data_type_to_c(o["data_type"]),
    }
                if not "default" in o:
                    required_member = True
                else:
                    member_options += """\
            .defvalue = {
                .%(default_member)s=%(default)s,
            },
""" % {
    "default_member": data_type_to_default_member(o["data_type"]),
    "default": value_to_c(o.get("default"), o["data_type"]),
    }
                member_options += """\
        },
"""

        member_options += """\
            {},
"""
        outfile.write("""\
    .options = &((const struct sol_flow_node_options_description){
        .data_size = sizeof(struct %(name_c)s_options),
        .sub_api = %(NAME_C)s_OPTIONS_API_VERSION,
        .required = %(required)s,
        .members = (const struct sol_flow_node_options_member_description[]){
            %(member_options)s
""" % {
    "name_c": data["name_c"],
    "NAME_C": data["NAME_C"],
    "member_options": member_options,
    "required": bool_to_c(required_member),
    })

        outfile.write("""\
        },
    }),
""")

    node_type = "struct sol_flow_node_type"
    node_base_type_access_open = ""
    node_base_type_access_close = ""
    node_base_type_access = ""
    if "node_type" in data:
        node_type = data["node_type"]["data_type"]
        for a in data["node_type"]["access"]:
            node_base_type_access_open += ".%s = {" % (a,)
            node_base_type_access_close += "}"
            node_base_type_access += ".%s" % (a,)
        node_base_type_access_close += ","

    data_size = "0"
    if "private_data_type" in data:
        data_size = "sizeof(struct " + data.get("private_data_type") + ")"
    outfile.write("""\
};
#endif

static const %(node_type)s %(name_c)s = {%(node_base_type_access_open)s
    .api_version = SOL_FLOW_NODE_TYPE_API_VERSION,
    .data_size = %(data_size)s,
    .init_type = %(name_c)s_init_type_internal,
    .new_options = %(new_opts)s,
    .free_options = %(free_opts)s,
    .open = %(open)s,
    .close = %(close)s,
    .get_ports_counts = %(name_c)s_get_ports_counts_internal,
""" % {
    "node_type": node_type,
    "node_base_type_access_open": node_base_type_access_open,
    "name_c": data["name_c"],
    "data_size": data_size,
    "new_opts": new_options_func,
    "free_opts": free_options_func,
    "open": methods.get("open", "NULL"),
    "close": methods.get("close", "NULL"),
    })

    if in_ports_names:
        outfile.write("""\
    .get_port_in = %(name_c)s_get_port_in_internal,
""" % {"name_c": data["name_c"]})

    if out_ports_names:
        outfile.write("""\
    .get_port_out = %(name_c)s_get_port_out_internal,
""" % {"name_c": data["name_c"]})

    outfile.write("""\
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    .description=&%(name_c)s_description,
#endif
%(node_base_type_access_close)s
""" % {
    "name_c": data["name_c"],
    "node_base_type_access_close": node_base_type_access_close,
    })

    if "node_type" in data and "extra_methods" in data["node_type"]:
        def rec_extra(k, v, indent=0):
            indent += 1
            outfile.write("    " * indent)
            outfile.write(".%s = " % (k,))
            if isinstance(v, str):
                outfile.write(v)
            else:
                outfile.write("{\n")
                for key, value in v.items():
                    rec_extra(key, value, indent)
                outfile.write("    " * indent)
                outfile.write("}")
            outfile.write(",\n")

        for k, v in data["node_type"]["extra_methods"].items():
            rec_extra(k, v)

    outfile.write("""\
};

SOL_API const struct sol_flow_node_type *%(NAME_C)s = &%(name_c)s%(node_base_type_access)s;
""" % {
    "name_c": data["name_c"],
    "NAME_C": data["NAME_C"],
    "node_base_type_access": node_base_type_access,
    })


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix",
                        help="Prefix to use in C generated code",
                        type=str)
    parser.add_argument("schema",
                        help="JSON Schema to use for validation",
                        type=argparse.FileType('r'))
    parser.add_argument("input",
                        help="Input description file in JSON format",
                        type=argparse.FileType('r'))
    parser.add_argument("output_header",
                        help="Output header (.h) file",
                        type=argparse.FileType('w'))
    parser.add_argument("output_code",
                        help="Output code (.c) file",
                        type=argparse.FileType('w'))

    args = parser.parse_args()
    try:
        data = load_json(args.input, args.schema, args.prefix)
    except Exception as e:
        if args.output_header and args.output_header.name:
            os.unlink(args.output_header.name)
        if args.output_code and args.output_code.name:
            os.unlink(args.output_code.name)
        if isinstance(e, (ValueError, jsonschema.ValidationError, jsonschema.SchemaError)):
            exit(1)
        else:
            raise

    try:
        if not "types" in data:
            tmp = data
            data = {
                "types": [tmp],
                "name": tmp["name"],
                "name_c": tmp["name_c"],
                "NAME_C": tmp["NAME_C"],
                "prefix_c": tmp["prefix_c"],
                }

        data["input"] = args.input.name
        data["header"] = args.output_header.name
        data["source"] = args.output_code.name

        ntypes = len(data["types"])
        output_header = args.output_header
        output_code = args.output_code
        generate_header_head(args.output_header, data)
        generate_code_head(args.output_code, data)
        for t in data["types"]:
            t["header"] = data["header"]
            t["source"] = data["source"]
            t["input"] = data["input"]
            t["name_c"] = data["prefix_c"] + "_" + c_clean(t["name"].lower())
            t["NAME_C"] = t["name_c"].upper()
            t["prefix_c"] = data["prefix_c"]
            generate_header_entry(output_header, t)
            generate_code_entry(output_code, t)

        generate_header_tail(args.output_header, data)
        generate_code_tail(args.output_code, data)
        output_header.close()
        output_code.close()
    except:
        if args.output_header and args.output_header.name:
            os.unlink(args.output_header.name)
        if args.output_code and args.output_code.name:
            os.unlink(args.output_code.name)
        raise
