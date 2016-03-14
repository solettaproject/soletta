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

if test "x${V}x" != "xx"; then
	set -x
fi

# Node.js seems to poison these
unset PYTHON || exit 1
unset PYTHON_PATH || exit 1

# Configure with defaults
make alldefconfig || exit 1

if test "x${npm_config_debug}x" = "xtruex"; then
	sed -i .config -r -e 's/(# )?CONFIG_CFLAGS.*$/CONFIG_CFLAGS="-g -O0"/'
fi

USE_NODEJS="y" RPATH="y" make || exit 1
