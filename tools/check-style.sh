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

function die()
{
    echo "ERROR: $@"
    exit 1
}

function cleanup()
{
    for f in $(cat $DIFF_LIST); do
        if [ -e "$f.uncrustify" ]; then
            rm "$f.uncrustify"
        fi
    done
}

RET=0
UNCRUSTIFY=$(which uncrustify 2>/dev/null)
DIFF=$(which colordiff 2>/dev/null || which diff 2>/dev/null)
DIFF_LIST=$(mktemp)
UNCRUSTIFY_ERROR=$(mktemp)
SUPPORTED_VERSION="0.60"

trap "rm -f $DIFF_LIST $UNCRUSTIFY_ERROR" EXIT

function usage()
{
    echo "usage: $0 [-b <base_commit>] [-h]"
}

while getopts b:h o
do case "$o" in
       b) BASE_COMMIT="$OPTARG";;
       h) usage
          exit 0;;
       [?]) usage
          exit 1;;
   esac
done

if [ -z "$UNCRUSTIFY" ]; then
    die "Uncrustify tool is not present, can't check code format."
fi

UNCRUSTIFY_VERSION=$($UNCRUSTIFY --version | cut -d ' ' -f2)

if [ ! "$UNCRUSTIFY_VERSION" = "$SUPPORTED_VERSION" ]; then
    die "Uncrustify tool must be at $SUPPORTED_VERSION version, exactly (found $UNCRUSTIFY_VERSION)."
fi

if [ -z "$DIFF_LIST" ]; then
    die "Failed to create temporary file to store code diffs."
fi

git diff --diff-filter=ACMR --oneline --name-only | grep --color=never '^.*\.[ch]$' > $DIFF_LIST
git diff --cached --diff-filter=ACMR --oneline --name-only | grep --color=never '^.*\.[ch]$' >> $DIFF_LIST

if [ -n "$BASE_COMMIT" -a -s "$DIFF_LIST" ]; then
    echo "Commits since $BASE_COMMIT marked to be checked, but the git tree is dirty -- checking these files instead"
    BASE_COMMIT=""
fi

if [ ! -s "$DIFF_LIST" ]; then
    if [ -z "$BASE_COMMIT" ]; then
        BASE_COMMIT="HEAD~1"
    fi
    echo "Working directory is clean, checking commit changes since $BASE_COMMIT"
    git diff --diff-filter=ACMR --oneline --name-only $BASE_COMMIT HEAD | grep --color=never '^.*\.[ch]$' > $DIFF_LIST
fi

cleanup

$UNCRUSTIFY -c data/schemas/uncrustify.schema -l C -F $DIFF_LIST &> $UNCRUSTIFY_ERROR
if [ $? != 0 ]; then
    echo "ERROR: Failed to run uncrustify tool, output below:"
    echo
    cat $UNCRUSTIFY_ERROR
    echo
    cleanup
    exit 1
fi

for f in $(cat $DIFF_LIST); do
    if [ ! -e "$f.uncrustify" ]; then
        echo "WARNING: Expected $f.uncrustify not found, skipping this file."
        continue
    else
        FDIF=$($DIFF -u $f "$f.uncrustify")
        if [ -n "$FDIF" ]; then
            echo "File not properly formatted: $f"
            echo "$FDIF"
            RET=1
        else
            echo "File formatting OK: $f"
        fi
    fi
done

cleanup

exit $RET
