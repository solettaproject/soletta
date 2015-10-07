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

usage() {
    echo "
Usage:  ./$(basename $0) [OPTIONS]

OPTIONS
     -t             soletta target repo.
     -b             soletta target branch.
     -h             soletta host repo.
     -r             soletta host branch.
     -p             show this help.

     Host and target repos default to this soletta repo, e.g. ../, while
     host and target branches default to the current checked out branch.
" 1>&$1
}

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
        p)
            usage 1; exit 0;;
        \?)
            usage 2; exit 1;;
    esac
done

if [[ -z "$SOLETTA_TARGET_REPO" ]]; then
    export SOLETTA_TARGET_REPO="${PWD}/.."
fi

if [[ -z "$SOLETTA_HOST_REPO" ]]; then
    export SOLETTA_HOST_REPO="${PWD}/.."
fi

for dir in */; do
    if [[ $dir =~ platform-* ]]; then
        $dir/prepare
    fi
done
