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

var soletta = require( 'bindings' )( 'soletta' ),
    _ = require( 'lodash' );

exports.open = function( init ) {
    return new Promise( function( fulfill, reject ) {
        var config = null;
        var connection;
        var callback_data = [];
        var flowControl = ( typeof init.flowControl === 'undefined' ) ? false : init.flowControl;
        var baudRate = init.baud ? init.baud : "baud-115200";
        var dataBits = init.dataBits ? init.dataBits : "databits-8";
        var stopBits = init.stopBits ? init.stopBits : "stopbits-1";
        var parity = init.parity ? init.parity : "none";

        config = {
            baud_rate: soletta.sol_uart_baud_rate_from_str( baudRate ),
            data_bits: soletta.sol_uart_data_bits_from_str( dataBits ),
            parity: soletta.sol_uart_parity_from_str( parity ),
            stop_bits: soletta.sol_uart_stop_bits_from_str( stopBits ),
            flow_control: flowControl,
            on_data: function( data ) {
                callback_data[0].dispatchEvent( "read", {
                    type: "read",
                    data: data
                } );
            },
            on_feed_done: function( cb_status ) {
                callback_data[0].dispatchEvent( "write", {
                    type: "write"
                } );
            },
        }

        var uart = soletta.sol_uart_open( init.port, config );
        if ( !uart ) {
            reject( new Error( "Could not open UART device" ) );
            return;
        }
        connection = UARTConnection( uart );
        callback_data.push( connection );
        fulfill( connection );
    });
}

var UARTConnection = function( connection ) {
    if ( !this._isUARTConnection )
        return new UARTConnection( connection );
    this._connection = connection;
}

require( "util" ).inherits( UARTConnection, require( "events" ).EventEmitter );

_.extend( UARTConnection.prototype, {
    _isUARTConnection: true,
    onchange: null,

    write: function( value ) {
        return new Promise( _.bind( function( fulfill, reject ) {
            var buffer;
            if ( Buffer.isBuffer( value ) )
                buffer = value;
            else
                buffer = new Buffer( value );

            var returnStatus = soletta.sol_uart_feed( this._connection, buffer,
                function( cb_status ) {
                if ( cb_status < 0 ) {
                    reject( new Error( "UART write failed" ) );
                } else {
                    fulfill();
                }
            });

            if ( ( typeof returnStatus === 'undefined' ) || returnStatus < 0) {
                reject( new Error( "UART write failed" ) );
            }
        }, this ) );
    },

    close: function() {
        soletta.sol_uart_close( this._connection);
    },

    addEventListener: UARTConnection.prototype.addListener,

    removeEventListener: UARTConnection.prototype.removeListener,

    dispatchEvent: function( event, request ) {
        this.emit( event, request );
        if ( typeof this[ "on" + event ] === "function" ) {
            this[ "on" + event ]( request );
        }
    }
});

exports.UARTConnection = UARTConnection;
