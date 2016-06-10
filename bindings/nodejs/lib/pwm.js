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
        var raw = ( typeof init.raw === 'undefined' ) ? false : init.raw;
        var enabled = ( typeof init.enabled === 'undefined' ) ? false : init.enabled;
        var alignment = init.alignment ? init.alignment : "left";
        var polarity = init.polarity ? init.polarity : "normal";
        var pwm;

        config = {
            period_ns: init.period,
            duty_cycle_ns: init.dutyCycle,
            alignment: soletta.sol_pwm_alignment_from_str( alignment ),
            polarity: soletta.sol_pwm_polarity_from_str( polarity ),
            enabled: enabled,
        }

        if ( typeof init.name === 'string' && init.name !== "" ) {
            pwm = soletta.sol_pwm_open_by_label( init.name, config );
        } else {
            if ( raw )
                pwm = soletta.sol_pwm_open_raw( init.device, init.channel, config );
            else
                pwm = soletta.sol_pwm_open( init.device, init.channel, config );
        }

        if ( pwm ) {
            fulfill( PWMPin( pwm ) );
        } else {
            reject( new Error( "Could not open PWM device" ) );
        }

    });
}

var PWMPin = function( pin ) {
    if ( !this._isPWMPin )
        return new PWMPin( pin );
    this._pin = pin;
}

_.extend(PWMPin.prototype, {
    _isPWMPin: true,

    setEnabled: function( value ) {
        return new Promise( _.bind( function( fulfill, reject ) {
            var returnValue = soletta.sol_pwm_set_enabled( this._pin, value);
            if ( returnValue )
                fulfill();
            else
                reject( new Error( "Failed to enable PWM pin" ) );
        }, this ) );
    },

    setDutyCycle: function( value ) {
        return new Promise( _.bind( function( fulfill, reject ) {
            var returnValue = soletta.sol_pwm_set_duty_cycle( this._pin, value );
            if ( returnValue )
                fulfill();
            else
                reject( new Error( "Failed to set PWM duty cycle" ) );
        }, this ) );
    },

    setPeriod: function( value ) {
        return new Promise( _.bind( function( fulfill, reject ) {
            var returnValue = soletta.sol_pwm_set_period( this._pin, value );
            if ( returnValue )
                fulfill();
            else
                reject( new Error( "Failed to set PWM period" ) );
        }, this ) );
    },

    close: function() {
       soletta.sol_pwm_close( this._pin);
    }
});

exports.PWMPin = PWMPin;
