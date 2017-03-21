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
 * This sample code returns the value read from the GPIO
 * input pin 48 on Edison.
 */
var gpio = require( "soletta/gpio" ),
    gpioPin = null;

gpio.open( {
    pin: 48, // Setup pin 48 for reading
    direction: "in", // Set the gpio direction to input
    edge: "rising"
} ).then( function( pin ) {
    gpioPin = pin;

    // Read the value from GPIO pin
    gpioPin.read().then( function( value ) {
        console.log( "GPIO value is ", value );
    } ).catch( function( error ) {
        console.log( "Failed to read the value: ", error );
        process.exit();
    } );
} ).catch( function( error ) {
    console.log( "Could not open GPIO: ", error );
    process.exit();
} );

process.on( "exit", function() {
    if ( gpioPin ) {
        gpioPin.close();
    }
} );
