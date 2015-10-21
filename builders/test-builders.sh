#!/bin/bash

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

trap 'exit 1;' SIGINT

SCRIPT_DIR=$(dirname $(realpath ${BASH_SOURCE[0]}))

usage() {
    echo "
Usage:  ./$(basename $0) [OPTIONS]

OPTIONS
     -t             soletta target repo.
     -b             soletta target branch.
     -h             soletta host repo.
     -r             soletta host branch.
     -s             skip testing the `sol` tool and builds.
     -p             show this help.

     Host and target repos default to this soletta repo, e.g. ../, while
     host and target branches default to the current checked out branch.
" 1>&$1
}

OPT_SKIP_SOL_BUILDS=0

while getopts "t:b:h:r:p" o; do
    case "${o}" in
        t)
            export SOLETTA_TARGET_REPO="$OPTARG";;
        b)
            export SOLETTA_TARGET_BRANCH="$OPTARG";;
        h)
            export SOLETTA_HOST_REPO="$OPTARG";;
        r)
            export SOLETTA_HOST_BRANCH="$OPTARG";;
	s)
	    export OPT_TEST_SOL_BUILDS=1;;
        p)
            usage 1; exit 0;;
        \?)
            usage 2; exit 1;;
    esac
done

if [[ -z "$SOLETTA_TARGET_REPO" ]]; then
    export SOLETTA_TARGET_REPO="$SCRIPT_DIR/.."
fi

if [[ -z "$SOLETTA_HOST_REPO" ]]; then
    export SOLETTA_HOST_REPO="$SCRIPT_DIR/.."
fi

if [ $OPT_SKIP_SOL_BUILDS -eq 0 ]; then
    command -v go > /dev/null
    if [ $? -eq 1 ]; then
	echo "Couldn't find go to build 'sol' tool. You can skip this by running with '-s' option." >&2
	exit 1
    fi

    pushd $SCRIPT_DIR/sol > /dev/null
    go build
    if [ $? -eq 1 ]; then
	echo "Failed build 'sol' tool." >&2
    fi
    popd > /dev/null
fi

for dir in $SCRIPT_DIR/platform-*/; do
    $dir/prepare

    if [ $? -ne 0 ]; then
	echo "Failed to prepare $dir" >&2
	exit 1
    fi
done

if [ $OPT_SKIP_SOL_BUILDS -eq 1 ]; then
    echo "Skipped building 'sol' tool and using it to build the test programs."
    exit
fi

# Use an alternative port to avoid conflict with any other manually
# ran sol server.
SOL_TOOL="$SCRIPT_DIR/sol/sol -addr=localhost:2223"

SERVER_OUTPUT=$(mktemp)

echo
echo "=== Running build server, output in: $SERVER_OUTPUT"
echo

pushd $SCRIPT_DIR/out > /dev/null
$SOL_TOOL -run-as-server 2>&1 > $SERVER_OUTPUT &

SERVER_PID=$!
popd > /dev/null

trap 'kill $SERVER_PID' EXIT

# Skip the platform-test since it always fail to compile, it's goal is
# to show the environment things would be compiled.

PLATFORMS=$($SOL_TOOL | grep -v 'platform-test')
echo $PLATFORMS | tr ' ' '\n'

for test in $SCRIPT_DIR/tests/*; do
    pushd $test > /dev/null
    for plat in $PLATFORMS; do
	echo
	echo "=== Using 'sol' tool to build '$test' in platform '$plat'"
	echo
	$SOL_TOOL $plat
	if [ $? -eq 1 ]; then
	    echo "Failed building '$test' in platform '$plat'" >&2
	    exit 1
	fi
	# TODO: Check the actual build product!
    done
    popd > /dev/null
done

echo
echo OK!
echo
