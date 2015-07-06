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

import argparse
import configparser
import os
import re
import stat

class TemplateFragment:
    def __init__(self, context, verbatim, expr):
        self.context = context
        self.verbatim = verbatim
        self.expr = expr
        self.subst = ""

    def __append_subst(self, subst):
        self.subst += "%s\n" % subst

    def value_of(self, k):
        value = self.context.get(k)
        if value:
            self.__append_subst(value)

    def on_value(self, k, v, ontrue, onfalse):
        value = self.context.get(k.lower())
        if value and value == v:
            self.__append_subst(ontrue)
        else:
            self.__append_subst(onfalse)

    def on_set(self, k, ontrue, onfalse):
        if self.context.get(k.lower()):
            self.__append_subst(ontrue)
        else:
            self.__append_subst(onfalse)

    def println(self, ln):
        self.__append_subst(ln)

def parse_template(raw, context):
    result = []
    curr = prev = ""
    expr = False
    for ch in raw:
        if ch == "{" and prev == "{":
            if curr:
                fragment = TemplateFragment(context, curr, expr)
                result.append(fragment)
            curr = ""
            expr = True
        elif ch == "}" and prev == "}":
            if curr:
                fragment = TemplateFragment(context, curr, expr)
                result.append(fragment)
            curr = ""
            expr = False
        elif ch not in("{", "}"):
            curr += ch
        prev = ch

    if curr:
        fragment = TemplateFragment(context, curr, expr)
        result.append(fragment)

    return result

def load_context(files):
    result = {}
    for f in files:
        lines = f.read()
        content = "[context]\n%s" % lines
        handle = configparser.ConfigParser(delimiters=('=','?=',':='))
        handle.read_string(content)
        dc = handle["context"]
        result = dict(list(result.items()) + list(dc.items()))
    return result

def try_subst(verbatim):
    p = re.compile("^\@.*\@$")
    m = p.match(verbatim)

    if not m:
        return None

    return verbatim.replace("@","")

def run_template(f, context):
    raw = f.read()
    fragments = parse_template(raw, context)
    for frag in fragments:
        if frag.expr:
            subst = try_subst(frag.verbatim)
            if subst:
                subst = context.get(subst.lower(), "")
                subst = subst.replace("\"","")
                frag.subst = subst
            else:
                exec(frag.verbatim, {"st": frag, "context": context})
            raw = raw.replace("{{%s}}" % frag.verbatim, frag.subst)

    return raw

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--context-files",
                        help=("The context files path. A context file"
                              "is a file containing key=value pairs, like"
                              "the kconfig's .config file"),
                        type=argparse.FileType("r"), nargs="+",
                        required=True)
    parser.add_argument("--template", help="The template file path",
                        type=argparse.FileType("r"), required=True)
    parser.add_argument("--output", help="The template file path",
                        type=argparse.FileType("w"), required=True)

    args = parser.parse_args()
    context = load_context(args.context_files)
    output = run_template(args.template, context)

    args.output.write(output)
    st = os.fstat(args.template.fileno())
    os.fchmod(args.output.fileno(), st.st_mode)
