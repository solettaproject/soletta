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

#include "sol-util.h"
#include "sol-pin-mux-impl.h"

// =============================================================================
// Galileo Gen2 Multiplexer Description
// =============================================================================

static struct sol_pin_mux_description desc_0[] = {
    { 32, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_OUTPUT },
    { 32, SOL_PIN_HIGH, SOL_PIN_MODE_UART | SOL_PIN_MODE_GPIO_INPUT },
    { 33, SOL_PIN_NONE, SOL_PIN_MODE_UART | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 33, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { }
};

static struct sol_pin_mux_description desc_1[] = {
    { 45, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 45, SOL_PIN_HIGH, SOL_PIN_MODE_UART },
    { 28, SOL_PIN_LOW, SOL_PIN_MODE_UART | SOL_PIN_MODE_GPIO_OUTPUT },
    { 28, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT },
    { 29, SOL_PIN_NONE, SOL_PIN_MODE_UART | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 29, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 29, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_2[] = {
    { 77, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 77, SOL_PIN_HIGH, SOL_PIN_MODE_UART },
    { 34, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_OUTPUT },
    { 34, SOL_PIN_HIGH, SOL_PIN_MODE_UART | SOL_PIN_MODE_GPIO_INPUT },
    { 35, SOL_PIN_NONE, SOL_PIN_MODE_UART | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 35, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 35, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 13, SOL_PIN_NONE, SOL_PIN_MODE_GPIO },
    { 61, SOL_PIN_NONE, SOL_PIN_MODE_UART },
    { }
};

static struct sol_pin_mux_description desc_3[] = {
    { 64, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 64, SOL_PIN_HIGH, SOL_PIN_MODE_PWM },
    { 76, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { 76, SOL_PIN_HIGH, SOL_PIN_MODE_UART },
    { 16, SOL_PIN_LOW, SOL_PIN_MODE_UART | SOL_PIN_MODE_PWM | SOL_PIN_MODE_GPIO_OUTPUT },
    { 16, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT },
    { 17, SOL_PIN_NONE, SOL_PIN_MODE_UART | SOL_PIN_MODE_PWM | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 17, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 17, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 14, SOL_PIN_NONE, SOL_PIN_MODE_GPIO },
    { 62, SOL_PIN_NONE, SOL_PIN_MODE_PWM | SOL_PIN_MODE_UART },
    { }
};

static struct sol_pin_mux_description desc_4[] = {
    { 36, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_OUTPUT },
    { 36, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT },
    { 37, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 37, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 37, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_5[] = {
    { 66, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 66, SOL_PIN_HIGH, SOL_PIN_MODE_PWM },
    { 18, SOL_PIN_LOW, SOL_PIN_MODE_PWM | SOL_PIN_MODE_GPIO_OUTPUT },
    { 18, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT },
    { 19, SOL_PIN_NONE, SOL_PIN_MODE_PWM | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 19, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 19, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_6[] = {
    { 68, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 68, SOL_PIN_HIGH, SOL_PIN_MODE_PWM },
    { 20, SOL_PIN_LOW, SOL_PIN_MODE_PWM | SOL_PIN_MODE_GPIO_OUTPUT },
    { 20, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT },
    { 21, SOL_PIN_NONE, SOL_PIN_MODE_PWM | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 21, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 21, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_7[] = {
    { 39, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 39, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 39, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_8[] = {
    { 41, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 41, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 41, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_9[] = {
    { 70, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 70, SOL_PIN_HIGH, SOL_PIN_MODE_PWM },
    { 22, SOL_PIN_LOW, SOL_PIN_MODE_PWM | SOL_PIN_MODE_GPIO_OUTPUT },
    { 22, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT },
    { 23, SOL_PIN_NONE, SOL_PIN_MODE_PWM | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 23, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 23, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_10[] = {
    { 74, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 74, SOL_PIN_HIGH, SOL_PIN_MODE_PWM },
    { 26, SOL_PIN_LOW, SOL_PIN_MODE_PWM | SOL_PIN_MODE_GPIO_OUTPUT },
    { 26, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT },
    { 27, SOL_PIN_NONE, SOL_PIN_MODE_PWM | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 27, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 27, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_11[] = {
    { 44, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 44, SOL_PIN_HIGH, SOL_PIN_MODE_SPI },
    { 72, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 72, SOL_PIN_LOW, SOL_PIN_MODE_SPI },
    { 72, SOL_PIN_HIGH, SOL_PIN_MODE_PWM },
    { 24, SOL_PIN_LOW, SOL_PIN_MODE_PWM | SOL_PIN_MODE_SPI | SOL_PIN_MODE_GPIO_OUTPUT },
    { 24, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT },
    { 25, SOL_PIN_NONE, SOL_PIN_MODE_PWM | SOL_PIN_MODE_SPI | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 25, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 25, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_12[] = {
    { 42, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_OUTPUT },
    { 42, SOL_PIN_HIGH, SOL_PIN_MODE_SPI | SOL_PIN_MODE_GPIO_INPUT },
    { 43, SOL_PIN_NONE, SOL_PIN_MODE_SPI | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 43, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 43, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_13[] = {
    { 46, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 46, SOL_PIN_HIGH, SOL_PIN_MODE_SPI },
    { 30, SOL_PIN_LOW, SOL_PIN_MODE_SPI | SOL_PIN_MODE_GPIO_OUTPUT },
    { 30, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT },
    { 31, SOL_PIN_NONE, SOL_PIN_MODE_SPI | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 31, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 31, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_14[] = {
    { 48, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG },
    { 49, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 49, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 49, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_15[] = {
    { 50, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG },
    { 51, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 51, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 51, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_16[] = {
    { 52, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG },
    { 53, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 53, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 53, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_17[] = {
    { 54, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG },
    { 55, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 55, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 55, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_18[] = {
    { 78, SOL_PIN_LOW, SOL_PIN_MODE_ANALOG },
    { 78, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 60, SOL_PIN_LOW, SOL_PIN_MODE_I2C },
    { 60, SOL_PIN_HIGH, SOL_PIN_MODE_ANALOG },
    { 60, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 56, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG | SOL_PIN_MODE_I2C },
    { 57, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG | SOL_PIN_MODE_I2C | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 57, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 57, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

static struct sol_pin_mux_description desc_19[] = {
    { 79, SOL_PIN_LOW, SOL_PIN_MODE_ANALOG },
    { 79, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 60, SOL_PIN_LOW, SOL_PIN_MODE_I2C },
    { 60, SOL_PIN_HIGH, SOL_PIN_MODE_ANALOG },
    { 60, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 58, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG | SOL_PIN_MODE_I2C },
    { 59, SOL_PIN_NONE, SOL_PIN_MODE_ANALOG | SOL_PIN_MODE_I2C | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_GPIO_OUTPUT },
    { 59, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 59, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { }
};

// =============================================================================
// Galileo Gen2 Multiplexers
// =============================================================================

//AIO
static struct sol_pin_mux_description *aio_0[] = {
    desc_14, desc_15, desc_16, desc_17, desc_18, desc_19
};

static struct sol_pin_mux_controller galileo_gen2_mux_aio[] = {
    { ARRAY_SIZE(aio_0), aio_0 },
};

//GPIO:
static struct sol_pin_mux_description *galileo_gen2_mux_gpio[63] = {
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
static struct sol_pin_mux_description *galileo_gen2_mux_i2c[][2] = {
    { desc_18, desc_19 }
};

//PWM
static struct sol_pin_mux_description *pwm_0[12] = {
    [1] = desc_3,
    [3] = desc_5,
    [5] = desc_6,
    [7] = desc_9,
    [9] = desc_11,
    [11] = desc_10,
};

static struct sol_pin_mux_controller galileo_gen2_mux_pwm[] = {
    { ARRAY_SIZE(pwm_0), pwm_0 },
};

SOL_PIN_MUX_DECLARE(INTEL_GALILEO_REV_G,
    .plat_name = "intel-galileo-rev-g",
    .aio = { ARRAY_SIZE(galileo_gen2_mux_aio), galileo_gen2_mux_aio },
    .gpio = { ARRAY_SIZE(galileo_gen2_mux_gpio), galileo_gen2_mux_gpio },
    .i2c = { ARRAY_SIZE(galileo_gen2_mux_i2c), galileo_gen2_mux_i2c },
    .pwm = { ARRAY_SIZE(galileo_gen2_mux_pwm), galileo_gen2_mux_pwm },
    );
