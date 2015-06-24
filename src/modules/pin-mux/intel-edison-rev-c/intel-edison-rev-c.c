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
#include "sol-pin-mux-modules.h"

// =============================================================================
// Edison Multiplexer Descriptions
// =============================================================================

static struct sol_pin_mux_description desc_0[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_UART },
    { 248, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 248, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT | SOL_PIN_MODE_UART },
    { 216, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 216, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN | SOL_PIN_MODE_UART },
    { 216, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_UART },
    { }
};

static struct sol_pin_mux_description desc_1[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_UART },
    { 249, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_UART },
    { 249, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 217, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 217, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 217, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_UART },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_UART },
    { }
};

static struct sol_pin_mux_description desc_2[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 250, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 250, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 218, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 218, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 218, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { }
};

static struct sol_pin_mux_description desc_3[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { 251, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_PWM },
    { 251, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 219, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 219, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 219, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_PWM },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { }
};

static struct sol_pin_mux_description desc_4[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 252, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 252, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 220, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 220, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 220, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { }
};

static struct sol_pin_mux_description desc_5[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { 253, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_PWM },
    { 253, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 221, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 221, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 221, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_PWM },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { }
};

static struct sol_pin_mux_description desc_6[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { 254, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_PWM },
    { 254, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 222, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 222, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 222, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_PWM },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { }
};

static struct sol_pin_mux_description desc_7[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 255, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 255, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 223, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 223, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 223, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { }
};

static struct sol_pin_mux_description desc_8[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 256, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 256, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 224, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 224, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 224, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { }
};

static struct sol_pin_mux_description desc_9[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { 257, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_PWM },
    { 257, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 225, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 225, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 225, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_PWM },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { }
};

static struct sol_pin_mux_description desc_10[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { 258, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_PWM },
    { 258, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 226, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 226, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 226, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_PWM },
    { 240, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 263, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 263, SOL_PIN_LOW, SOL_PIN_MODE_PWM },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { }
};

static struct sol_pin_mux_description desc_11[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_SPI | SOL_PIN_MODE_PWM },
    { 259, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_SPI | SOL_PIN_MODE_PWM },
    { 259, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 227, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 227, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 227, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_SPI },
    { 241, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 241, SOL_PIN_HIGH, SOL_PIN_MODE_SPI },
    { 262, SOL_PIN_LOW, SOL_PIN_MODE_PWM },
    { 262, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_SPI },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_SPI | SOL_PIN_MODE_PWM },
    { }
};

static struct sol_pin_mux_description desc_12[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_SPI },
    { 260, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 260, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT | SOL_PIN_MODE_SPI },
    { 228, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 228, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 228, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_SPI },
    { 242, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 242, SOL_PIN_HIGH, SOL_PIN_MODE_SPI },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_SPI },
    { }
};

static struct sol_pin_mux_description desc_13[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_SPI },
    { 261, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_SPI },
    { 261, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT },
    { 229, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 229, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 229, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_SPI },
    { 243, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 243, SOL_PIN_HIGH, SOL_PIN_MODE_SPI },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_SPI },
    { }
};

static struct sol_pin_mux_description desc_14[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_ANALOG },
    { 200, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 200, SOL_PIN_HIGH, SOL_PIN_MODE_ANALOG },
    { 232, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 232, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT | SOL_PIN_MODE_ANALOG },
    { 208, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 208, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 208, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_ANALOG },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_ANALOG },
    { }
};

static struct sol_pin_mux_description desc_15[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_ANALOG },
    { 201, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 201, SOL_PIN_HIGH, SOL_PIN_MODE_ANALOG },
    { 233, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 233, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT | SOL_PIN_MODE_ANALOG },
    { 209, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 209, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 209, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_ANALOG },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_ANALOG },
    { }
};

static struct sol_pin_mux_description desc_16[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_ANALOG },
    { 202, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 202, SOL_PIN_HIGH, SOL_PIN_MODE_ANALOG },
    { 234, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 234, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT | SOL_PIN_MODE_ANALOG },
    { 210, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 210, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 210, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_ANALOG },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_ANALOG },
    { }
};

