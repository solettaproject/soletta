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

/*
 * This sample code demonstrates the usage of AIO JS bindings for
 * configuring and reading the value of analog device connected to
 * the Edison.
 *
 * Pin configuration:
 * Analog input device (e.g. Grove Light Sensor) -> A0 on Edison
 * Arduino Breakout board.
 *
 * Press Ctrl+C to exit the process.
 */
var aio = require( "soletta/aio" ),
    aioPin = null,
    readInterval;

// Configure analog pin #0 (A0)
aio.open( {
    device: 1,
    pin: 0
} ).then( function( pin ) {
    aioPin = pin;

    readInterval = setInterval( function() {

        // Read the value of analog input in every one second
        pin.read().then( function( data ) {
            console.log( "Value of the analog input device: ", data );
        } ).catch( function( error ) {
            console.log( "Failed to read the value ", error );
            process.exit();
        } );
    }, 1000 );
} ).catch( function( error ) {
    console.log( "Could not open AIO: ", error );
    process.exit();
} );

// Press Ctrl+C to exit the process
process.on( "SIGINT", function() {
    process.exit();
} );

process.on( "exit", function() {
    if ( readInterval ) {
        clearInterval( readInterval );
    }

    if ( aioPin ) {
        aioPin.close();
    }
} );
