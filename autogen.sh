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

/usr/bin/env python3 data/oic/oicgen.py \
             data/oic \
             src/modules/flow/oic/oic.json \
             src/modules/flow/oic/oic.c || exit 1

/usr/bin/env python3 data/scripts/sol-modules-mk-gen.py \
             --built-sources-suffix=-gen.c \
             --built-sources-suffix=-gen.h \
             --if-module-rule="flowtypedescs += @path@.json" \
             --extra-rule="SOLETTA_FLOW_TYPE_JSONS += @path@/@name@.json" \
             --am-conditional=FLOW \
             --ltlibraries=flowmodules \
             --if-module-cflags="-DSOL_FLOW_NODE_TYPE_MODULE_EXTERNAL=1" \
             --outfile=sol_flow_modules.mk \
             --modules-path=src/modules/flow || exit 1

/usr/bin/env python3 data/scripts/sol-modules-mk-gen.py \
             --am-conditional=PLATFORM_LINUX_MICRO \
             --ltlibraries=linuxmicromodules \
             --if-module-cflags="-DSOL_PLATFORM_LINUX_MICRO_MODULE_EXTERNAL=1" \
             --outfile=sol_linux_micro_modules.mk \
             --modules-path=src/modules/linux-micro || exit 1

autoreconf -f -i || exit 1

if [ -z "$NOCONFIGURE" ]; then
    ./configure "$@"
fi
