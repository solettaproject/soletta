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

var soletta = require( require( "path" )
	.join( require( "bindings" ).getRoot( __filename ), "lowlevel" ) );
var testUtils = require( "../assert-to-console" );
var exec = require( "child_process" ).exec;
var fs = require( "fs" );
var childEnv = require( "lodash" ).extend( {}, process.env, { LD_PRELOAD: undefined } );

console.log( JSON.stringify( { assertionCount: 3 } ) );

// Use the UUID we were given as the soletta machine ID
process.env.SOL_MACHINE_ID = process.argv[ 2 ].replace( /[-]/g, "" );
testUtils.assert( "strictEqual", soletta.sol_platform_get_machine_id(),
	process.env.SOL_MACHINE_ID, "Machine ID value is as expected" );

exec( "hostname", { env: childEnv }, function( error, stdout ) {
	if ( error ) {
		testUtils.die( error );
	}
	testUtils.assert( "strictEqual", stdout.toString().trim(),
		soletta.sol_platform_get_hostname(), "Hostname matches `hostname`" );
} );

exec( "uname -r", { env: childEnv }, function( error, stdout ) {
	if ( error ) {
		testUtils.die( error );
	}
	testUtils.assert( "strictEqual", stdout.toString().trim(),
		soletta.sol_platform_get_os_version(), "OS version matches `uname -r`");
} );
