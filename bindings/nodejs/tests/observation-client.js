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

module.exports = function( messagePrefix, desiredObservationCount, clientCount ) {

var _ = require( "lodash" );
var async = require( "async" );
var soletta = require( require( "path" )
	.join( require( "bindings" ).getRoot( __filename ), "lowlevel" ) );
var testUtils = require( "./assert-to-console" );
var payload = require( "./payload" );
var uuid = process.argv[ 2 ];
var observationCount = 0;
var theResource;

console.log( JSON.stringify( { assertionCount: desiredObservationCount + 5 } ) );

var client = soletta.sol_oic_client_new();
var destination = soletta.sol_network_link_addr_from_str( {
		bytes: _.fill( Array( 16 ), 0 ),
		family: soletta.sol_network_family.SOL_NETWORK_FAMILY_INET,
		port: 5683
	}, "224.0.1.187" );

async.series( [
	function findResource( callback ) {
		var result = soletta.sol_oic_client_find_resource( client, destination, "", "",
			function( client, resource ) {
				if ( !resource ) {
					callback( new Error( messagePrefix + "Resource not found" ) );
				}
				if ( resource && resource.href === "/a/" + uuid ) {
					testUtils.assert( "ok", true, messagePrefix + "Resource found" );
					theResource = resource;
					callback();
					return false;
				}
				return !!resource;
			} );
	},

	function observeResource( callback ) {
		var observationHandle = soletta.sol_oic_client_resource_observe( client, theResource,
			function( code, client, address, response ) {
				testUtils.assert( "deepEqual", response, payload.validate( response ),
					messagePrefix + "payload is as expected" );
				observationCount++;
				if ( observationCount === desiredObservationCount ) {
					var typeError = {};
					soletta.sol_oic_client_resource_unobserve( observationHandle );
					try {
						soletta.sol_oic_client_resource_unobserve( observationHandle );
					} catch( theError ) {
						typeError = theError;
					}
					testUtils.assert( "strictEqual", typeError instanceof TypeError, true,
						messagePrefix +
							"Attempting double free of observation throws a type error" );
					testUtils.assert( "strictEqual", typeError.message,
						"Object is not of type SolOicObservation",
						messagePrefix + "Double free error message is as expected" );
					callback();
				}
			} );
	},

	function maybeQuit( callback ) {
		var result = soletta.sol_oic_client_request( client, theResource,
			soletta.sol_coap_method_t.SOL_COAP_METHOD_PUT, { finished: uuid },
			function( code, client, address, response ) {
				if ( response && response.clientsFinished === clientCount ) {
					console.log( JSON.stringify( { killPeer: true } ) );
				}
				callback();
			} );
		testUtils.assert( "strictEqual", result, true, messagePrefix + "PUT request succeeded" );
	}
], function( error ) {
	if ( error ) {
		testUtils.die( error.message );
	}
} );

process.on( "exit", function() {
	testUtils.assert( "strictEqual", observationCount, desiredObservationCount,
		"Exactly the desired number of observations was made" );
} );

};
