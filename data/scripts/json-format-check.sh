#!/bin/sh

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

INFILE="$1"
PYTHON=${PYTHON:-python3}
SCRIPTSDIR=`dirname $0`
DIFF=`which colordiff 2>/dev/null || which diff 2>/dev/null`

json_fmt() {
    $PYTHON $SCRIPTSDIR/json-format.py "$1" - 2>/dev/null
}

if json_fmt "$INFILE" | cmp "$INFILE"; then
    exit 0
else
    echo "JSON file not properly formatted: $INFILE"
    if [ -n "$DIFF" ]; then
        json_fmt "$INFILE" | $DIFF -u "$INFILE" -
    fi
    exit 1
fi
