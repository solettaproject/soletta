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

var soletta = require( 'bindings' )( 'soletta' ),
    _ = require( 'lodash' );

exports.open = function( init ) {
    return new Promise( function( fulfill, reject ) {
        var config = null;
        var spiMode =  init.mode ? init.mode : "mode0";
        var chipSelect =  init.chipSelect ? init.chipSelect : 0;
        var bitsPerWord =  init.bitsPerWord ? init.bitsPerWord : 8;

        config = {
            chip_select: chipSelect,
            mode: soletta.sol_spi_mode_from_str( spiMode ),
            frequency: init.frequency,
            bits_per_word: bitsPerWord,
        }
        fulfill( SPIBus( soletta.sol_spi_open( init.bus, config ) ) );
    });
}

var SPIBus = function( bus ) {
    if ( !this._isSPIBus )
        return new SPIBus( bus );
    this._bus = bus;
}

_.extend( SPIBus.prototype, {
    _isSPIBus: true,

    transfer: function(value) {
       return new Promise( _.bind( function( fulfill, reject ) {
           var txBuffer;
           if ( Buffer.isBuffer( value ) )
               txBuffer = value;
           else
               txBuffer = new Buffer(value);

           var returnStatus = soletta.sol_spi_transfer( this._bus, txBuffer,
               function( txData, rxData, count ) {
                   if ( rxData !== null ) {
                       var rxBffer = new Buffer( rxData );
                       fulfill( rxBffer );
                   }
           });

           if ( ( typeof returnStatus === 'undefined' ) || !returnStatus ) {
               reject( new Error( "SPI transmission failed" ) );
           }
       }, this ) );
    },

    close: function() {
        return new Promise( _.bind( function( fulfill, reject ) {
            fulfill( soletta.sol_spi_close( this._bus ) );
        }, this ) );
    },
});

exports.SPIBus = SPIBus;
