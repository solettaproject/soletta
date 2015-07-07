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

import sys, argparse, os, json
from jsonschema import validate

def printerror():
    print("Usage:\n\t" + os.path.basename(__file__) + " --input_config=/path/to/config_file.config --output_json=path/to/json_file.json")
    sys.exit(2)

def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("input_config", help="The input config file to be converted to json")
    parser.add_argument("output_json", help="The resulting json file")
    args = vars(parser.parse_args())

    main_dict = dict()
    main_dict['nodetypes'] = list()

    with open(args['input_config'], 'r') as input_file:
        for line in input_file:
            line.strip()

            # create a new entry on the 'nodetypes array'
            if line[0] == '[':
                main_dict['nodetypes'].append(dict())
                obj = main_dict['nodetypes'][-1]
                obj['name'] = line.split()[1].rstrip(']')
            elif line.startswith("Type"):
                obj = main_dict['nodetypes'][-1]
                obj['type'] = line.split('=')[1].strip()
            elif line.startswith("Options"):
                # here we create a dict of the possible options
                obj = main_dict['nodetypes'][-1]
                obj['options'] = dict()
                options_str = line.split('=',1)[1]

                for option in options_str.split(';') :
                    key, value = option.split('=')
                    obj['options'][key] = value.strip()

    schema = os.path.abspath(os.path.dirname(__file__) + "/../schemas") +  "/config.schema"
    with open(schema) as schema_file:
        json_schema = json.loads(schema_file.read())
        validate(main_dict, json_schema)

    with open(args['output_json'], 'w') as output_json :
            output_json.write( json.dumps(main_dict, indent=4) )

if __name__ == "__main__":
   main(sys.argv[1:])

