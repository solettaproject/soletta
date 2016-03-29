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
        var pin = init.pin;
        var dir = 0;
        var drive_mode = 0;
        var config = null;
        var gpiopin;
        var callback_data = [];

        if ( init.pullup )
            drive_mode = soletta.sol_gpio_drive.SOL_GPIO_DRIVE_PULL_UP;
        else if ( init.pullup === false )
            drive_mode = soletta.sol_gpio_drive.SOL_GPIO_DRIVE_PULL_DOWN;
        else
            drive_mode = soletta.sol_gpio_drive.SOL_GPIO_DRIVE_NONE;

        if ( init.direction == "in" ) {
            config = {
                dir: soletta.sol_gpio_direction.SOL_GPIO_DIR_IN,
                active_low: init.activeLow,
                poll_timeout: init.poll,
                drive_mode: drive_mode,
                trigger_mode: init.edge,
                callback: function( pin, value ) {
                    callback_data[0].dispatchEvent( "change", {
                        type: "change",
                        value: value
                    } );
                },
            }

        } else {
            config = {
                dir: soletta.sol_gpio_direction.SOL_GPIO_DIR_OUT,
                active_low: init.activeLow,
                drive_mode: drive_mode,
            }
        }

        gpiopin = GPIOPin( soletta.sol_gpio_open( pin, config ) );
        callback_data.push( gpiopin );
        fulfill( gpiopin );
    });

}

var GPIOPin = function( pin ) {
    if ( !this._isGPIOPin )
        return new GPIOPin( pin );
    this._pin = pin;
}

require( "util" ).inherits( GPIOPin, require( "events" ).EventEmitter );

_.extend( GPIOPin.prototype, {
    _isGPIOPin: true,
    onchange: null,

    read: function() {
        return new Promise( _.bind( function( fulfill, reject ) {

            fulfill( soletta.sol_gpio_read( this._pin ) );
        }, this ) );
    },

    write: function( value ) {
        return new Promise( _.bind( function( fulfill, reject ) {
            fulfill( soletta.sol_gpio_write( this._pin, value ) );
        }, this ) );
    },

    close: function() {
        return new Promise( _.bind( function( fulfill, reject ) {
            fulfill( soletta.sol_gpio_close( this._pin) );
        }, this ) );
    },

    addEventListener: GPIOPin.prototype.addListener,

    removeEventListener: GPIOPin.prototype.removeListener,

    dispatchEvent: function( event, request ) {
        this.emit( event, request );
        if ( typeof this[ "on" + event ] === "function" ) {
            this[ "on" + event ]( request );
        }
    },

});

exports.GPIOPin = GPIOPin;
