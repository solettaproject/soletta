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
 * This sample code blinks an LED connected to Edison GPIO pin label 7 in every
 * one second.
 *
 * Pin configuration:
 * Digital output device (e.g. Grove LED) -> pin 7 on Edison
 * Arduino Breakout board (D7 on the Grove base shield).
 *
 * Press Ctrl+C to exit the process.
 */
var gpio = require( "soletta/gpio" ),
    ledPin = null,
    state = false,
    changeInterval;

// Change the LED state
function changeState() {
    state = !state;
    ledPin.write( state ).then( function() {
        console.log( "LED state changed" );
    } ).catch( function( error ) {
        console.log( "Failed to write on GPIO device: ", error );
        process.exit();
    } );
}

// Configure LED pin.
gpio.open( {
    name: "7"
} ).then( function( pin ) {
    ledPin = pin; // Save the handle

    // Change the LED state in every one second.
    changeInterval = setInterval( changeState, 1000 );
} ).catch( function( error ) {
    console.log( "Could not open GPIO pin by label: ", error );
    process.exit();
} );

// Press Ctrl+C to exit the process
process.on( "SIGINT", function() {
    process.exit();
} );

process.on( "exit", function() {
    if ( changeInterval ) {
        clearInterval( changeInterval );
    }

    if ( ledPin ) {
        ledPin.close();
    }
} );
