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

#include "sol-gpio.h"
#include "sol-pin-mux.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pin logical value to be used
 */
enum mux_pin_val {
    PIN_LOW = 0, /**< Logical zero */
    PIN_HIGH, /**< Logical one */
    PIN_NONE, /**< Pin should be disable i.e. set to high impedance input */
    PIN_MODE_0,
    PIN_MODE_1,
    PIN_MODE_2,
    PIN_MODE_3,
    PIN_MODE_4,
    PIN_MODE_5
};

/**
 * Mode in which the pin will be set to operate
 */
enum mux_mode {
    MODE_GPIO_INPUT_PULLUP = 0x01, /**< GPIO Input (Pull-up) */
    MODE_GPIO_INPUT_PULLDOWN = 0x02, /**< GPIO Input (Pull-down) */
    MODE_GPIO_INPUT_HIZ = 0x04, /**< GPIO Input (High impedance) */
    MODE_GPIO_OUTPUT = 0x08, /**< GPIO Output */
    MODE_PWM = 0x10, /**< PWM */
    MODE_I2C = 0x20, /**< I2C */
    MODE_ANALOG = 0x40, /**< Analog Reader */
    MODE_UART = 0x80, /**< UART */
    MODE_SPI = 0x100, /**< SPI */
    MODE_SWITCH = 0x200, /**< SWITCH */
    MODE_RESERVED = 0x400 /**< Reserved */
};

/* Combinations of the above for convenience */
#define MODE_GPIO_INPUT (MODE_GPIO_INPUT_PULLUP | MODE_GPIO_INPUT_PULLDOWN | MODE_GPIO_INPUT_HIZ)
#define MODE_GPIO (MODE_GPIO_INPUT | MODE_GPIO_OUTPUT)

struct mux_description {
    uint32_t gpio_pin; /**< GPIO pin that controls the mux */
    enum mux_pin_val val; /**< Pin value */
    enum mux_mode mode; /**< Combination of possible pin operation modes */
}; /**< Description of a rule to be applied to setup the multiplexer of a given pin */

/**
 * Structure containing recipes list for the controller's pin set. Controller is the 'chipset'
 * controlling a set of pins of a given protocol, mode or technology.
 */
struct mux_controller {
    unsigned int len; /**< Size of the pin set list */
    struct mux_description **recipe; /**< A list of mux recipes for each pin */
};

struct mux_pin_map {
    const char *label; /**< Pin label on the board */
    int cap; /**< Combination of protocols that share the pin */
    uint32_t gpio; /**< GPIO internal value */
    struct {
        int device;
        int pin;
    } aio, pwm; /**< AIO and PWM mapping */
};

void mux_shutdown(void);

int mux_pin_map(const struct mux_pin_map *map, const char *label, const enum sol_io_protocol prot,
    va_list args);

int mux_set_aio(const int device, const int pin, const struct mux_controller *ctl_list,
    const int s);

int mux_set_gpio(const uint32_t pin, const enum sol_gpio_direction dir,
    struct mux_description **const desc_list, const uint32_t s);

int mux_set_i2c(const uint8_t bus, struct mux_description * (*const desc_list)[2],
    const unsigned int s);

int mux_set_pwm(const int device, const int channel, const struct mux_controller *ctl_list,
    const int s);

#ifdef __cplusplus
}
#endif
