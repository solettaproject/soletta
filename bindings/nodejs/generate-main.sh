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

SOLETTA_SEARCH_PATHS=$(node -p '
	( "'"$(echo "$@" | sed 's/"/\\"/g')"'"
		.match( /-I\s*\S+/g ) || [] )
		.map( function( item ) {
			return item.replace( /-I\s*/, "" );
		} )
		.join( " " );
')

cat bindings/nodejs/generated/main.cc.prologue > bindings/nodejs/generated/main.cc || exit 1
cat bindings/nodejs/generated/main.h.prologue > bindings/nodejs/generated/main.h || exit 1

# Add constants and enums from selected files
FILES='
sol-platform.h
sol-network.h
sol-oic-client.h
sol-oic-common.h
sol-coap.h
sol-gpio.h
'

for file in $FILES; do
	if test "x${file}x" = "xx"; then
		continue
	fi
	echo "#include <$file>" >> bindings/nodejs/generated/main.h
	for path in $SOLETTA_SEARCH_PATHS; do
		if test -f $path/$file; then
			cat $path/$file | awk -f bindings/nodejs/generate-main.awk
		fi
	done
done

echo "" >> "bindings/nodejs/generated/main.h"

# Add all the bound functions
find bindings/nodejs/src -type f | while read filename; do
	cat "${filename}" | grep '^NAN_METHOD' | while read method; do
		echo "${method}" | sed 's/).*$/);/' >> bindings/nodejs/generated/main.h
		echo "${method}" | sed -r 's/^\s*NAN_METHOD\s*\(\s*bind_([^)]*).*$/  SET_FUNCTION(target, \1);/' >> bindings/nodejs/generated/main.cc
	done
done

cat bindings/nodejs/generated/main.cc.epilogue >> bindings/nodejs/generated/main.cc || exit 1
cat bindings/nodejs/generated/main.h.epilogue >> bindings/nodejs/generated/main.h || exit 1
