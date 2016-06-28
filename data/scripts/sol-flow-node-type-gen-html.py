#!/usr/bin/env python3

# This file is part of the Soletta (TM) Project
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

# This script should be used to generate a node types html documentation.
# It required json files describing nodes and a html template.
#
# Example:
#
# ./sol-flow-node-type-gen-html.py index.html.in index.html svg_dir/ \
#   src/lib/flow/builtins.json src/modules/flow/*.json

import json
import os
import re
from os import walk

# TODO - handle types used by conf files
def get_svgs(svg_dir, path_index):
    svgs = {}
    fbp_files = []

    for (dirpath, dirnames, filenames) in walk(svg_dir):
        for filename in filenames:
            if filename.endswith(".fbp"):
                fbp_files.append(os.path.join(dirpath, filename))

    for fbp_file in fbp_files:
        with open(fbp_file, "r") as f:
            for line in f:
                nodes = re.findall("\(.*?\)", line)
                for node in nodes:
                    # if it hasn't options, find will return -1, required to
                    # remove ')'.
                    type_end = node.find(':')
                    node = node[1:type_end]
                    node_files = svgs.get(node, [])
                    fbp_url = fbp_file[path_index:]
                    if fbp_url not in node_files:
                        node_files.append(fbp_url)
                        svgs[node] = node_files

    return svgs

def print_example(outfile, example):
    outfile.write("""\
                    { large:'%(image_path)s', thumb:'%(thumb_path)s', code:'%(code_path)s' },
""" % {
    "image_path": example[0:-3] + "svg",
    "thumb_path": example[0:-4] + "_thumb.jpg",
    "code_path": example,
    })

def print_option(outfile, option):
    outfile.write("""\
                    { name:"%(name)s", description:"%(description)s", dataType:"%(data_type)s", required:true, defaults:"%(default)s" },
""" % {
    "name": option["name"],
    "data_type": option["data_type"],
    "description": option["description"].replace("\"", "\\\""),
    "default": re.sub("\"", "\\\"", str(option.get("default", "")))
    })

def print_port(outfile, port):
    outfile.write("""\
                    { name:"%(name)s", description:"%(description)s", dataType:"%(data_type)s", required:true},
""" % {
    "name": port["name"],
    "data_type": port["data_type"],
    "description": port["description"],
    })

def print_alias(outfile, alias):
    outfile.write("""\
                    { name:"%s"},
""" %(alias))

def print_node_type(outfile, node_type, svgs, group_id):
    outfile.write("""\
            var entry = new Entry({
                id:"entry_%(group_id)s_%(entry_id)s",
                name:"%(name)s",
                categoryName:fullCategoryName,
                categoryId:"category_%(group_id)s",
                description:"%(description)s",
                aliases:[
""" % {
    "name": node_type["name"],
    "description": node_type["description"].replace("\"", "\\\""),
    "group_id": group_id,
    "entry_id": node_type["name"].replace("/", "-")
    })

    aliases = node_type.get("aliases")
    if aliases:
        for alias in aliases:
            print_alias(outfile, alias)

    outfile.write("""\
                ],
                inputs:[
""")

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

    examples = svgs.get(node_type["name"])
    if examples:
        for example in examples:
            print_example(outfile, example)

    outfile.write("""\
                ],
                exampleId:0,
            });

            entry.getElement().on('entry:select',onEntryEvent);
            entry.getElement().on('entry:hover',onEntryEvent);
""")

def print_description(outfile, description, infile, svgs):
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
    "id": module,
    "name": module,
    })

        for node_type in description[module]:
            print_node_type(outfile, node_type, svgs, module)

        outfile.write("""\
            $(category.getContents()).isotope({
                itemSelector:".entry",
                layoutMode:"masonry"
            });
""")

PLACEHOLDER = "<!-- PLACEHOLDER -->\n"

def create_nodes(outfile, infiles, svgs):
    for infile in infiles:
        description = json.load(infile)
        print_description(outfile, description, infile, svgs)

def create_doc(template, outfile, svg_dir, infiles):
    base = os.path.dirname(outfile.name)
    base_start = svg_dir.find(base)
    if base_start < 0:
        print("Warning: svg dir (%s) isn't inside html path (%s)" %
              svg_dir, outfile)
    svgs = get_svgs(svg_dir, base_start + len(base) + 1)

    for line in template.readlines():
        if (line == PLACEHOLDER):
            create_nodes(outfile, infiles, svgs)
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
    parser.add_argument("svg_dir",
                        help="Path to directory with svg and fbp example files",
                        type=str)
    parser.add_argument("inputs_list", nargs='+',
                        help="List of input description files in JSON format",
                        type=argparse.FileType('r'))

    args = parser.parse_args()

    create_doc(args.template, args.html, args.svg_dir, args.inputs_list)

    args.template.close()
    args.html.close()
