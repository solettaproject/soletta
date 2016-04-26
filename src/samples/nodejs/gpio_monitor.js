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
 * Sample code to turn on/off an LED connected to Edison by
 * using button.
 *
 * Pin configuration:
 * LED pin -> 48 (D7 on the Grove base shield)
 * Button pin -> 49 (D8 on the Grove base shield)
 */
var gpio = require( "soletta/gpio" ),
    ledState = false,
    ledPin = null,
    buttonPin = null;

// Configure LED pin.
function setupLEDPin() {
    gpio.open( {
        pin: 48 // LED pin 48
    } ).then( function( pin ) {
        ledPin = pin;

        // Setup 'onchange' event handler. The handler will
        // be called whenever the button pin value changes.
        buttonPin.onchange = function( event ) {
            ledState = !ledState;
            ledPin.write( ledState ).then( function() {
                console.log( "LED state changed" );
            } ).catch( function( error ) {
                console.log( "Failed to write on GPIO device: ", error );
                process.exit();
            } );
        };
    } ).catch( function( error ) {
        console.log( "Could not open LED pin for writing." );
        process.exit();
    } );
}

// Configure button pin.
gpio.open( {
    pin: 49, // Button pin 49
    direction: "in", // Set the gpio direction to input
    edge: "rising"
} ).then( function( pin ) {
    buttonPin = pin;

    // Configure LED pin
    setupLEDPin();
} ).catch( function( error ) {
    console.log( "Could not open button pin for reading." );
} );

process.on( "exit", function() {
    if ( ledPin ) {
        ledPin.close();
    }

    if ( buttonPin ) {
        buttonPin.close();
    }
} );
