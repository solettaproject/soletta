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
var theResource;
var anError = {};

console.log( JSON.stringify( { assertionCount: 3 } ) );

theResource = soletta.sol_oic_server_add_resource( {
		path: "/a/" + process.argv[ 2 ],
		resource_type: "core.light",
		interface: "oic.if.baseline"
	},
	soletta.sol_oic_resource_flag.SOL_OIC_FLAG_ACTIVE |
	soletta.sol_oic_resource_flag.SOL_OIC_FLAG_DISCOVERABLE );

testUtils.assert( "ok", !!theResource, "sol_oic_server_add_resource() is successful" );

soletta.sol_oic_server_del_resource( theResource );
try {
	soletta.sol_oic_server_del_resource( theResource );
} catch( theError ) {
	anError = theError;
}

testUtils.assert( "strictEqual", anError instanceof TypeError, true,
	"Attempting to delete a resource twice causes a TypeError" );
testUtils.assert( "strictEqual", anError.message,
	"Object is not of type SolOicServerResource",
	"TypeError message is as expected" );
