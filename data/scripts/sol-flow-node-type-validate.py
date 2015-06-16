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

import jsonschema
import json
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


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--context-lines",
                        help="How many lines of context to show on JSON parsing errors",
                        default=3,
                        type=int)
    parser.add_argument("--schema-max-depth",
                        help="Depth to print on JSON Schema validation errors. 0 disables, -1 shows all.",
                        default=2,
                        type=int)
    parser.add_argument("schema",
                        help="JSON Schema to use for validation",
                        type=argparse.FileType('r'))
    parser.add_argument("input",
                        help="Input description file in JSON format",
                        type=argparse.FileType('r'))

    args = parser.parse_args()

    try:
        json_load_and_check(args.input, args.schema,
                            args.context_lines, args.schema_max_depth)
    except (ValueError, jsonschema.ValidationError, jsonschema.SchemaError):
        exit(1)
