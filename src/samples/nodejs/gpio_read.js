/*
 * This file is part of the Soletta™ Project
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
 * This sample code demonstrates the usage of GPIO JS bindings for
 * configuring and reading the value of the digital input connected
 * to the Edison.
 *
 * Pin configuration:
 * Digital input device (e.g. Grove Button) -> 49 (D8 on the base shield)
 *
 * Press Ctrl+C to exit the process.
 */
var gpio = require( "soletta/gpio" ),
    gpioPin = null,
    readInterval;

gpio.open( {
    pin: 49, // Setup pin 49 for reading
    direction: "in" // Set the gpio direction to input
} ).then( function( pin ) {
    gpioPin = pin;

    readInterval = setInterval( function() {

        // Read the value of the digital input in every one second.
        gpioPin.read().then( function( value ) {
            console.log( "GPIO value is ", value );
        } ).catch( function( error ) {
            console.log( "Failed to read the value: ", error );
            process.exit();
        } );
    }, 1000 );
} ).catch( function( error ) {
    console.log( "Could not open GPIO: ", error );
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

    if ( gpioPin ) {
        gpioPin.close();
    }
} );
