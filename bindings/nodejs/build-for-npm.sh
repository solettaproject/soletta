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

# This script covers the use case where the user installs soletta via npm. The
# actual build happens via Soletta's own build system, but before we fire it up
# we configure it to make sure the node.js bindings get built and we handle the
# case where npm wants a debug build by turning on Soletta's own debug build.

if test "x${V}x" != "xx"; then
	set -x
fi

# Node.js seems to poison these
unset PYTHON || exit 1
unset PYTHON_PATH || exit 1

# Configure with defaults
make alldefconfig || exit 1

# Turn on node.js bindings and build with RPATH
sed -i .config -r -e 's/(# )?USE_NODEJS.*$/USE_NODEJS=y/'
export RPATH="y"

if test "x${npm_config_debug}x" = "xtruex"; then

	# If debug is on, turn off release build and optimization, and turn on debug build and symbols
	sed -i .config -r -e 's/(# )?BUILD_TYPE_DEBUG.*$/BUILD_TYPE_DEBUG=y/'
	sed -i .config -r -e 's/(# )?BUILD_TYPE_RELEASE.*$/# BUILD_TYPE_RELEASE is not set/'
fi

# Finally, we fire up the build
make || exit 1
