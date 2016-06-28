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

#include "intel-common.h"
#include "sol-pin-mux-modules.h"
#include "sol-util-internal.h"

// =============================================================================
// Galileo Gen2 Multiplexer Description
// =============================================================================

static const struct mux_description desc_0[] = {
    { 32, PIN_LOW, MODE_GPIO_OUTPUT },
    { 32, PIN_HIGH, MODE_UART | MODE_GPIO_INPUT },
    { 33, PIN_NONE, MODE_UART | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 33, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { }
};

static const struct mux_description desc_1[] = {
    { 45, PIN_LOW, MODE_GPIO },
    { 45, PIN_HIGH, MODE_UART },
    { 28, PIN_LOW, MODE_UART | MODE_GPIO_OUTPUT },
    { 28, PIN_HIGH, MODE_GPIO_INPUT },
    { 29, PIN_NONE, MODE_UART | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 29, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 29, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_2[] = {
    { 77, PIN_LOW, MODE_GPIO },
    { 77, PIN_HIGH, MODE_UART },
    { 34, PIN_LOW, MODE_GPIO_OUTPUT },
    { 34, PIN_HIGH, MODE_UART | MODE_GPIO_INPUT },
    { 35, PIN_NONE, MODE_UART | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 35, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 35, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 13, PIN_NONE, MODE_GPIO },
    { 61, PIN_NONE, MODE_UART },
    { }
};

static const struct mux_description desc_3[] = {
    { 64, PIN_LOW, MODE_GPIO },
    { 64, PIN_HIGH, MODE_PWM },
    { 76, PIN_LOW, MODE_GPIO | MODE_PWM },
    { 76, PIN_HIGH, MODE_UART },
    { 16, PIN_LOW, MODE_UART | MODE_PWM | MODE_GPIO_OUTPUT },
    { 16, PIN_HIGH, MODE_GPIO_INPUT },
    { 17, PIN_NONE, MODE_UART | MODE_PWM | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 17, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 17, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 14, PIN_NONE, MODE_GPIO },
    { 62, PIN_NONE, MODE_PWM | MODE_UART },
    { }
};

static const struct mux_description desc_4[] = {
    { 36, PIN_LOW, MODE_GPIO_OUTPUT },
    { 36, PIN_HIGH, MODE_GPIO_INPUT },
    { 37, PIN_NONE, MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 37, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 37, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_5[] = {
    { 66, PIN_LOW, MODE_GPIO },
    { 66, PIN_HIGH, MODE_PWM },
    { 18, PIN_LOW, MODE_PWM | MODE_GPIO_OUTPUT },
    { 18, PIN_HIGH, MODE_GPIO_INPUT },
    { 19, PIN_NONE, MODE_PWM | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 19, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 19, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_6[] = {
    { 68, PIN_LOW, MODE_GPIO },
    { 68, PIN_HIGH, MODE_PWM },
    { 20, PIN_LOW, MODE_PWM | MODE_GPIO_OUTPUT },
    { 20, PIN_HIGH, MODE_GPIO_INPUT },
    { 21, PIN_NONE, MODE_PWM | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 21, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 21, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_7[] = {
    { 39, PIN_NONE, MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 39, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 39, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_8[] = {
    { 41, PIN_NONE, MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 41, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 41, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_9[] = {
    { 70, PIN_LOW, MODE_GPIO },
    { 70, PIN_HIGH, MODE_PWM },
    { 22, PIN_LOW, MODE_PWM | MODE_GPIO_OUTPUT },
    { 22, PIN_HIGH, MODE_GPIO_INPUT },
    { 23, PIN_NONE, MODE_PWM | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 23, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 23, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_10[] = {
    { 74, PIN_LOW, MODE_GPIO },
    { 74, PIN_HIGH, MODE_PWM },
    { 26, PIN_LOW, MODE_PWM | MODE_GPIO_OUTPUT },
    { 26, PIN_HIGH, MODE_GPIO_INPUT },
    { 27, PIN_NONE, MODE_PWM | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 27, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 27, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_11[] = {
    { 44, PIN_LOW, MODE_GPIO },
    { 44, PIN_HIGH, MODE_SPI },
    { 72, PIN_LOW, MODE_GPIO },
    { 72, PIN_LOW, MODE_SPI },
    { 72, PIN_HIGH, MODE_PWM },
    { 24, PIN_LOW, MODE_PWM | MODE_SPI | MODE_GPIO_OUTPUT },
    { 24, PIN_HIGH, MODE_GPIO_INPUT },
    { 25, PIN_NONE, MODE_PWM | MODE_SPI | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 25, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 25, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_12[] = {
    { 42, PIN_LOW, MODE_GPIO_OUTPUT },
    { 42, PIN_HIGH, MODE_SPI | MODE_GPIO_INPUT },
    { 43, PIN_NONE, MODE_SPI | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 43, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 43, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_13[] = {
    { 46, PIN_LOW, MODE_GPIO },
    { 46, PIN_HIGH, MODE_SPI },
    { 30, PIN_LOW, MODE_SPI | MODE_GPIO_OUTPUT },
    { 30, PIN_HIGH, MODE_GPIO_INPUT },
    { 31, PIN_NONE, MODE_SPI | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 31, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 31, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_14[] = {
    { 48, PIN_NONE, MODE_ANALOG },
    { 49, PIN_NONE, MODE_ANALOG | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 49, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 49, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_15[] = {
    { 50, PIN_NONE, MODE_ANALOG },
    { 51, PIN_NONE, MODE_ANALOG | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 51, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 51, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_16[] = {
    { 52, PIN_NONE, MODE_ANALOG },
    { 53, PIN_NONE, MODE_ANALOG | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 53, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 53, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_17[] = {
    { 54, PIN_NONE, MODE_ANALOG },
    { 55, PIN_NONE, MODE_ANALOG | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 55, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 55, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_18[] = {
    { 78, PIN_LOW, MODE_ANALOG },
    { 78, PIN_HIGH, MODE_GPIO },
    { 60, PIN_LOW, MODE_I2C },
    { 60, PIN_HIGH, MODE_ANALOG },
    { 60, PIN_HIGH, MODE_GPIO },
    { 56, PIN_NONE, MODE_ANALOG | MODE_I2C },
    { 57, PIN_NONE, MODE_ANALOG | MODE_I2C | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 57, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 57, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static const struct mux_description desc_19[] = {
    { 79, PIN_LOW, MODE_ANALOG },
    { 79, PIN_HIGH, MODE_GPIO },
    { 60, PIN_LOW, MODE_I2C },
    { 60, PIN_HIGH, MODE_ANALOG },
    { 60, PIN_HIGH, MODE_GPIO },
    { 58, PIN_NONE, MODE_ANALOG | MODE_I2C },
    { 59, PIN_NONE, MODE_ANALOG | MODE_I2C | MODE_GPIO_INPUT_HIZ | MODE_GPIO_OUTPUT },
    { 59, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 59, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { }
};

// =============================================================================
// Galileo Gen2 Multiplexers
// =============================================================================

//AIO
static const struct mux_description *const aio_dev_0[] = {
    desc_14, desc_15, desc_16, desc_17, desc_18, desc_19
};

static const struct mux_controller aio_controller_list[] = {
    { sol_util_array_size(aio_dev_0), aio_dev_0 },
};

//GPIO:
static const struct mux_description *const gpio_dev_0[63] = {
    [0] = desc_5,
    [1] = desc_6,
    [2 ... 3] = NULL,
    [4] = desc_9,
    [5] = desc_11,
    [6] = desc_4,
    [7] = desc_13,
    [8 ... 9] = NULL,
    [10] = desc_10,
    [11] = desc_0,
    [12] = desc_1,
    [13 ... 14] = NULL,
    [15] = desc_12,
    [16 ... 37] = NULL,
    [38] = desc_7,
    [39] = NULL,
    [40] = desc_8,
    [41 ... 47] = NULL,
    [48] = desc_14,
    [49] = NULL,
    [50] = desc_15,
    [51] = NULL,
    [52] = desc_16,
    [53] = NULL,
    [54] = desc_17,
    [55] = NULL,
    [56] = desc_18,
    [57] = NULL,
    [58] = desc_19,
    [59 ... 60] = NULL,
    [61] = desc_2,
    [62] = desc_3,
};

//I2C
static const struct mux_description *const i2c_dev_0[][2] = {
    { desc_18, desc_19 }
};

//PWM
static const struct mux_description *const pwm_dev_0[12] = {
    [1] = desc_3,
    [3] = desc_5,
    [5] = desc_6,
    [7] = desc_9,
    [9] = desc_11,
    [11] = desc_10,
};

static const struct mux_controller pwm_controller_list[] = {
    { sol_util_array_size(pwm_dev_0), pwm_dev_0 },
};

static const struct mux_pin_map pin_map[] = {
    { .label = "A0", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 0, 0 }, .gpio = 14 },
    { .label = "A1", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 0, 1 }, .gpio = 15 },
    { .label = "A2", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 0, 2 }, .gpio = 16 },
    { .label = "A3", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 0, 3 }, .gpio = 17 },
    { .label = "A4", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 0, 4 }, .gpio = 18 },
    { .label = "A5", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 0, 5 }, .gpio = 19 },
    { .label = "0", .cap = SOL_IO_GPIO, .gpio = 11 },
    { .label = "1", .cap = SOL_IO_GPIO, .gpio = 12 },
    { .label = "2", .cap = SOL_IO_GPIO, .gpio = 61 },
    { .label = "3", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 62, .pwm = { 0, 1 } },
    { .label = "4", .cap = SOL_IO_GPIO, .gpio = 6 },
    { .label = "5", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 0, .pwm = { 0, 3 } },
    { .label = "6", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 1, .pwm = { 0, 5 } },
    { .label = "7", .cap = SOL_IO_GPIO, .gpio = 38 },
    { .label = "8", .cap = SOL_IO_GPIO, .gpio = 40 },
    { .label = "9", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 4, .pwm = { 0, 7 } },
    { .label = "10", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 10, .pwm = { 0, 11 } },
    { .label = "11", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 5, .pwm = { 0, 9 } },
    { .label = "12", .cap = SOL_IO_GPIO, .gpio = 15 },
    { .label = "13", .cap = SOL_IO_GPIO, .gpio = 7 },
    { }
};

// =============================================================================

static int
_pin_map(const char *label, const enum sol_io_protocol prot, va_list args)
{
    return mux_pin_map(pin_map, label, prot, args);
}

static int
_set_aio(int device, int pin)
{
    return mux_set_aio(device, pin, aio_controller_list, (int)sol_util_array_size(aio_controller_list));
}

static int
_set_gpio(uint32_t pin, const struct sol_gpio_config *config)
{
    return mux_set_gpio(pin, config, gpio_dev_0, (uint32_t)sol_util_array_size(gpio_dev_0));
}

static int
_set_i2c(uint8_t bus)
{
    return mux_set_i2c(bus, i2c_dev_0, sol_util_array_size(i2c_dev_0));
}

static int
_set_pwm(int device, int channel)
{
    return mux_set_pwm(device, channel, pwm_controller_list, (int)sol_util_array_size(pwm_controller_list));
}

SOL_PIN_MUX_DECLARE(INTEL_GALILEO_REV_G,
    .plat_name = "intel-galileo-rev-g",
    .shutdown = mux_shutdown,
    .pin_map = _pin_map,
    .aio = _set_aio,
    .gpio = _set_gpio,
    .i2c = _set_i2c,
    .pwm = _set_pwm
    );
