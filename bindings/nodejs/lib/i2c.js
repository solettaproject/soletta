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
        var bus = init.bus;
        var spd = soletta.sol_i2c_speed_from_str(init.speed);
        var i2cbus = null;

        if (init.raw) {
            i2cbus = I2CBus( soletta.sol_i2c_open_raw( bus, spd ) );
        } else {
            i2cbus = I2CBus( soletta.sol_i2c_open( bus, spd ) );
        }

        //copy the properties
        _.extend(i2cbus, init);

        fulfill( i2cbus );
    });

}

var I2CBus = function( i2cbus ) {
    if ( !this._isI2CBus )
        return new I2CBus ( i2cbus );
    this._i2cbus = i2cbus;
}

_.extend( I2CBus.prototype, {
    _isI2CBus: true,
    _pending: null,
    busy: {
        get: function() {
            return soletta.sol_i2c_busy( this._i2cbus );
        }
    },

    read: function( device, size, register, repetitions ) {
        return new Promise( _.bind( function( fulfill, reject ) {
            if (!repetitions)
                repetitions = 1;

            soletta.sol_i2c_set_slave_address( this._i2cbus, device );
            if (register == null) {
                this._pending = soletta.sol_i2c_read( this._i2cbus,
                    register, size, function( data, status ) {
                    this._pending = null;
                    fulfill( data );
                });
            } else if (repetitions > 1) {
                this._pending = soletta.sol_i2c_read_register_multiple(
                    this._i2cbus, register, size, repetitions,
                    function( register, data, status ) {
                    this._pending = null;
                    fulfill( data );
                });
            } else {
                this._pending = soletta.sol_i2c_read_register(
                    this._i2cbus, register, size,
                    function( register, data, status ) {
                    this._pending = null;
                    fulfill( data );
                });
            }
            if (!this._pending)
                reject( new Error( "I2C read failed" ) );
        }, this ) );
    },

    write: function( device, data, register ) {
        return new Promise( _.bind( function( fulfill, reject ) {
            soletta.sol_i2c_set_slave_address( this._i2cbus, device );
            var buf;

            if (Buffer.isBuffer( data ))
                buf = data;
            else
                buf = new Buffer(data);

            if (!register) {
                this._pending = soletta.sol_i2c_write( this._i2cbus,
                    buf, function( data, status ) {
                    this._pending = null;
                    fulfill();
                } );
            } else {
                this._pending = soletta.sol_i2c_write_register(
                    this._i2cbus, register, buf,
                    function( register, data, status ) {
                    this._pending = null;
                    fulfill();
                } );
            }
            if (!this._pending)
                reject( new Error( "I2C write failed" ) );
        }, this ) );
    },

    writeBit: function( device, data ) {
        return new Promise( _.bind( function( fulfill, reject ) {
            soletta.sol_i2c_set_slave_address( this._i2cbus, device );
            this._pending = soletta.sol_i2c_write_quick(
                this._i2cbus, data, function( status ) {
                this._pending = null;
                fulfill();
            } );
            if (!this._pending)
                reject( new Error( "I2C writeBit failed" ) );
        }, this ) );
    },

    close: function() {
        if (this.raw)
            soletta.sol_i2c_close_raw( this._i2cbus);
        else
            soletta.sol_i2c_close( this._i2cbus);
    },

    abort: function() {
        soletta.sol_i2c_pending_cancel( this._i2cbus, this._pending );
        this._pending = null;
    }
});

exports.I2CBus = I2CBus;
