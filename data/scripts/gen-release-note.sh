#!/bin/sh

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

echo "Release " `git describe  --abbrev=0 --tags` " - " `date +%D` > $NOTE

echo "" >> $NOTE
echo "** WRITE A SHORT DESCRIPTION HERE **" >> $NOTE
echo "" >> $NOTE
echo "" >> $NOTE

CLOSED_COUNT=(`wc -l $CLOSED_ISSUES`)
echo "Issues resolved in this release ($CLOSED_COUNT):" >> $NOTE
echo "" >> $NOTE
cat $CLOSED_ISSUES >> $NOTE
echo "" >> $NOTE
echo "" >> $NOTE

OPEN_COUNT=(`wc -l $OPEN_ISSUES`)
echo "Open issues ($OPEN_COUNT):" >> $NOTE
echo "" >> $NOTE
cat $OPEN_ISSUES >> $NOTE
echo "" >> $NOTE
echo "" >> $NOTE

COMMITS_COUNT=(`wc -l $COMMITS_LOG`)
echo "Changes in this release ($COMMITS_COUNT commits):" >> $NOTE
echo "" >> $NOTE
cat $COMMITS_LOG >> $NOTE

"${EDITOR:-vi}" $NOTE
