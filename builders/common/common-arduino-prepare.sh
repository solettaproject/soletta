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

cd $COMPILE_DIR

git clone --branch 2015.09 https://github.com/RIOT-OS/RIOT.git
if [ $? -ne 0 ]; then
    exit 1
fi

mkdir -p RIOT/pkg/libsoletta
cp $PREPARE_DIR/Makefile.soletta RIOT/pkg/libsoletta/Makefile
cp $PREPARE_DIR/Makefile.include RIOT/pkg/libsoletta/

mkdir -p RIOT/examples/soletta_app_base
cp $PREPARE_DIR/Makefile.app RIOT/examples/soletta_app_base/Makefile

cp -r $COMPILE_DIR/RIOT/examples/soletta_app_base $COMPILE_DIR/RIOT/examples/soletta_app_dummy
cat > $COMPILE_DIR/RIOT/examples/soletta_app_dummy/main.c <<EOF
#include "sol-mainloop.h"
static void startup(void) { sol_quit(); }
static void shutdown(void) {}
SOL_MAIN_DEFAULT(startup, shutdown);
EOF
make -j $PARALLEL_JOBS WERROR=0 -C $COMPILE_DIR/RIOT/examples/soletta_app_dummy
if [ $? -ne 0 ]; then
    exit 1
fi

cp \
    $PREPARE_DIR/flash.sh \
    $PREPARE_DIR/../common/common-compile.sh \
    $PREPARE_DIR/../common/common-arduino-compile.sh \
    $PREPARE_DIR/compile \
    $COMPILE_DIR

chmod +x $COMPILE_DIR/compile
