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
 * Sample code to demonstrate the usage of PWM JS bindings for
 * configuring and using PWM pins.
 *
 * This example changes the brightness of an LED in a loop.
 *
 * Pin configuration:
 * LED (e.g. Grove LED) -> PWM pin
 *
 * Press Ctrl+C to exit the process.
 */
var pwm = require( "soletta/pwm" ),
    pwmPin = null,
    dutyCycle = 0,
    delta = 0.1,
    changeInterval;

// Change brightness of an LED
function changeLEDBrightness() {
    if ( dutyCycle <= 0 ) {
        // Increase the brightness
        delta = 0.1;
    } else if ( dutyCycle >= 1 ) {
        // Decrease the brightness
        delta = -0.1;
    }

    dutyCycle = parseFloat( ( dutyCycle + delta ).toFixed( 1 ) );
    pwmPin.setDutyCycle( dutyCycle * 10000 ).then( function() {
        console.log( "LED brightness changed" );
    } ).catch( function( error ) {
        console.log( "PWM error: ", error );
        process.exit();
    } );
}

// Configure PWM pin
pwm.open( {
    device: 0,
    channel: 0,
    period: 10000,
    dutyCycle: 0,
    enabled: true
} ).then( function( pin ) {
    pwmPin = pin;
    changeInterval = setInterval( changeLEDBrightness, 300 );
} ).catch( function( error ) {
    console.log( "PWM error: ", error );
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

    // Close the PWM pin
    if ( pwmPin ) {
        pwmPin.close();
    }
} );
