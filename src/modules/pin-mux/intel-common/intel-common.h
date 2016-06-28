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
    const struct mux_description *const *recipe; /**< A list of mux recipes for each pin */
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

int apply_mux_desc(const struct mux_description *desc, unsigned int mode);

void mux_shutdown(void);

int mux_pin_map(const struct mux_pin_map *map, const char *label, const enum sol_io_protocol prot,
    va_list args);

int mux_set_aio(int device, int pin, const struct mux_controller *const ctl_list,
    int s);

int mux_set_gpio(uint32_t pin, const struct sol_gpio_config *config,
    const struct mux_description *const *desc_list, uint32_t s);

int mux_set_i2c(uint8_t bus, const struct mux_description *const (*desc_list)[2],
    unsigned int s);

int mux_set_pwm(int device, int channel, const struct mux_controller *const ctl_list,
    int s);

#ifdef __cplusplus
}
#endif
