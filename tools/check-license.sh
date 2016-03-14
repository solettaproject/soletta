#!/bin/bash

# This file is part of the Soletta Project
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

function die() {
    echo "ERROR: $1"
    exit 1
}

[ -e ./Kconfig ] || die "Call from the root directory."

DIFF_LIST=$(mktemp /tmp/sol-tmp.XXXX)

PATTERNS=".*\.*\([ch]\|py\|h\.in\|py\.in\|fbp\|sh\|json\|COPYING\|calc\-lib\-size\|generate\-svg\-from\-all\-fbps\)$"
IGNORE="src\/thirdparty\/\|tools\/kconfig\/\|data\/oic\/\|data\/jsons\/\|.*\.ac|.*Makefile.*\|.*-gen.h\.in"

trap "rm -f $DIFF_LIST" EXIT

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

git diff --diff-filter=ACMR --oneline --name-only | grep $PATTERNS | sed '/'${IGNORE}'/d' > $DIFF_LIST
git diff --cached --diff-filter=ACMR --oneline --name-only | grep $PATTERNS | sed '/'${IGNORE}'/d' >> $DIFF_LIST

if [ -n "$BASE_COMMIT" -a -s "$DIFF_LIST" ]; then
    echo "Commits since $BASE_COMMIT marked to be checked, but the git tree is dirty -- checking these files instead"
    BASE_COMMIT=""
fi

if [ ! -s "$DIFF_LIST" ]; then
    if [ -z "$BASE_COMMIT" ]; then
        BASE_COMMIT="HEAD~1"
    fi
    echo "Working directory is clean, checking commit changes since $BASE_COMMIT"
    git diff --diff-filter=ACMR --oneline --name-only $BASE_COMMIT HEAD | grep $PATTERNS | sed '/'${IGNORE}'/d' > $DIFF_LIST
fi

EXIT_CODE=0
for f in $(cat $DIFF_LIST); do
    r=0
    if [ ${f##*.} = "json" ]; then
        # There is no need of license in config files
        if [ $(grep -c -m 1 "config.schema" $f) -ne 0 ]; then
            continue;
        fi
        r=$(grep -c -m 1 "\"license\": \"BSD-3-Clause\"" $f);
    else
        r=$(grep -c -m 1 "This file is part of the Soletta Project" $f)
    fi
    if [ $r -eq 0 ]; then
        echo "$f has license issues"
        EXIT_CODE=1
    fi
done
exit $EXIT_CODE
