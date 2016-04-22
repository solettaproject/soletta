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
        var direction = init.direction ? init.direction : "out";
        var edge = init.edge ? init.edge : "any";
        var pull = init.pull ? init.pull : "none";
        var active_low = init.activeLow ? init.activeLow : false;
        var poll = init.poll ? init.poll : 1000;

        if ( direction == "in" ) {
            config = {
                dir: soletta.sol_gpio_direction_from_str( direction ),
                active_low: active_low,
                poll_timeout: poll,
                drive_mode: soletta.sol_gpio_drive_from_str( pull ),
                trigger_mode: soletta.sol_gpio_edge_from_str( edge ),
                callback: function( value ) {
                    callback_data[0].dispatchEvent( "change", {
                        type: "change",
                        value: value
                    } );
                },
            }

        } else {
            config = {
                dir: soletta.sol_gpio_direction_from_str( direction ),
                active_low: active_low,
                drive_mode: soletta.sol_gpio_drive_from_str( pull ),
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
