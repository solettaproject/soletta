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

var success = "\x1b[42;30m✓\x1b[0m",
	failure = "\x1b[41;30m✗\x1b[0m",
	QUnit = require( "qunitjs" ),
	results = require( "./getresult" );

// Right-align runtime in a field that's 10 columns wide
function formatRuntime( runtime ) {
	var index,
		str = "" + runtime,
		indent = "";

	for ( index = 0; index < Math.max( 0, 10 - str.length ); index++ ) {
		indent += " ";
	}

	return indent + str;
}

QUnit.load();
QUnit.config.requireExpects = true;
QUnit.config.testTimeout = 300000;
QUnit.config.callbacks.moduleStart.push( function( status ) {

	// Parameters: status: { name, tests }

	if ( status.name ) {
		console.log( "\n### " + status.name );
	}
} );
QUnit.config.callbacks.testStart.push( function( status ) {

	// Parameters: status: { name, module, testId }

	if ( status.name ) {
		console.log( "\n" + status.name );
	}
} );
QUnit.config.callbacks.log.push( function( status ) {

	// Parameters: status: { module, result(t/f), message, actual, expected, testId, runtime }

	results.getTestResult( status );

	console.log(
		( status.result ? success : failure ) +
		" @" + formatRuntime( status.runtime ) + ": " +
		status.message );
	if ( !status.result ) {
		console.log( "Actual: " );
		console.log( QUnit.dump.parse( status.actual ) );
		console.log( "Expected: " );
		console.log( QUnit.dump.parse( status.expected ) );
	}
} );

QUnit.config.callbacks.done.push( function( status ) {
	var passed = "\x1b[42;30m " + status.passed + " \x1b[0m",
		failed = "\x1b[41;30m " + status.failed + " \x1b[0m";

	console.log( "Total assertions: " +
		"(" + passed + ( status.failed > 0 ? "+" + failed : "" ) + ") / " + status.total );

	process.exit( status.failed > 0 ? 1 : 0 );
} );

module.exports = QUnit;
