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

import sys
import os
import re
import json
import argparse

fbpTargetRegex = re.compile(r'(.+)\$\((.+)\)(-deps)? := \\?\s?(.+fbp)')
nodeTypeRegex = re.compile(r'\w+\(([a-zA-Z0-9-_]+).*?\).*?')
declaredTypesRegex = re.compile('DECLARE=(.+):(.+):(.+)')
problems = {'missing_makefiles': [], 'missing_deps': {}}

def getNodeTypesFromJson(path):
    nodeAlias = {}
    with open(path, 'r') as f:
        jsonContent = json.load(f)
    for node in jsonContent['nodetypes']:
        t = node['type'].strip()
        idx = t.find('/')
        if idx != -1:
            t = t[:idx]
        nodeAlias[node['name']] = t
    return nodeAlias

def getSamplesData(path):
    samplesData = {}
    with open(os.path.join(path, 'Makefile'), 'r') as f:
        makeFileContent = f.read()
    with open(os.path.join(path, 'Kconfig'), 'r') as f:
        kConfigContent = f.read()

    matches = fbpTargetRegex.findall(makeFileContent)
    for match in matches:
        kConfigKey = match[1].strip()
        fbp = match[3].strip()
        start = kConfigContent.index(kConfigKey)
        deps = kConfigContent[kConfigContent.index('depends', start) :
                              kConfigContent.index('default', start)].replace('depends on', '').lower().split('&&')
        deps = list(filter(lambda x: x != 'FLOW_FBP_GENERATOR_SAMPLES', map(lambda x : x.strip(), deps)))
        samplesData[fbp] = {'deps' : deps, 'node_aliases': { } }
        conf = re.search(match[0].strip() +'\$\(' + kConfigKey + '\)-conffile := \\\\?\\s?(.*json)', makeFileContent)
        if conf is not None:
            samplesData[fbp]['node_aliases'] = getNodeTypesFromJson(os.path.join(path, conf.groups()[0].strip()))
    return samplesData

def getNodeData(path):
    with open(path, 'r') as f:
        #Skip comments
        fbpContent = re.sub('^#.*', '', f.read(), 0, re.M).strip()
    #Skip declares
    lastDeclare = fbpContent.rfind('DECLARE')
    if lastDeclare > -1:
        nodes = nodeTypeRegex.findall(fbpContent[fbpContent.index('\n', lastDeclare):])
    else:
        nodes = nodeTypeRegex.findall(fbpContent)
    declaredTypes = declaredTypesRegex.findall(fbpContent)
    toRemove = []
    extraFbps = []
    extraDeps = []
    for declaredType in declaredTypes:
        t = declaredType[1].strip()
        if  t == 'composed-new' or t == 'composed-split':
            toRemove.append(declaredType[0].strip())
        elif t == 'js':
            toRemove.append(declaredType[0].strip())
            if 'flow_metatype_javascript' not in extraDeps:
                extraDeps.append('flow_metatype_javascript')
        elif t == 'http-composed-server':
            toRemove.append(declaredType[0].strip())
            if 'flow_metatype_http_composed_server' not in extraDeps:
                extraDeps.append('flow_metatype_http_composed_server')
        elif t == 'http-composed-client':
            toRemove.append(declaredType[0].strip())
            if 'flow_metatype_http_composed_client' not in extraDeps:
                extraDeps.append('flow_metatype_http_composed_client')
        elif t == 'fbp':
            toRemove.append(declaredType[0].strip())
            extraFbps.append(declaredType[2].strip())
    return (list(filter(lambda x: False if x in toRemove else True, nodes)), extraFbps, extraDeps)

def hasDep(deps, node):
    for dep in deps:
        if node in dep:
            return True
    return False

def checkFbpDeps(rootPath, samplesData, fbpFiles):
    for fbp in fbpFiles:
        if fbp not in samplesData:
            continue
        fbpPath = os.path.join(rootPath, fbp)
        nodeData = getNodeData(fbpPath)
        nodes = list(map(lambda x: samplesData[fbp]['node_aliases'][x].lower() if x in samplesData[fbp]['node_aliases'] else x.lower(),nodeData[0]))
        #Extra deps.
        if len(nodeData[2]) > 0:
            nodes.extend(nodeData[2])
        #Declared FBPs
        if len(nodeData[1]) > 0:
            for child in nodeData[1]:
                samplesData[child] = samplesData[fbp]
            checkFbpDeps(rootPath, samplesData, nodeData[1])
        missing = list(filter(lambda x: hasDep(samplesData[fbp]['deps'], x.replace('-', '_')) == False, nodes))
        if len(missing) == 0:
            continue
        problems['missing_deps'][fbpPath] = missing

def loadBlackList(path):
    with open(path, 'r') as f:
        return json.load(f)

if __name__ == "__main__":
    exitWithError = False

    argparser = argparse.ArgumentParser(description=""" This script will check if there are any missing
                                        dependencies for the Soletta samples""")

    argparser.add_argument("--samples_root_dir", type=str, help=""" Where the samples are located """, required=True)
    argparser.add_argument("--samples_dependency_check_skip_list", type=str,
                           help="""A JSON array file that contains the samples that should be skipped """, required=True)

    args = argparser.parse_args()

    blackList = loadBlackList(args.samples_dependency_check_skip_list)
    if len(blackList) > 0:
        print('Skipping dependency check for FBPs:' + str(blackList))
    for root, dirs, files in os.walk(args.samples_root_dir):
        fbpFiles = list(filter(lambda x: x.endswith('.fbp') and x not in blackList, files))
        if len(fbpFiles) == 0:
            continue
        try:
            samplesData = getSamplesData(root)
        except FileNotFoundError:
            problems['missing_makefiles'].append(root)
            continue

        checkFbpDeps(root, samplesData, fbpFiles)

    if len(problems['missing_makefiles']) > 0:
        print('---- Start missing makefiles ----')
        print('The folling samples have no makefiles:' + str(problems['missing_makefiles']))
        print('---- End missing makefiles ----')
        exitWithError = True

    if len(problems['missing_deps']) > 0:
        print('\n---- Start missing deps ----')
        for fbp in problems['missing_deps']:
            print('The FBP \'%s\' should declare dependencies for the node types: %s' % (fbp, problems['missing_deps'][fbp]))
        print('---- End missing deps ----')
        exitWithError = True

    if exitWithError:
        sys.exit(-1)
    else:
        sys.exit(0)
