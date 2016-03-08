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

var QUnit,
	async = require( "async" ),
	glob = require( "glob" ),
	_ = require( "lodash" ),
	childProcess = require( "child_process" ),
	fs = require( "fs" ),
	path = require( "path" ),
	uuid = require( "uuid" ),
	suiteOptions = _.extend( {

		// Default options
		prefix: "tests",
	}, require( "yargs" ).argv ),
	runningProcesses = [],
	getQUnit = function() {
		if ( !QUnit ) {
			QUnit = require( "./setup" );
		}
		return QUnit;
	};

function havePromises() {
	var nodeVersion = _.map(
		process.versions.node.split( "." ),
		function( item ) {
			return +item;
		} );

	return ( nodeVersion.length > 1 &&
		( nodeVersion[ 0 ] > 0 ||
		nodeVersion[ 0 ] === 0 && nodeVersion[ 1 ] > 11 ) );
}

// Spawn a single child and process its stdout.
function spawnOne( assert, options ) {
	var theChild = childProcess.spawn(
		"node",
		[ "--expose_gc", options.path ].concat( options.uuid ? [ options.uuid ] : [] ),
		{
			stdio: [ process.stdin, "pipe", process.stderr ],
			env: _.extend( {}, process.env,
				suiteOptions.ldPreload ? { LD_PRELOAD: suiteOptions.ldPreload } : {} )
		} );

	theChild.commandLine = "node" + " " + options.path + " " + options.uuid;
	runningProcesses.push( theChild );

	theChild
		.on( "exit", function( code, signal ) {
			var exitCodeOK = ( code === 0 || code === null ),
				signalOK = ( signal !== "SIGSEGV" );

			assert.ok( exitCodeOK, options.name + " exited successfully (" + code + ")" );
			assert.ok( signalOK, options.name + " did not segfault" );
		} )
		.on( "close", function() {
			var childIndex = runningProcesses.indexOf( theChild );
			if ( childIndex >= 0 ) {
				runningProcesses.splice( childIndex, 1 );
			}
			options.maybeQuit( theChild );
		} );

	// The stdout of the child is a sequence of \n-separated stringified JSON objects.
	theChild.stdout.on( "data", function serverStdoutData( data ) {
		_.each( data.toString().split( "\n" ), function( value ) {
			var jsonObject;

			if ( !value ) {
				return;
			}

			// Attempt to retrieve a JSON object from stdout.
			try {
				jsonObject = JSON.parse( value );
			} catch ( e ) {
				options.teardown( "Error parsing " + options.name + " JSON: '" + value + "'" +
					( e.message ? e.message : e ), true );
				return;
			}

			// The child is reporting the number of assertions it will be making. We add our own
			// two assertions ( 1.) successful exit and 2.) no segfault) to that count.
			if ( jsonObject.assertionCount !== undefined ) {
				options.reportAssertions( jsonObject.assertionCount + 2 );

			// The child has requested a teardown.
			} else if ( jsonObject.teardown ) {
				options.teardown(
					options.name + " requested teardown: " + jsonObject.message );

			// The child has requested that its peer be killed.
			} else if ( jsonObject.killPeer ) {
				options.teardown( null, theChild );

			// The child is reporting that it is ready. Only servers do this.
			} else if ( jsonObject.ready ) {
				if ( options.onReady ) {
					options.onReady();
				}

			// The child is making an assertion.
			} else if ( jsonObject.assertion ) {
				assert[ jsonObject.assertion ].apply( assert, jsonObject.arguments );

			// Otherwise, we have received unknown JSON from the child - bail.
			} else {
				options.teardown( "Unknown JSON from " + options.name + ": " + value, true );
			}
		} );
	} );

	return theChild;
}

