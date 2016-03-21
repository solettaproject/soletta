#!/bin/sh

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
