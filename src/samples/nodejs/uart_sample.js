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
 * This sample code demonstrates the usage of UART JS bindings for
 * reading and writing data to a UART accessible device.
 *
 * Setup:
 * Use two UART cables and connect RX/TX pins in the cable #1 to
 * TX/RX pins in the cable #2.
 */

var uart = require( "soletta/uart" ),
    args = process.argv.slice( 2 ),
    uartproducer = null,
    uartConsumer = null;

var options = {
    help: false,
    producer,
    consumer
};

const usage = "Usage: node uart_sample.js -p <producerUART> -c <consumerUART>\n" +
    "options: \n" +
    "  -h, --help \n" +
    "  -p, --producer <producerUART>\n" +
    "  -c, --consumer <consumerUART>\n";

for ( var i = 0; i < args.length; i++ ) {
    var arg = args[ i ];

    switch ( arg ) {
        case "-h":
        case "--help":
            options.help = true;
            break;
        case "-p":
        case "--producer":
            var producer = args[ i + 1 ];
            if ( typeof producer == "undefined" ) {
                console.log( usage );
                process.exit( 0 );
            }
            options.producer = producer;
            break;
        case "-c":
        case "--consumer":
            var consumer = args[ i + 1 ];
            if ( typeof consumer == "undefined" ) {
                console.log( usage );
                process.exit( 0 );
            }
            options.consumer = consumer;
            break;
        default:
            break;
    }
}

if ( options.help == true ) {
    console.log( usage );
    process.exit( 0 );
}

if ( !options.producer || !options.consumer ) {
    console.log( usage );
    process.exit( 0 );
}

// Setup UART for writing
uart.open( {
    port: options.producer
} ).then( function( connection ) {
    uartproducer = connection;

    // Send the data
    connection.write( "UART test " ).then( function() {
        connection.write( [ 0x62, 0x75, 0x66, 0x66, 0x65, 0x72 ] ).then( function() {
            console.log( "UART Producer: Data sent successfully!" );
        } );
    } ).catch( function( error ) {
        console.log( "UART error: ", error );
        process.exit();
    } );
} ).catch( function( error ) {
    console.log( "UART error: ", error );
    process.exit();
} );

// Setup UART for reading
uart.open( {
    port: options.consumer
} ).then( function( connection ) {
    uartConsumer = connection;

    // Setup 'onread' event handler to receive the data.
    connection.onread = function( event ) {
        console.log( "UART Consumer: Received data:", event.data.toString( "utf-8" ) );
    };
} );

// Press Ctrl+C to exit the process
process.on( "SIGINT", function() {
    process.exit();
} );

process.on( "exit", function() {
    if ( uartproducer ) {
        uartproducer.close();
    }
    if ( uartConsumer ) {
        uartConsumer.close();
    }
} );
