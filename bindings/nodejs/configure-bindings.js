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

var fs = require( "fs" ),
	path = require( "path" );

// List containing the names of source files for the bindings we wish to include.
// Paths are relative to the location of nodejs-bindings-sources.gyp (generated below).
var sources = [
	"main.cc",
	"../src/data.cc",
	"../src/functions/sol-platform-monitors.cc",
	"../src/functions/simple.cc",
	"../src/hijack.cc",
	"../src/sol-uv-integration.c",
	"../src/structures/js-handle.cc"
];

// List containing the names of the header files in which to search for constants and enums
var headers = [
	"sol-platform.h"
];

var oneVariable, match;
for ( oneVariable in process.env ) {

	// If it's an environment variable starting with SOL_CONFIG_ then examine its value
	match = oneVariable.match( /^SOL_CONFIG_(.*)$/ ) ? process.env[ oneVariable ] : null;

	// If the value is "y" then add files based on the name of the variable, removing the prefix
	match = match && ( match === "y" ) ? oneVariable.replace( /^SOL_CONFIG_/, "" ) : null;

	switch( match ) {
		case "OIC":
			sources = sources.concat( [
				"../src/functions/oic-client-common.cc",
				"../src/functions/oic-client-discovery.cc",
				"../src/functions/oic-client-resource-ops.cc",
				"../src/functions/oic-server.cc",
				"../src/structures/device-id.cc",
				"../src/structures/oic-client.cc",
				"../src/structures/oic-map.cc"
			] );
			headers = headers.concat( [
				"sol-oic-client.h",
				"sol-oic-common.h"
			] );
			break;
		case "NETWORK":
			sources = sources.concat( [
				"../src/functions/sol-network.cc",
				"../src/structures/network.cc"
			] );
			headers = headers.concat( [
				"sol-coap.h",
				"sol-network.h"
			] );
			break;
		case "USE_GPIO":
			sources = sources.concat( [
				"../src/functions/gpio.cc",
				"../src/structures/sol-js-gpio.cc"
			] );
			headers = headers.concat( [
				"sol-gpio.h"
			] );
			break;
		case "USE_AIO":
			sources = sources.concat( [
				"../src/functions/aio.cc",
			] );
			headers = headers.concat( [
				"sol-aio.h"
			break;
		case "USE_UART":
			sources = sources.concat( [
				"../src/functions/uart.cc",
				"../src/structures/sol-js-uart.cc"
			] );
			headers = headers.concat( [
				"sol-uart.h"
			] );
			break;
		case "USE_PWM":
			sources = sources.concat( [
				"../src/functions/pwm.cc",
				"../src/structures/sol-js-pwm.cc"
			] );
			headers = headers.concat( [
				"sol-pwm.h"
			] );
			break;
		default:
			break;
	}
}

fs.writeFileSync( path.join( __dirname, "generated", "header-files-list" ), headers.join( "\n" ) );

fs.writeFileSync( path.join( __dirname, "generated", "nodejs-bindings-sources.gyp" ),
	JSON.stringify( { sources: sources } ) );