function runTestSuites( files ) {
	_.each( files, function( item ) {
		var clientPathIndex,
			clientPaths = glob.sync( path.join( item, "client*.js" ) ),
			serverPath = path.join( item, "server.js" );

		if ( fs.lstatSync( item ).isFile() ) {
			getQUnit().test( path.basename( item ).replace( /\.js$/, "" ), function( assert ) {
				var theChild,
					spawnOptions = {
						uuid: uuid.v4(),
						name: "Test",
						path: item,
						teardown: function( error ) {
							if ( theChild ) {
								theChild.kill( "SIGTERM" );
							}
							if ( error ) {
								throw error;
							}
						},
						maybeQuit: assert.async(),
						reportAssertions: _.bind( assert.expect, assert )
					};
				theChild = spawnOne( assert, spawnOptions );
			} );
			return;
		}

		if ( !fs.lstatSync( item ).isDirectory() ) {
			return;
		}

		for ( clientPathIndex in clientPaths ) {
			if ( !( fs.lstatSync( clientPaths[ clientPathIndex ] ).isFile() ) ) {
				throw new Error( "Cannot find client at " + clientPaths[ clientPathIndex ] );
			}
		}

		if ( !( fs.lstatSync( serverPath ).isFile() ) ) {
			throw new Error( "Cannot find server at " + serverPath );
		}

		getQUnit().test( path.basename( item ), function( assert ) {
			var totalChildren = clientPaths.length + 1,

				// Track the child processes involved in this test in this array
				children = [],

				// Turn this test async
				done = assert.async(),

				// Count assertions made by the children. Report them to assert.expect() when both
				// children have reported their number of assertions.
				totalAssertions = 0,
				childrenAssertionsReported = 0,

				spawnOptions = {
					uuid: uuid.v4(),
					teardown: function( error, sourceProcess ) {
						var index,
							signal = error ? "SIGTERM" : "SIGINT",

							// When killing child processes in a loop we have to copy the array
							// because it may become modified by the incoming notifications that a
							// process has exited.
							copyOfChildren = children.slice();

						for ( index in copyOfChildren ) {
							if ( sourceProcess && sourceProcess === copyOfChildren[ index ] ) {
								continue;
							}
							copyOfChildren[ index ].kill( signal );
						}

						if ( error ) {
							throw new Error( error );
						}
					},
					maybeQuit: function( theChild ) {
						var childIndex = children.indexOf( theChild );
						if ( childIndex >= 0 ) {
							children.splice( childIndex, 1 );
						}
						if ( children.length === 0 ) {
							done();
						}
					},
					reportAssertions: function( assertionCount ) {
						childrenAssertionsReported++;
						totalAssertions += assertionCount;
						if ( childrenAssertionsReported === totalChildren ) {
							assert.expect( totalAssertions );
						}
					}
				};

			// We run the server first, because the server has to be there before the clients
			// can run. OTOH, the clients may initiate the termination of the test via a non-error
			// teardown request.
			children.push( spawnOne( assert, _.extend( {}, spawnOptions, {
				name: "Server",
				path: serverPath,
				onReady: function() {
					var clientIndex = 0;
					async.eachSeries( clientPaths, function startOneChild( item, callback ) {
						children.push( spawnOne( assert, _.extend( {}, spawnOptions, {
							name: "Client" +
								( clientPaths.length > 1 ? " " + ( ++clientIndex ) : "" ),
						path: item } ) ) );

						// Spawn clients at least two seconds apart to avoid message uniqueness
						// issue in iotivity: https://jira.iotivity.org/browse/IOT-724
						setTimeout( callback, 2000 );
					} );
				}
			} ) ) );
		} );
	} );
}

// Run tests. If no tests were specified on the command line, we scan the tests directory and run
// all the tests we find therein.
runTestSuites( ( suiteOptions.testList ?
	( _.map( suiteOptions.testList.split( "," ), function( item ) {
		return path.join( __dirname, suiteOptions.prefix, item );
	} ) ) :
	( glob.sync( path.join( __dirname, suiteOptions.prefix,
		( havePromises() ? "" : "!(API)" ) + "*" ) ) ) ) );

function processIsDying( anError, exitSignal ) {
	var childIndex;

	if ( anError && typeof anError !== "number" ) {
		console.error( anError.message + anError.stack );
	}

	for ( childIndex in runningProcesses ) {
		runningProcesses[ childIndex ].kill( "SIGKILL" );
	}
}

process.on( "SIGINT", function() {
	processIsDying();
	process.exit( 0 );
} );
process.on( "exit", processIsDying );
process.on( "uncaughtException", function( anError ) {
	processIsDying( anError );
	process.exit( 0 );
} );
