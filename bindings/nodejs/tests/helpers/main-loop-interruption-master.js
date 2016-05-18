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

module.exports = function parentProcess( childProcessWontQuit ) {
	console.log( JSON.stringify( { assertionCount: 2 } ) );
	var childHasReceivedSIGINT = false;
	var childHadToBeKilled = false;
	var utils = require( "../assert-to-console" );
	var theChild = require( "child_process" ).spawn( "node",
		[ __filename, childProcessWontQuit ? "noCleanup" : "cleanup" ], {
		stdio: [ process.stdin, "pipe", process.stderr ]
	} );
	var drawConclusion = function() {
		utils.assert( "strictEqual", childHasReceivedSIGINT, true,
			"Child process has received SIGINT" );
		utils.assert( "strictEqual", childHadToBeKilled, childProcessWontQuit,
			"Child process termination was as expected" );
	};
	var failsafeTimeout = setTimeout( function() {
		childHadToBeKilled = true;
		theChild.kill( "SIGKILL" );
	}, 5000 );
	theChild.on( "close", function() {
		clearTimeout( failsafeTimeout );
		drawConclusion();
	} );
	theChild.stdout.on( "data", function( data ) {
		var index, oneLine, theMessage = {};
		var lines = data.toString().split( "\n" );
		for ( index in lines ) {
			oneLine = lines[ index ];
			if ( !oneLine ) {
				continue;
			}
			try {
				theMessage = JSON.parse( oneLine );
			} catch ( anError ) {}
			if ( theMessage.SIGINT ) {
				childHasReceivedSIGINT = true;
			} else if ( theMessage.readyToDie ) {
				theChild.kill( "SIGINT" );
			}
		}
	} );
};

if ( require.main === module ) {

	// We were run directly, instead of via require(), so we are the child process
	var soletta = require( require( "path" )
		.join( require( "bindings" ).getRoot( __filename ), "lowlevel" ) );
	var monitor = soletta.sol_platform_add_hostname_monitor( function() {} );
	process.on( "SIGINT", function() {
		console.log( JSON.stringify( { SIGINT: true } ) );
		if ( process.argv[ 2 ] === "cleanup" ) {
			soletta.sol_platform_del_hostname_monitor( monitor );
		}
	} );

	// It is essential that we attempt to print something out on the stderr to reproduce the issue
	console.error( "" );
	console.log( JSON.stringify( { readyToDie: true } ) );
}
