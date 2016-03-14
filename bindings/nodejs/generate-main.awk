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

# Not using \s in this script because mawk, which runs on semaphore, doesn't understand it.
# Using [ 	] instead (that's a class consisting of a space and a tab character)

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
/^(typedef[ 	]+)?enum[ 	]+[^{]*{$/ {
	enum_name = ( $2 == "enum" ) ? $3 : $2;
	gsub(/{/, "", enum_name);
	inside_enum = 1;
	new_enum = 1;
}
/[ 	]*}[ 	]*([^ 	]*)?[ 	]*;[ 	]*$/ {
	if ( inside_enum == 1 ) {
		if ( enum_name == "" ) {
			enum_name = $0;
			gsub(/([ 	]|[};])/, "", enum_name);
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