static struct sol_pin_mux_description desc_17[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_ANALOG },
    { 203, SOL_PIN_LOW, SOL_PIN_MODE_GPIO },
    { 203, SOL_PIN_HIGH, SOL_PIN_MODE_ANALOG },
    { 235, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 235, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT | SOL_PIN_MODE_ANALOG },
    { 211, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 211, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 211, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_ANALOG },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_ANALOG },
    { }
};

static struct sol_pin_mux_description desc_18[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_I2C | SOL_PIN_MODE_ANALOG },
    {  14, SOL_PIN_NONE, SOL_PIN_MODE_I2C },
    { 204, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_I2C },
    { 204, SOL_PIN_HIGH, SOL_PIN_MODE_ANALOG },
    { 236, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 236, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT | SOL_PIN_MODE_I2C | SOL_PIN_MODE_ANALOG },
    { 212, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 212, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 212, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_I2C | SOL_PIN_MODE_ANALOG },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_I2C | SOL_PIN_MODE_ANALOG },
    { }
};

static struct sol_pin_mux_description desc_19[] = {
    { 214, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_I2C | SOL_PIN_MODE_ANALOG },
    { 165, SOL_PIN_NONE, SOL_PIN_MODE_I2C },
    { 205, SOL_PIN_LOW, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_I2C },
    { 205, SOL_PIN_HIGH, SOL_PIN_MODE_ANALOG },
    { 237, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_OUTPUT },
    { 237, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT | SOL_PIN_MODE_I2C | SOL_PIN_MODE_ANALOG },
    { 213, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO_INPUT_PULLUP },
    { 213, SOL_PIN_LOW, SOL_PIN_MODE_GPIO_INPUT_PULLDOWN },
    { 213, SOL_PIN_NONE, SOL_PIN_MODE_GPIO_OUTPUT | SOL_PIN_MODE_GPIO_INPUT_HIZ | SOL_PIN_MODE_I2C | SOL_PIN_MODE_ANALOG },
    { 214, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_I2C | SOL_PIN_MODE_ANALOG },
    { }
};

// =============================================================================
// Edison Multiplexers
// =============================================================================

//AIO
static struct sol_pin_mux_description *aio_0[] = {
    desc_14, desc_15, desc_16, desc_17, desc_18, desc_19
};

static struct sol_pin_mux_controller edison_rev_c_mux_aio[] = {
    { ARRAY_SIZE(aio_0), aio_0 },
};

//GPIO
static struct sol_pin_mux_description *edison_rev_c_mux_gpio[184] = {
    [12] = desc_3,
    [13] = desc_5,
    [14] = desc_18,
    [40] = desc_13,
    [41] = desc_10,
    [42] = desc_12,
    [43] = desc_11,
    [44] = desc_14,
    [45] = desc_15,
    [46] = desc_16,
    [47] = desc_17,
    [48] = desc_7,
    [49] = desc_8,
    [128] = desc_2,
    [129] = desc_4,
    [130] = desc_0,
    [131] = desc_1,
    [165] = desc_19,
    [182] = desc_6,
    [183] = desc_9,
};

//I2C
static struct sol_pin_mux_description *edison_rev_c_mux_i2c[][2] = {
    { desc_18, desc_19 }
};

//PWM
static struct sol_pin_mux_description *pwm_0[] = {
    desc_3, desc_5, desc_6, desc_9,
};

static struct sol_pin_mux_controller edison_rev_c_mux_pwm[] = {
    { ARRAY_SIZE(pwm_0), pwm_0 },
};

SOL_PIN_MUX_DECLARE(INTEL_EDISON_REV_C,
    .plat_name = "intel-edison-rev-c",
    .aio = { ARRAY_SIZE(edison_rev_c_mux_aio), edison_rev_c_mux_aio },
    .gpio = { ARRAY_SIZE(edison_rev_c_mux_gpio), edison_rev_c_mux_gpio },
    .i2c = { ARRAY_SIZE(edison_rev_c_mux_i2c), edison_rev_c_mux_i2c },
    .pwm = { ARRAY_SIZE(edison_rev_c_mux_pwm), edison_rev_c_mux_pwm },
    );
