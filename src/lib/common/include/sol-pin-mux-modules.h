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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pin logical value to be used
 */
enum sol_pin_val {
    SOL_PIN_LOW = 0, /**< Logical zero */
    SOL_PIN_HIGH, /**< Logical one */
    SOL_PIN_NONE /**< Pin should be disable i.e. set to high impedance input */
};

/**
 * Mode in which the pin will be set to operate
 */
enum sol_pin_mode {
    SOL_PIN_MODE_GPIO_INPUT_PULLUP = 0x01, /**< GPIO Input (Pull-up) */
    SOL_PIN_MODE_GPIO_INPUT_PULLDOWN = 0x02, /**< GPIO Input (Pull-down) */
    SOL_PIN_MODE_GPIO_INPUT_HIZ = 0x04, /**< GPIO Input (High impedance) */
    SOL_PIN_MODE_GPIO_OUTPUT = 0x08, /**< GPIO Output */
    SOL_PIN_MODE_PWM = 0x10, /**< PWM */
    SOL_PIN_MODE_I2C = 0x20, /**< I2C */
    SOL_PIN_MODE_ANALOG = 0x40, /**< Analog Reader */
    SOL_PIN_MODE_UART = 0x80, /**< UART */
    SOL_PIN_MODE_SPI = 0x100, /**< SPI */
    SOL_PIN_MODE_SWITCH = 0x200, /**< SWITCH */
    SOL_PIN_MODE_RESERVED = 0x400 /**< Reserved */
};

/* Combinations of the above for convenience */
#define SOL_PIN_MODE_GPIO_INPUT (SOL_PIN_MODE_GPIO_INPUT_PULLUP | \
    SOL_PIN_MODE_GPIO_INPUT_PULLDOWN | SOL_PIN_MODE_GPIO_INPUT_HIZ)
#define SOL_PIN_MODE_GPIO (SOL_PIN_MODE_GPIO_INPUT | SOL_PIN_MODE_GPIO_OUTPUT)

struct sol_pin_mux_description {
    int gpio_pin; /**< GPIO pin that controls the mux */
    enum sol_pin_val val; /**< Pin value */
    enum sol_pin_mode mode; /**< Combination of possible pin operation modes */
}; /**< Description of a rule to be applied to setup the multiplexer of a given pin */

/**
 * Structure containing recipes list for the controller's pin set. Controller is the 'chipset'
 * controlling a set of pins of a given protocol, mode or technology.
 */
struct sol_pin_mux_controller {
    unsigned int len; /**< Size of the pin set list */
    struct sol_pin_mux_description **recipe; /**< A list of mux recipes for each pin */
};

/**
 * Structure containing the recipes (lists of rules) that should be used
 * to multiplex the pins of a given platform
 */
#define SOL_PIN_MUX_API_VERSION (1)
struct sol_pin_mux {
    uint16_t api_version; /**< API version */
    uint16_t reserved; /* save this hole for a future field */
    const char *plat_name; /**< Name this multiplexer target platform */

    struct sol_pin_mux_controller gpio;

    /**
     * Structure containing the controller's list used by aio and pwm
     */
    struct {
        unsigned int len; /**< Size of controller list */
        struct sol_pin_mux_controller *controllers; /**< A list of mux recipes for each device/pin pair */
    } aio, pwm;

    struct {
        unsigned int len; /**< Size of i2c bus map */
        struct sol_pin_mux_description *(*recipe)[2]; /**< A list of mux recipes pairs (one for each i2c wire: scl and sda) for each i2c bus */
    } i2c;
};

#ifdef SOL_PIN_MUX_MODULE_EXTERNAL
#define SOL_PIN_MUX_DECLARE(_NAME, decl ...) \
    SOL_API const struct sol_pin_mux SOL_PIN_MUX = { SOL_PIN_MUX_API_VERSION, decl }
#else
#define SOL_PIN_MUX_DECLARE(_NAME, decl ...) \
    SOL_API const struct sol_pin_mux SOL_PIN_MUX_ ## _NAME = { SOL_PIN_MUX_API_VERSION, decl }
#endif

#ifdef __cplusplus
}
#endif
