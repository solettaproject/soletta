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

var messagePrefix = "Client 2: ";
var async = require( "async" );
var _ = require( "lodash" );
var soletta = require( require( "path" )
	.join( require( "bindings" ).getRoot( __filename ), "lowlevel" ) );
var testUtils = require( "../../assert-to-console" );
var discoveryCallbackCount = 0;
var theResource;

console.log( JSON.stringify( { assertionCount: 3 } ) );

var defaultAddress = {
	bytes: _.fill( Array( 16 ), 0 ),
	family: soletta.sol_network_family.SOL_NETWORK_FAMILY_INET,
	port: 0
};

var client = soletta.sol_oic_client_new();
var destination = soletta.sol_network_link_addr_from_str( {
		bytes: _.fill( Array( 16 ), 0 ),
		family: soletta.sol_network_family.SOL_NETWORK_FAMILY_INET,
		port: 5683
	}, "224.0.1.187" );

var defaultAddress = {
	bytes: _.fill( Array( 16 ), 0 ),
	family: soletta.sol_network_family.SOL_NETWORK_FAMILY_INET,
	port: 0
};

async.series( [
	function waitForDiscoveryToComplete( callback ) {
		soletta.sol_oic_client_find_resources( client, destination, "", "",
			function( client, resource ) {
				discoveryCallbackCount++;
				if ( resource && resource.path === "/a/" + process.argv[ 2 ] ) {
					theResource = resource;
				}
				if ( !resource ) {
					testUtils.assert( "ok", true,
						messagePrefix + "Discovery callback called with null" );
					callback();
				}
				return true;
			} );
	},
	function tellServerImDone( callback ) {
		soletta.sol_oic_client_request( client, theResource,
			soletta.sol_coap_method.SOL_COAP_METHOD_PUT, { uuid: process.argv[ 2 ] },
				function( code, client, address, response ) {
					testUtils.assert( "strictEqual", code,
						soletta.sol_coap_response_code.SOL_COAP_RESPONSE_CODE_OK,
						messagePrefix + "server acknowledged PUT request" );

					// If the server has heard from all of us clients, we can conclude the test
					if ( response.putRequests >= 2 ) {
						console.log( JSON.stringify( { killPeer: true } ) );
					}
					callback();
				} );
	}
] );

process.on( "exit", function() {

	// The discovery callback should have been called at least twice - once with the resource,
	// and once with null
	testUtils.assert( "ok", discoveryCallbackCount >= 2,
		messagePrefix + "discovery callback was called at least twice" );
} );
