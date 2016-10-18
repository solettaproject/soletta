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

import argparse
import os
import re
import sys

functionsPattern = re.compile('(\w*\s*\w+)\s\**\s*(\w+)\([\w*,\s\(\).\[\]]+\)[\s\w,\(\)]*(;|\s#ifndef)')
variablesPattern = re.compile('extern[\s\w]+?\**(\w+)[\[\d\]]*;')

if __name__ == "__main__":
    argparser = argparse.ArgumentParser(description=""" The check-api script checks if all the exported functions/variables
    declared in the installed headers are properly present in the version script file""")

    argparser.add_argument("--version_script", type=str, help=""" Path to the version script """, required=True)
    argparser.add_argument("--src_dir", type=str, help=""" Source directory """, required=True)
    args = argparser.parse_args()
    exitWithErr = False
    missingSymbols = {}

    with open(args.version_script) as fData:
        versionScriptSymbols = re.findall('(?<!})\s+(\w+);', fData.read())

    for root, dirs, files in os.walk(args.src_dir):
        if not root.endswith("include"):
            continue
        for f in files:
            contents = ""
            with open(os.path.join(root, f)) as fData:
                contents = fData.read()
            headerExportedFunctions = functionsPattern.findall(contents)
            headerExportedFunctions = list(filter(lambda symbol: False if "return" in symbol[0] or
                                                  "inline" in symbol[0] else True, headerExportedFunctions))

            exportedSymbols = variablesPattern.findall(contents)
            exportedSymbols = exportedSymbols + list(map(lambda symbol: symbol[1], headerExportedFunctions))

            for exported in exportedSymbols:
                if exported.startswith("SOL_FLOW_PACKET_TYPE_"): #A lovely whitelist <3
                    continue
                if not exported in versionScriptSymbols:
                    if not f in missingSymbols:
                        missingSymbols[f] = [exported]
                    else:
                        missingSymbols[f].append(exported)
                else:
                    versionScriptSymbols.remove(exported)

    if len(missingSymbols):
        print("Symbols that were not found at '%s'\n\n" % (args.version_script))
        for key in missingSymbols:
            print("\nFile: %s - Missing symbols: %s" % (key, missingSymbols[key]))
        exitWithErr = True
    if len(versionScriptSymbols):
        print("\n\nSymbols declared at '%s' that were not found in the exported headers: %s" % (args.version_script, versionScriptSymbols))
        exitWithErr = True

    if exitWithErr:
        sys.exit(-1)
    print("All exported symbols are present in " + args.version_script)
    sys.exit(0)
