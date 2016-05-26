/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

var _ = require( "lodash" );
var soletta = require( "bindings" )( "soletta" );

var cookedConstants = { forward: {}, reverse: {} };

var isInteresting = /^[A-Z0-9_]+$/;

_.each( soletta._sysConstants(), function( value, key ) {
	if ( !key.match( isInteresting ) ) {
		return;
	}

	var forward = cookedConstants.forward;
	var reverse = cookedConstants.reverse;
	var ns = key.split( "_" )[ 0 ];

	ns = ( ns === key ) ?
		( ns.substr( 0, 1 ) === "E" ? "E" :
		ns.substr( 0, 3 ) === "SIG" ? "SIG" : ns )
		: ns;

	if ( !forward[ ns ] ) {
		forward[ ns ] = {};
	}
	forward[ ns ][ key ] = value;

	if ( !reverse[ ns ] ) {
		reverse[ ns ] = {};
	}
	reverse[ ns ][ value ] = key;
} );

soletta._sysConstants( cookedConstants );

module.exports = soletta;
