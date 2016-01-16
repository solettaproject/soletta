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

# Helper script for binding.gyp that determines whether we need to build
# soletta first, and that attempts to establish CFLAGS and LIBS for the
# Node.js bindings.

BUILD_SOLETTA="false"

# If we do not find adequate soletta CFLAGS or LIBS, try pkg-config
if test "x${SOLETTA_CFLAGS}x" = "xx" -o "x${SOLETTA_LIBS}x" = "xx"; then
	SOLETTA_CFLAGS="$(pkg-config --cflags soletta 2>/dev/null)"
	SOLETTA_LIBS="$(pkg-config --libs soletta 2>/dev/null)"
fi

# If we still don't have soletta CFLAGS or LIBS, we need to build soletta
if test "x${SOLETTA_CFLAGS}x" = "xx" -o "x${SOLETTA_LIBS}x" = "xx"; then
	BUILD_SOLETTA="true"
fi

if test "x${1}x" = "xBUILD_SOLETTAx"; then
	echo "${BUILD_SOLETTA}"
elif test "x${1}x" = "xSOLETTA_CFLAGSx"; then
	echo "${SOLETTA_CFLAGS}"
elif test "x${1}x" = "xSOLETTA_LIBSx"; then
	echo "${SOLETTA_LIBS}"
fi
