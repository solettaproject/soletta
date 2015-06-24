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
import re
import sys

def c_clean(string):
    return re.sub('[^A-Za-z0-9_]', '_', string)

def generate_module_entry(file, data):
    def rules2str(key):
        if key not in data or not data[key]:
            return ""
        s = ""
        for r in data[key]:
            s += "%s\n" % (r.replace("@path@", data["path"])
                           .replace("@name@", data["name"]),)
        return s

    def built_sources(data):
        if "built_sources" not in data:
            return ""
        bs = ""
        for s in data["built_sources"]:
            bs += " " + s
        return bs

    repl = {
        "name": data["name"],
        "c_name": c_clean(data["name"]),
        "NAME": c_clean(data["name"].upper()),
        "path": data["path"],
        "c_path": c_clean(data["path"]),
        "if_module_cflags": data["if_module_cflags"],
        "am_conditional": data["am_conditional"],
        "ltlibraries": data["ltlibraries"],
        "extra_path_include": "",
        "if_builtin_rules": rules2str("if_builtin_rules"),
        "if_module_rules": rules2str("if_module_rules"),
        "extra_rules": rules2str("extra_rules"),
        "built_sources": built_sources(data),
    }
    if "extra_path" in data:
        repl["extra_path_include"] = "include %s\n" % (data["extra_path"],)

    file.write("""\
%(ltlibraries)s_%(c_name)s_extra_src =
%(ltlibraries)s_%(c_name)s_extra_cflags =
%(ltlibraries)s_%(c_name)s_extra_libs =
%(extra_path_include)s\
if BUILTIN_%(am_conditional)s_%(NAME)s
src_lib_libsoletta_la_SOURCES += %(path)s/%(name)s.c $(%(ltlibraries)s_%(c_name)s_extra_src)
src_lib_libsoletta_la_CFLAGS += $(%(ltlibraries)s_%(c_name)s_extra_cflags)
src_lib_libsoletta_la_LIBADD += $(%(ltlibraries)s_%(c_name)s_extra_libs)
%(if_builtin_rules)s\
endif
if MODULE_%(am_conditional)s_%(NAME)s
%(ltlibraries)s_LTLIBRARIES += %(path)s.la
%(c_path)s_la_SOURCES = %(path)s/%(name)s.c $(%(ltlibraries)s_%(c_name)s_extra_src)
%(c_path)s_la_CFLAGS = $(src_lib_libsoletta_la_CFLAGS) %(if_module_cflags)s $(%(ltlibraries)s_%(c_name)s_extra_cflags)
$(%(c_path)s_la_OBJECTS): %(built_sources)s
%(c_path)s_la_LDFLAGS = -module -no-undefined -avoid-version
%(c_path)s_la_LIBADD = src/lib/libsoletta.la src/shared/libshared.la $(%(ltlibraries)s_%(c_name)s_extra_libs)
%(c_path)s_la_LIBTOOLFLAGS = --tag=disable-static
%(if_module_rules)s\
endif
%(extra_rules)s\
""" % repl)

    if "built_sources" in data:
        file.write("""\
if MODULE_OR_BUILTIN_%(am_conditional)s_%(NAME)s
BUILT_SOURCES += \
""" % repl)
        for s in data["built_sources"]:
            file.write(" " + s)
        file.write("\nendif\n")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()

    parser.add_argument("--built-sources-suffix",
                        help="List of built sources suffix.",
                        type=str, action="append")
    parser.add_argument("--if-module-rule",
                        help="List of extra rules if compiled as module.",
                        type=str, action="append")
    parser.add_argument("--if-builtin-rule",
                        help="List of extra rules if compiled as builtin.",
                        type=str, action="append")
    parser.add_argument("--extra-rule",
                        help="List of extra rules if compiled as either module or builtin.",
                        type=str, action="append")

    parser.add_argument("--if-module-cflags",
                        help="The extra CFLAGS to use if compiled as module.",
                        type=str, default="")

    parser.add_argument("--am-conditional",
                        help="The AM_CONDITIONAL base symbol to use.",
                        required=True, type=str)
    parser.add_argument("--ltlibraries",
                        help="The _LTLIBRARIES base symbol to use.",
                        required=True, type=str)

    parser.add_argument("--outfile",
                        help="Output mk file",
                        type=argparse.FileType('w'),
                        required=True)

    parser.add_argument("--modules-path",
                        help="modules path",
                        required=True, type=str)

    args = parser.parse_args()
    args.outfile.write("# This file was auto-generated by %s\n" %
                       (" ".join(repr(x) for x in sys.argv),))
    for fname in sorted(os.listdir(args.modules_path)):
        path = os.path.join(args.modules_path, fname)
        if not os.path.isdir(path):
            continue
        if fname.startswith("."):
            continue
        if not (os.path.isfile(os.path.join(path, fname + ".c")) or
                os.path.isfile(os.path.join(path, fname + ".json"))):
            continue

        data = {
            "name": fname,
            "path": path,
            "if_module_cflags": args.if_module_cflags,
            "am_conditional": args.am_conditional,
            "ltlibraries": args.ltlibraries,
            "if_module_rules": args.if_module_rule,
            "if_builtin_rules": args.if_builtin_rule,
            "extra_rules": args.extra_rule,
        }

        extra_path = os.path.join(path, "extra_rules.mk")
        if os.path.isfile(extra_path):
            data["extra_path"] = extra_path

        if args.built_sources_suffix:
            built_sources = []
            for s in args.built_sources_suffix:
                built_sources.append(os.path.join(path, fname + s))
            data["built_sources"] = built_sources

        generate_module_entry(args.outfile, data)
    args.outfile.close()
