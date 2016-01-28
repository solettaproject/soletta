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

# By default, build soletta using defaults
SOLETTA_CFLAGS="-I$(pwd)/build/soletta_sysroot/usr/include/soletta"
SOLETTA_LIBS="-L$(pwd)/build/soletta_sysroot/usr/lib -lsoletta -Wl,-rpath $(pwd)/build/soletta_sysroot/usr/lib"
BUILD_SOLETTA="true"

# If we're running from soletta's build process, establish the prefix, use
# local include files, and link with no rpath
if test "x${SOLETTA_FROM_MAKE}x" == "xtruex"; then
	PREFIX="$(cat .config | grep '^PREFIX=' | sed -r 's/^[^"]*"([^"]*)"/\1/')"
	PREFIX="${PREFIX##/}"

	SOLETTA_CFLAGS="-I$(pwd)/build/soletta_sysroot/${PREFIX}/include/soletta"
	SOLETTA_LIBS="-L $(pwd)/build/soletta_sysroot/${PREFIX}/lib -lsoletta"
	BUILD_SOLETTA="false"

# Otherwise, try to find soletta via pkg-config
elif pkg-config --exists soletta > /dev/null 2>&1; then
	SOLETTA_CFLAGS="$(pkg-config --cflags soletta)"
	SOLETTA_LIBS="$(pkg-config --libs soletta)"
	BUILD_SOLETTA="false"
fi

if test "x${1}x" == "xBUILD_SOLETTAx"; then
	echo "${BUILD_SOLETTA}"
elif test "x${1}x" == "xSOLETTA_CFLAGSx"; then
	echo "${SOLETTA_CFLAGS}"
elif test "x${1}x" == "xSOLETTA_LIBSx"; then
	echo "${SOLETTA_LIBS}"
fi
