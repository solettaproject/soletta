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

if test "x${V}x" != "xx"; then
	set -x
fi

DO_DEBUG=false

while test $# -gt 0; do
	if test "x$1x" = "x--debugx" -o "x$1x" = "x-dx"; then
		DO_DEBUG=true
	elif test "x$1x" = "x--helpx" -o "x$1x" = "x-hx"; then
		echo "$( basename "$0" ) [options...]"
		echo ""
		echo "Possible options:"
		echo "--debug or -d : Build in debug mode"
		exit 0
	fi
	shift
done

unset PYTHON || exit 1
unset PYTHON_PATH || exit 1
make alldefconfig || exit 1
if test "x${DO_DEBUG}x" == "xtruex"; then
	( cat .config | awk '{
		if ( $0 ~ /^CONFIG_CFLAGS=/ ) {
			print( "CONFIG_CFLAGS=\"-g -Wall -O0\"" );
		} else {
			print;
		}
	}' > .config.new && mv .config.new .config ) || exit 1
fi
make || exit 1
