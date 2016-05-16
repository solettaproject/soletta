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

module.exports = function( setObservable ) {

var _ = require( "lodash" );
var soletta = require( require( "path" )
	.join( require( "bindings" ).getRoot( __filename ), "lowlevel" ) );
var testUtils = require( "../assert-to-console" );
var payload = require( "./payload" );
var uuid = process.argv[ 2 ];
var observationCount = 0;
var theResource;
var theInterval;

console.log( JSON.stringify( { assertionCount: 0 } ) );

theResource = soletta.sol_oic_server_register_resource( _.extend( {
		interface: "oic.if.baseline",
		resource_type: "core.light",
		path: "/a/" + uuid,
		put: function putHandler( input, output ) {
			if ( input.finished === uuid ) {
				observationCount++;
				output.clientsFinished = observationCount;
			}
			if ( observationCount >= 2 && !setObservable ) {
				clearInterval(theInterval);
			}
			return soletta.sol_coap_response_code.SOL_COAP_RESPONSE_CODE_OK;
		},
	}, setObservable ? {} : {
		get: function getHandler( input, output ) {
			_.extend( output, payload.generate() );
			return soletta.sol_coap_response_code.SOL_COAP_RESPONSE_CODE_OK;
		}
	} ),
		soletta.sol_oic_resource_flag.SOL_OIC_FLAG_DISCOVERABLE |
		( setObservable ? soletta.sol_oic_resource_flag.SOL_OIC_FLAG_OBSERVABLE : 0 ) |
		soletta.sol_oic_resource_flag.SOL_OIC_FLAG_ACTIVE );

console.log( JSON.stringify( { ready: true } ) );

if ( !setObservable ) {
	theInterval = setInterval( function() {
		soletta.sol_oic_server_notify( theResource, payload.generate() );
	}, 200 );
}

process.on( "SIGINT", function() {
	soletta.sol_oic_server_unregister_resource( theResource );
} );

};
