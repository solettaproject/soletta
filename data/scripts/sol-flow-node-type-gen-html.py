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

# This script should be used to generate a node types html documentation.
# It required json files describing nodes and a html template.
#
# Example:
#
# ./sol-flow-node-type-gen-html.py index.html.in index.html \
#   src/lib/flow/builtins.json src/modules/flow/*.json

import json
import re

def print_option(outfile, option):
    outfile.write("""\
                    {name:"%(name)s", description:"%(description)s", dataType:"(data_type)s", required:true, defaults:"%(default)s"},
""" % {
    "name": option["name"],
    "data_type": option["data_type"],
    "description": option["description"],
    "default": str(option.get("default", ""))
    })

def print_port(outfile, port):
    outfile.write("""\
                    { name:"%(name)s", description:"%(description)s", dataType:"(data_type)s", required:true},
""" % {
    "name": port["name"],
    "data_type": port["data_type"],
    "description": port["description"],
    })

group_id = 0
entry_id = 0

def print_node_type(outfile, node_type):
    global group_id
    global entry_id
    outfile.write("""\
            var entry = new Entry({
                id:"entry_%(group_id)s_%(entry_id)s",
                name:"%(name)s",
                categoryName:fullCategoryName,
                categoryId:"category_%(group_id)s",
                description:"%(description)s",
                inputs:[
""" % {
    "name": node_type["name"],
    "description": node_type["description"],
    "group_id": str(group_id),
    "entry_id": str(entry_id),
    })

    in_ports = node_type.get("in_ports")
    if in_ports:
        for port in in_ports:
            print_port(outfile, port)

    outfile.write("""\
                ],
                outputs:[
""")

    out_ports = node_type.get("out_ports")
    if out_ports:
        for port in out_ports:
            print_port(outfile, port)

    outfile.write("""\
                ],
                options:[
""")

    options = node_type.get("options")
    if options:
        members = options.get("members")
        if members:
            for option in members:
                print_option(outfile, option)

    outfile.write("""\
                ],
                exampleList:[
""")

    # TODO add real examples
    outfile.write("""\
                    {large:'images/checker.jpg',thumb:'images/checker_thumb.jpg',code:'example 0 code here'},
                    {large:'images/checker.jpg',thumb:'images/checker_thumb.jpg',code:'example 8 code here'}
""")

    outfile.write("""\
                ],
                exampleId:0,
            });

            entry.getElement().on('entry:select',onEntryEvent);
            entry.getElement().on('entry:hover',onEntryEvent);
""")
    entry_id = entry_id + 1

def print_description(outfile, description, infile):
    global group_id
    global entry_id

    modules = description.keys()
    if len(modules) != 1:
        print("Warning: a single module is expected per file. Skiping %s" %
              infile.name)
        return

    for module in description.keys():
        outfile.write("""\
            var fullCategoryName = "%(name)s";

            var category = new Category({
                id:"category_%(id)s",
                categoryLabel:fullCategoryName,
                menuLabel:fullCategoryName
            });
""" % {
    "id": str(group_id),
    "name": module,
    })

        entry_id = 0
        for node_type in description[module]:
            print_node_type(outfile, node_type)

        outfile.write("""\
            $(category.getContents()).isotope({
                itemSelector:".entry",
                layoutMode:"masonry"
            });
""")
        group_id = group_id + 1


PLACEHOLDER = "<!-- PLACEHOLDER -->\n"

def create_nodes(outfile, infiles):
    for infile in infiles:
        description = json.load(infile)
        print_description(outfile, description, infile)

def create_doc(template, outfile, infiles):
    for line in template.readlines():
        if (line == PLACEHOLDER):
            create_nodes(outfile, infiles)
        else:
            outfile.write(line)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("template",
                        help="Path to html template",
                        type=argparse.FileType('r'))
    parser.add_argument("html",
                        help="Path to write html file",
                        type=argparse.FileType('w'))
    parser.add_argument("inputs_list", nargs='+',
                        help="List of input description files in JSON format",
                        type=argparse.FileType('r'))

    args = parser.parse_args()

    create_doc(args.template, args.html, args.inputs_list)

    args.template.close()
    args.html.close()
