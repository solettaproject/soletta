/*
 * This file is part of the Soletta Project
 *
 * Copyright (c) 2015, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#pragma once

#include <stdarg.h>

#include "sol-common-buildopts.h"
#include "sol-gpio.h"
#include "sol-pin-mux.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These structure is used for implementation of Pin Multiplexer modules under Soletta.
 */

/**
 * @defgroup PinMuxModules Pin Multiplexer Modules
 * @ingroup PinMux
 *
 * @brief These structure is used for implementation of Pin Multiplexing modules under Soletta.
 *
 * @{
 */

/**
 * Structure containing the callbacks used to setup and multiplex the pins of a given board
 */
struct sol_pin_mux {
#ifndef SOL_NO_API_VERSION
#define SOL_PIN_MUX_API_VERSION (2UL)
    unsigned long int api_version; /**< API version */
#endif
    const char *plat_name; /**< Name of this multiplexer target platform */

    /**
     * Called after the module is successfully load by Soletta to allow it
     * to do any initialization it may require.
     *
     * @return '0' on success, error code (always negative) otherwise.
     */
    int (*init)(void);

    /**
     * Called before the module is unloaded.
     * Is an opportunity for the module to execute any clean-up tasks it may require.
     */
    void (*shutdown)(void);

    /**
     * Callback to map a pin label to the parameters necessary so it works on the desired protocol.
     *
     * Find if a given pin labeled 'label' is capable of operate on protocol 'prot' and return
     * the parameters needed to setup the protocol.
     *
     * @param label The label of the pin as see on the board
     * @param prot Protocol on which the pin should operate
     * @param args Where to write the output. Soletta will provide the required args based on
     * the requested protocol, in the same order that they appear in the protocol API.
     *
     * @return '0' on success, error code (always negative) otherwise.
     */
    int (*pin_map)(const char *label, const enum sol_io_protocol prot, va_list args);

    /**
     * Callback to setup the given pin to operate as Analog I/O.
     *
     * Soletta will call this function so the module can execute the instructions
     * needed to configure 'device'/'pin' pair to operate as Analog I/O.
     *
     * @param device the aio device number.
     * @param pin the aio pin number.
     *
     * @return '0' on success, error code (always negative) otherwise.
     */
    int (*aio)(const int device, const int pin);

    /**
     * Callback to setup the given pin to operate as GPIO in the given direction (in or out).
     *
     * Soletta will call this function so the module can execute the instructions
     * needed to configure 'pin' to operate as GPIO in direction 'dir'.
     *
     * @param pin the gpio pin number.
     * @param dir direction (in or out) that the pin should operate.
     *
     * @return '0' on success, error code (always negative) otherwise.
     */
    int (*gpio)(const uint32_t pin, const enum sol_gpio_direction dir);

    /**
     * Callback to setup the pins used of the given i2c bus number to operate in I2C mode.
     *
     * Soletta will call this function so the module can execute the instructions
     * needed to configure the pins used by the given i2c bus to operate in I2C mode.
     *
     * @param bus the i2c bus number.
     *
     * @return '0' on success, error code (always negative) otherwise.
     */
    int (*i2c)(const uint8_t bus);

    /**
     * Callback to setup the given pin to operate as PWM.
     *
     * Soletta will call this function so the module can execute the instructions
     * needed to configure 'device'/'channel' pair to operate as PWM.
     *
     * @param device the pwm device number.
     * @param channel the channel number on device.
     *
     * @return '0' on success, error code (always negative) otherwise.
     */
    int (*pwm)(const int device, const int channel);

};

/**
 * @def SOL_PIN_MUX_DECLARE(_NAME, decl ...)
 * Helper macro to make easier to correctly declare the symbol
 * needed by the Pin Mux module.
 */
#ifdef SOL_PIN_MUX_MODULE_EXTERNAL
#define SOL_PIN_MUX_DECLARE(_NAME, decl ...) \
    SOL_API const struct sol_pin_mux SOL_PIN_MUX = { \
        SOL_SET_API_VERSION(SOL_PIN_MUX_API_VERSION, ) \
        decl \
    }
#else
#define SOL_PIN_MUX_DECLARE(_NAME, decl ...) \
    SOL_API const struct sol_pin_mux SOL_PIN_MUX_ ## _NAME = { \
        SOL_SET_API_VERSION(SOL_PIN_MUX_API_VERSION, ) \
        decl \
    }
#endif

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
