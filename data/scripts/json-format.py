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

import json
import argparse
import os
import os.path
import sys

parser = argparse.ArgumentParser()
parser.add_argument("input",
                    help="Input description file in JSON format",
                    type=argparse.FileType('r'))
parser.add_argument("output",
                    help="Where to output JSON",
                    type=str)

args = parser.parse_args()
data = json.load(args.input)
if args.output == '-':
    outfile = sys.stdout
else:
    if os.path.exists(args.output):
        bkp = "%s~" % (args.output,)
        try:
            os.unlink(bkp)
        except FileNotFoundError:
            pass
        os.rename(args.output, bkp)
    outfile = open(args.output, "w")

# Workaround for Python < 3.4
# In those versions a trailing whitespace is added at the end of each line
data = json.dumps(data, indent=True, sort_keys=True).replace(' \n','\n') + "\n";
outfile.write(data)
