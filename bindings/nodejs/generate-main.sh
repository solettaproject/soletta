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
FILES=\
'sol-platform.h'

for file in $FILES; do
	echo "#include <$file>" >> bindings/nodejs/generated/main.h
	for path in $SOLETTA_SEARCH_PATHS; do
		if test -f $path/$file; then
			cat $path/$file | awk '
				BEGIN {
					enum_values[0] = 0;
					delete enum_values[0];
					enum_name = "";
					inside_enum = 0;
					new_enum = 0;
					last_line_was_blank=0;
				}
				/^#define/ {
					if ( NF > 2 && $2 ~ /^[A-Za-z_][_A-Za-z0-9]*$/ ) {
						print "  SET_CONSTANT_" ( ( substr($3, 1, 1) == "\"" ) ? "STRING": "NUMBER" ) "(target, " $2 ");" >> "bindings/nodejs/generated/main.cc"
						last_line_was_blank = 0;
					}
				}
				/^(typedef\s+)?enum\s+[^{]*{$/ {
					enum_name = ( $2 == "enum" ) ? $3 : $2;
					gsub(/{/, "", enum_name);
					inside_enum = 1;
					new_enum = 1;
				}
				/\s*}\s*(\S*)?\s*;\s*$/ {
					if ( inside_enum == 1 ) {
						if ( enum_name == "" ) {
							enum_name = $0;
							gsub(/(\s|[};])/, "", enum_name);
						}
						if ( enum_name != "" ) {
							if ( last_line_was_blank == 0 ) {
								print "" >> "bindings/nodejs/generated/main.cc"
							}
							print "  Local<Object> bind_" enum_name " = Nan::New<Object>();" >> "bindings/nodejs/generated/main.cc"
							for ( enum_value in enum_values ) {
								print "  SET_CONSTANT_NUMBER(bind_" enum_name ", " enum_value ");" >> "bindings/nodejs/generated/main.cc"
							}
							for ( enum_value in enum_values ) {
								delete enum_values[ enum_value ];
							}
							print "  SET_CONSTANT_OBJECT(target, " enum_name ");" >> "bindings/nodejs/generated/main.cc"
							print "" >> "bindings/nodejs/generated/main.cc"
							last_line_was_blank = 1;
						}
						enum_name = "";
						inside_enum = 0;
					}
				}
				{
					if ( new_enum == 1 ) {
						new_enum = 0;
					}
					else
					if ( inside_enum == 1 ) {
						enum_member = $1;
						gsub( /,/, "", enum_member );
						if ( enum_member ~ /[A-Za-z][A-Za-z0-9]*/ ) {
							enum_values[ enum_member ] = 0;
						}
					}
				}
			'
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
