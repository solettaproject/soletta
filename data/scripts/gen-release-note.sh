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

TMP_DIR="/tmp"
if [ -d /run/user/$UID ]; then
   TMP_DIR="/run/user/$UID"
fi
NOTE_PREFIX=${TMP_DIR}/`basename $PWD`

COMMITS_SINCE="$1"
CLOSED_SINCE="$2"

COMMITS_LOG="$NOTE_PREFIX-release-commits.txt"
OPEN_ISSUES="$NOTE_PREFIX-release-issues-open.txt"
CLOSED_ISSUES="$NOTE_PREFIX-release-issues-closed.txt"
NOTE="release-note.txt"

if [ -z "$2" ]
then
    echo "Usage: $0 LAST_RELEASE_COMMIT CLOSED_SINCE"
    echo "    CLOSED_SINCE must be in ISO 8601 format: YYYY-MM-DDTHH:MM:SSZ"
    echo "Example.: $0 3baae03daa72e6 2015-08-01T00:00:00Z"
    exit 1
fi

echo "Getting git log..."
git log --oneline $COMMITS_SINCE..HEAD > $COMMITS_LOG

echo "Getting closed issues list..."
git-hub issue list -i -t --closed-after $CLOSED_SINCE > $CLOSED_ISSUES

echo "Getting open issues list..."
#TODO consider since to get only closed after last release
git-hub issue list -i -t > $OPEN_ISSUES

echo "# Release " `git describe  --abbrev=0 --tags` " - " `date +%D` > $NOTE

echo "" >> $NOTE
echo "** WRITE A SHORT DESCRIPTION HERE **" >> $NOTE
echo "" >> $NOTE
echo "" >> $NOTE

CLOSED_COUNT=(`wc -l $CLOSED_ISSUES`)
echo "## Issues resolved in this release ($CLOSED_COUNT):" >> $NOTE
echo "" >> $NOTE
echo "A full listing of issues may be found at https://github.com/solettaproject/soletta/issues. Details about specific issues may be found at https://github.com/solettaproject/soletta/issues/ISSUE_NUMBER." >> $NOTE
echo "" >> $NOTE
cat $CLOSED_ISSUES >> $NOTE
echo "" >> $NOTE
echo "" >> $NOTE

OPEN_COUNT=(`wc -l $OPEN_ISSUES`)
echo "## Open issues ($OPEN_COUNT):" >> $NOTE
echo "" >> $NOTE
echo "A full listing of issues may be found at https://github.com/solettaproject/soletta/issues. Details about specific issues may be found at https://github.com/solettaproject/soletta/issues/ISSUE_NUMBER." >> $NOTE
echo "" >> $NOTE
cat $OPEN_ISSUES >> $NOTE
echo "" >> $NOTE
echo "" >> $NOTE

COMMITS_COUNT=(`wc -l $COMMITS_LOG`)
echo "## Changes in this release ($COMMITS_COUNT commits):" >> $NOTE
echo "" >> $NOTE
echo "A full listing of commits may be found at https://github.com/solettaproject/soletta/commits/master. Details about specific commits may be found at https://github.com/solettaproject/soletta/commit/COMMIT_HASH." >> $NOTE
echo "" >> $NOTE
cat $COMMITS_LOG >> $NOTE

"${EDITOR:-vi}" $NOTE
