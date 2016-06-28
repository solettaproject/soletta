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

# This script should be used to generate a document describing
# node types. Json files should be provided as arguments.
# Output is latex format.
#
# Example:
#
# ./sol-flow-node-type-gen-cheat-sheet.py cheat_sheet.tex \
#   src/lib/flow/builtins.json src/modules/flow/*.json

import json
import re

last_category = None

def writeln(outfile, string):
    outfile.write(re.sub('_', '\_', string))

def print_header(outfile):
    outfile.write("""\
\\documentclass{article}
\\usepackage[utf8]{inputenc}

\\linespread{0}

\\usepackage{marginnote}
\\usepackage{color}
\\usepackage{rotating}
\\usepackage{geometry}
\\geometry{
  a4paper,
  left=20mm,
  right=20mm,
  top=20mm,
  bottom=20mm
}

\\title{Soletta Node Types Cheat Sheet}

\\begin{document}

\\section*{\\centering{Soletta Node Types Cheat Sheet}}


""")

def print_option(outfile, option):
    string = """\
\\item \\small \\texttt{%(name)s}{\\scriptsize (%(data_type)s)}: %(description)s
""" % {
    "name": option["name"],
    "data_type": option["data_type"],
    "description": option["description"],
    }
    writeln(outfile,string)

    value = option.get("default")
    if value:
        string = """ \
        Default Value: %s
""" % str(option["default"])
        writeln(outfile, string)

def print_port(outfile, port):
    string = """\
\\item \\small \\texttt{%(name)s}{\\scriptsize (%(data_type)s)}: %(description)s
""" % {
    "name": port["name"],
    "data_type": port["data_type"],
    "description": port["description"],
    }
    writeln(outfile, string)

def print_node_type(outfile, node_type):
    global last_category
    if last_category != node_type["category"]:
        outfile.write("""\\marginnote{\\begin{turn}{90}\\colorbox{black}{\\color{white}%s}\\end{turn}}""" % node_type["category"])
        last_category = node_type["category"]

    string = """\
\\hrulefill\\newline
{\\Large %(name)s} \\hfill {\\small %(description)s}\\newline
\\begin{itemize}
\\setlength\\itemsep{0em}
""" % {
    "name": node_type["name"],
    "category": node_type["category"],
    "description": node_type["description"],
    }
    writeln(outfile, string)

    options = node_type.get("options")
    if options:
        members = options.get("members")
        if members:
            outfile.write("""\
\\item \\small Options:
\\begin{itemize}
\\setlength\\itemsep{0em}
""")
            for option in members:
                print_option(outfile, option)
            outfile.write("""\
\\end{itemize}
""")

    in_ports = node_type.get("in_ports")
    out_ports = node_type.get("out_ports")

    if in_ports and out_ports:
        outfile.write("""\\begin{minipage}[t]{0.4\\textwidth}""")

    if in_ports:
        outfile.write("""\
\\item \\small Input Ports:
\\begin{itemize}
\\setlength\\itemsep{0em}
""")
        for port in in_ports:
            print_port(outfile, port)
        outfile.write("""\
\\end{itemize}
""")

    if in_ports and out_ports:
        outfile.write("""\\end{minipage}\\hfill\\begin{minipage}[t]{0.4\\textwidth}""")

    if out_ports:
        outfile.write("""\
\\item \\small Output Ports:
\\begin{itemize}
\\setlength\\itemsep{0em}
""")
        for port in out_ports:
            print_port(outfile, port)
        outfile.write("""\
\\end{itemize}
""")

    if in_ports and out_ports:
        outfile.write("""\\end{minipage}""")

    outfile.write("""\
\\end{itemize}
""")


def print_description(outfile, description, infile):
    modules = description.keys()
    if len(modules) != 1:
        print("Warning: a single module is expected per file. Skiping %s" %
              infile.name)
        return

    for module in description.keys():
        outfile.write("""\
\\subsection*{\\centering{Module: %s}}
""" % module)
        for node_type in description[module]:
            print_node_type(outfile, node_type)

def print_foot(outfile):
    outfile.write("""\

\\end{document}
""")

def create_cheat_sheet(outfile, infiles):
    print_header(outfile)
    for infile in infiles:
        description = json.load(infile)
        print_description(outfile, description, infile)
    print_foot(outfile)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("cheat_sheet",
                        help="Path to write cheat sheet file",
                        type=argparse.FileType('w'))
    parser.add_argument("inputs_list", nargs='+',
                        help="List of input description files in JSON format",
                        type=argparse.FileType('r'))

    args = parser.parse_args()
    create_cheat_sheet(args.cheat_sheet, args.inputs_list)
    args.cheat_sheet.close()
