#!/usr/bin/env python3

# This file is part of the Soletta (TM) Project
#
# Copyright (C) 2016 Intel Corporation. All rights reserved.
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

# This script should be used to generate a list of node types
# and examples of use. May be useful to generate reports.
#
# TODO - handle types used by conf files
#
# Usage example:
#
# ./sol-flow-node-type-examples-list.py file.csv soletta/src/ \
#   share/soletta/flow/descriptions/

import json
import os
import re
from os import walk

# TODO - handle types used by conf files
def get_examples(src_dir, path_index):
    examples = {}
    fbp_files = []

    for (dirpath, dirnames, filenames) in walk(src_dir):
        for filename in filenames:
            if filename.endswith(".fbp"):
                fbp_files.append(os.path.join(dirpath, filename))

    for fbp_file in fbp_files:
        with open(fbp_file, "r") as f:
            content = f.read()
            # skip error tests
            if "TEST-EXPECTS-ERROR" in content:
                continue
            for line in content.splitlines():
                nodes = re.findall("[(](?P<name>[^):]+)[):]", line)
                for node in nodes:
                    fbp_url = fbp_file[path_index:]
                    node_files = examples.setdefault(node, set())
                    node_files.add(fbp_url)

    return examples

def print_node_type(outfile, node_type, examples, group_id):
    examples_list = " "
    examples = examples.get(node_type["name"], set())
    for example in sorted(examples):
        # drop .fbp
        examples_list = examples_list + example[:-4] + ", "
    examples_list = examples_list[:-2]

    outfile.write("""\
%(name)s;%(examples)s
""" % {
    "name": node_type["name"],
    "examples": examples_list
    })

def print_description(outfile, description, infile, examples):
    modules = description.keys()
    if len(modules) != 1:
        print("Warning: a single module is expected per file. Skiping %s" %
              infile.name)
        return

    for module in sorted(description.keys()):
        for node_type in description[module]:
            print_node_type(outfile, node_type, examples, module)

def create_doc(outfile, src_dir, desc_dir):
    examples = get_examples(src_dir, len(src_dir))

    json_files = []
    for (dirpath, dirnames, filenames) in walk(desc_dir):
        for filename in filenames:
            if filename.endswith(".json"):
                infile = os.path.join(dirpath, filename)
                json_files.append(infile)

    for infile in sorted(json_files):
        fd = open(infile)
        description = json.load(fd)
        print_description(outfile, description, fd, examples)
        fd.close()


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("csv",
                        help="Path to write csv file",
                        type=argparse.FileType('w'))
    parser.add_argument("src_dir",
                        help="Path to Soletta src/ directory with example files",
                        type=str)
    parser.add_argument("desc_dir",
                        help="Path to directory with description files in JSON format",
                        type=str)

    args = parser.parse_args()

    create_doc(args.csv, args.src_dir, args.desc_dir)

    args.csv.close()
