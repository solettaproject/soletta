/*
 * This file is part of the Soletta (TM) Project
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
 * @brief Defines the API that needs to be implemented by Pin Multiplexer modules under Soletta.
 */

/**
 * @defgroup PinMuxModules Pin Multiplexer Modules
 * @ingroup PinMux
 *
 * @brief Defines the API that needs to be implemented by Pin Multiplexer modules under Soletta.
 *
 * @{
 */

/**
 * @brief Structure defining the API of a Pin Multiplexer module
 */
typedef struct sol_pin_mux {
#ifndef SOL_NO_API_VERSION
#define SOL_PIN_MUX_API_VERSION (2)
    uint16_t api_version; /**< @brief API version */
#endif
    const char *plat_name; /**< @brief Name of this multiplexer target platform */

    /**
     * @brief Called after the module is successfully load by Soletta to allow it
     * to do any initialization it may require.
     *
     * @return @c 0 on success, error code (always negative) otherwise.
     */
    int (*init)(void);

    /**
     * @brief Called before the module is unloaded.
     *
     * Is an opportunity for the module to execute any clean-up tasks it may require.
     */
    void (*shutdown)(void);

    /**
     * @brief Callback to map a pin label to the parameters necessary so it works on the desired protocol.
     *
     * Find if a given pin labeled @c label is capable of operate on protocol @c prot and return
     * the parameters needed to setup the protocol.
     *
     * @param label The label of the pin as see on the board
     * @param prot Protocol on which the pin should operate
     * @param args Where to write the output. Soletta will provide the required @c args based on
     * the requested protocol, in the same order that they appear in the protocol API.
     *
     * @return @c 0 on success, error code (always negative) otherwise.
     */
    int (*pin_map)(const char *label, const enum sol_io_protocol prot, va_list args);

    /**
     * @brief Callback to setup the given pin to operate as Analog I/O.
     *
     * Soletta will call this function so the module can execute the instructions
     * needed to configure 'device'/'pin' pair to operate as Analog I/O.
     *
     * @param device the aio device number.
     * @param pin the aio pin number.
     *
     * @return @c 0 on success, error code (always negative) otherwise.
     */
    int (*aio)(int device, int pin);

    /**
     * @brief Callback to setup the given pin to operate in the given GPIO configuration.
     *
     * Soletta will call this function so the module can execute the instructions
     * needed to configure @c pin to operate as configured by @c config.
     *
     * @param pin the gpio pin number.
     * @param config Desired configuration for the pin.
     *
     * @return @c 0 on success, error code (always negative) otherwise.
     */
    int (*gpio)(uint32_t pin, const struct sol_gpio_config *config);

    /**
     * @brief Callback to setup the pins used of the given i2c bus number to operate in I2C mode.
     *
     * Soletta will call this function so the module can execute the instructions
     * needed to configure the pins used by the given i2c bus to operate in I2C mode.
     *
     * @param bus the i2c bus number.
     *
     * @return @c 0 on success, error code (always negative) otherwise.
     */
    int (*i2c)(uint8_t bus);

    /**
     * @brief Callback to setup the given pin to operate as PWM.
     *
     * Soletta will call this function so the module can execute the instructions
     * needed to configure 'device'/'channel' pair to operate as PWM.
     *
     * @param device the pwm device number.
     * @param channel the channel number on device.
     *
     * @return @c 0 on success, error code (always negative) otherwise.
     */
    int (*pwm)(int device, int channel);

} sol_pin_mux;

/**
 * @def SOL_PIN_MUX_DECLARE(_NAME, decl ...)
 * @brief Helper macro to make easier to correctly declare the symbol
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
