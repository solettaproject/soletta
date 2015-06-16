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

def gen_builtins(outfile, decl, item, array, count, builtins):
    outfile.write("/* this file was auto-generated */\n")
    decl = decl.replace("@NAME@", "%(NAME)s").replace("@name@", "%(name)s")
    item = item.replace("@NAME@", "%(NAME)s").replace("@name@", "%(name)s")
    for b in builtins:
        vars = {
            "name": b.lower(),
            "NAME": b.upper()
        }
        outfile.write("extern %s;\n" % (decl % vars))
    if builtins:
        outfile.write("%s = {\n" % array)
        for b in builtins:
            vars = {
                "name": b.lower(),
                "NAME": b.upper()
            }
            outfile.write("    %s,\n" % (item % vars))
        outfile.write("};\n")
    outfile.write("#define %s %d\n" %
                  (count, len(builtins)))

if __name__ == "__main__":
    import argparse, os, sys
    parser = argparse.ArgumentParser()
    parser.add_argument("--array",
                        help="Declaration of the array",
                        type=str)
    parser.add_argument("--count",
                        help="symbol to store the array count",
                        type=str)
    parser.add_argument("--decl",
                        help="Declaration of variable to use",
                        type=str)
    parser.add_argument("--item",
                        help="Declaration of array item",
                        type=str, default="&%s")
    parser.add_argument("--output",
                        help="Output file to use.",
                        type=argparse.FileType('w'))
    parser.add_argument("builtins",
                        help="builtins to generate",
                        nargs="*")
    args = parser.parse_args()

    try:
        gen_builtins(args.output or sys.stdout,
                     args.decl,
                     args.item,
                     args.array,
                     args.count,
                     args.builtins)
    except:
        if args.output and args.output.name:
            os.unlink(args.output.name)
