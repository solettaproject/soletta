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
var testUtils = require( "../../assert-to-console" );
var putRequests = 0;
var theResource;

console.log( JSON.stringify( { assertionCount: 2 } ) );

theResource = soletta.sol_oic_server_add_resource( {
		interface: "oic.if.baseline",
		resource_type: "core.light",
		path: "/a/" + process.argv[ 2 ],
		put: function putHandler( input, output ) {
			testUtils.assert( "deepEqual", input, { uuid: process.argv[ 2 ] },
				"Server: PUT request payload is as expected" );
			output.putRequests = ++putRequests;
			return soletta.sol_coap_responsecode_t.SOL_COAP_RSPCODE_OK;
		}
	}, soletta.sol_oic_resource_flag.SOL_OIC_FLAG_DISCOVERABLE |
		soletta.sol_oic_resource_flag.SOL_OIC_FLAG_ACTIVE );

console.log( JSON.stringify( { ready: true } ) );

process.on( "exit", function() {
	soletta.sol_oic_server_del_resource( theResource );
} );
