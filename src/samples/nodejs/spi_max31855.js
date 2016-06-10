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
 * This sample code demonstrates the usage of SPI JS bindings for
 * configuring and reading the temperature in Celsius from the
 * thermocouple amplifier MAX31855 connected to the MinnowBoard MAX.
 *
 * Pin configuration:
 * The thermocouple amplifier talks over SPI, so connect it to the SPI pins.
 * Vin -> MinnowBoard MAX pin 4 (3.3V)
 * GND -> MinnowBoard MAX pin 2 (GND)
 * DO  -> MinnowBoard MAX pin 7 (MISO)
 * CS  -> MinnowBoard MAX pin 5 (CSO)
 * CLK -> MinnowBoard MAX pin 11 (SCLK)
 *
 * Press Ctrl+C to exit the process.
 */
var spi = require( "soletta/spi" ),
    spiBus = null,
    readInterval;

spi.open( {
    bus: 0,
    frequency: 2000000
} ).then( function( bus ) {
    spiBus = bus;

    // Take a temperature reading in every one second
    readInterval = setInterval( function() {
        var buf = new Buffer( 4 );
        buf.fill( 0 );
        spiBus.transfer( buf ).then( function( rx ) {
            var rawValue = ( rx[ 0 ] << 24 ) | ( rx[ 1 ] << 16 ) | ( rx[ 2 ] << 8 ) | rx[ 3 ];
            if ( rawValue & 0x7 ) {
                console.log( "Incorrect value received" );
                return;
            }

            var c = rawValue >> 18;
            c *= 0.25; // LSB = 0.25 deg C
            console.log( "Value in Celsius:", c );
        } ).catch( function( error ) {
            console.log( "SPI transfer error: ", error );
            process.exit();
        } );
    }, 1000 );
} ).catch( function( error ) {
    console.log( "SPI error: ", error );
    process.exit();
} );

// Press Ctrl+C to exit the process
process.on( "SIGINT", function() {
    process.exit( 0 );
} );

process.on( "exit", function() {
    if ( readInterval ) {
        clearInterval( readInterval );
    }

    if ( spiBus ) {
        spiBus.close();
    }
} );
