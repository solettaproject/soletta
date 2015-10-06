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

if [ -z "$COMPILE_DIR" ]; then
    echo "Compile script need to set COMPILE_DIR before including ${BASH_SOURCE[0]}"
    exit 1
fi

if [ -z "$SOLETTA_TARGET" ]; then
    echo "Compile script need to set SOLETTA_TARGET before including ${BASH_SOURCE[0]}"
    exit 1
fi

if [ ! -d "$SOLETTA_TARGET" ]; then
    echo "Soletta target directory $SOLETTA_TARGET must exist before including ${BASH_SOURCE[0]}"
    exit 1
fi

PLATFORM_NAME=$(echo "$(basename $COMPILE_DIR)" | sed -e 's/platform-//')
SOLETTA_HOST=$COMPILE_DIR/soletta-host/build/soletta_sysroot

if [ -n "$PARALLEL_JOBS" ]; then
    PARALLEL_JOBS=8
fi

trap '
    ret=$?;
    set +e;
    if [[ $ret -ne 0 ]]; then
	echo FAILED TO COMPILE >&2
    fi
    exit $ret;
    ' EXIT

trap 'exit 1;' SIGINT

function die() {
    echo "ERROR: $*" >&2
    exit 1
}

function generate-main-from-fbp() {
    local OPTS="-j $SOLETTA_TARGET/usr/share/soletta/flow/descriptions"
    local CONF_COUNT=$(find -name conf.json | wc -l)

    local SEARCH_FBP_DIRS=$(find -type d -exec echo -I{} \;)
    OPTS="$OPTS $SEARCH_FBP_DIRS"

    if [ $CONF_COUNT -eq 1 ]; then
	local CONF_FILE=$(find -name conf.json)
	OPTS="$OPTS -c $CONF_FILE"
    fi

    if [ $CONF_COUNT -gt 1 ]; then
	die "Found multiple conf files, but only one should be provided: $(find -name conf.json | tr '\n' ' ')"
    fi

    LD_LIBRARY_PATH=$SOLETTA_HOST/usr/lib $SOLETTA_HOST/usr/bin/sol-fbp-generator $OPTS main.fbp main.c
    if [ $? -ne 0  ]; then
	die "Failed generating main.c"
    fi
}

if [ ! -e main.c -a ! -e main.fbp ]; then
    die "Couldn't find program main: 'main.c' or 'main.fbp'"
fi

if [ -e main.c -a -e main.fbp ]; then
    die "Found both 'main.c' and 'main.fbp', only one should be provided"
fi

if [ -e main.fbp ]; then
    generate-main-from-fbp
fi
