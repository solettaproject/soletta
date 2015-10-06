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

if [ -z "$PREPARE_DIR" ]; then
    echo "Prepare script need to set PREPARE_DIR before including ${BASH_SOURCE[0]}"
    exit 1
fi

PLATFORM_NAME=$(echo "$(basename $PREPARE_DIR)" | sed -e 's/platform-//')
COMPILE_DIR=$PREPARE_DIR/../out/platform-$PLATFORM_NAME

if [ -n "$PARALLEL_JOBS" ]; then
    PARALLEL_JOBS=8
fi

trap '
    ret=$?;
    set +e;
    if [[ $ret -ne 0 ]]; then
	echo FAILED TO PREPARE >&2
    fi
    exit $ret;
    ' EXIT

trap 'exit 1;' SIGINT

rm -rf $COMPILE_DIR
mkdir -p $COMPILE_DIR
cd $COMPILE_DIR

git clone https://github.com/solettaproject/soletta.git soletta-host
if [ $? -ne 0 ]; then
    exit 1
fi

pushd soletta-host

make alldefconfig
if [ $? -ne 0 ]; then
    exit 1
fi

make -j $PARALLEL_JOBS build/soletta_sysroot/usr/bin/sol-fbp-generator
if [ $? -ne 0 ]; then
    exit 1
fi

popd
