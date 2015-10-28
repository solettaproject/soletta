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

#include "intel-common.h"
#include "sol-pin-mux-modules.h"
#include "sol-util.h"

// =============================================================================
// Edison Multiplexer Descriptions
// =============================================================================

static struct mux_description desc_0[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_UART },
    { 248, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 248, PIN_LOW, MODE_GPIO_INPUT | MODE_UART },
    { 216, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 216, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN | MODE_UART },
    { 216, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ },
    { 130, PIN_MODE_0, MODE_GPIO },
    { 130, PIN_MODE_1, MODE_UART },
    { 214, PIN_HIGH, MODE_GPIO | MODE_UART },
    { }
};

static struct mux_description desc_1[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_UART },
    { 249, PIN_HIGH, MODE_GPIO_OUTPUT | MODE_UART },
    { 249, PIN_LOW, MODE_GPIO_INPUT },
    { 217, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 217, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 217, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_UART },
    { 131, PIN_MODE_0, MODE_GPIO },
    { 131, PIN_MODE_1, MODE_UART },
    { 214, PIN_HIGH, MODE_GPIO | MODE_UART },
    { }
};

static struct mux_description desc_2[] = {
    { 214, PIN_LOW, MODE_GPIO },
    { 250, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 250, PIN_LOW, MODE_GPIO_INPUT },
    { 218, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 218, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 218, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ },
    { 128, PIN_MODE_0, MODE_GPIO },
    { 214, PIN_HIGH, MODE_GPIO },
    { }
};

static struct mux_description desc_3[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_PWM },
    { 251, PIN_HIGH, MODE_GPIO_OUTPUT | MODE_PWM },
    { 251, PIN_LOW, MODE_GPIO_INPUT },
    { 219, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 219, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 219, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_PWM },
    {  12, PIN_MODE_0, MODE_GPIO },
    {  12, PIN_MODE_1, MODE_PWM },
    { 214, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { }
};

static struct mux_description desc_4[] = {
    { 214, PIN_LOW, MODE_GPIO },
    { 252, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 252, PIN_LOW, MODE_GPIO_INPUT },
    { 220, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 220, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 220, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ },
    { 129, PIN_MODE_0, MODE_GPIO },
    { 214, PIN_HIGH, MODE_GPIO },
    { }
};

static struct mux_description desc_5[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_PWM },
    { 253, PIN_HIGH, MODE_GPIO_OUTPUT | MODE_PWM },
    { 253, PIN_LOW, MODE_GPIO_INPUT },
    { 221, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 221, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 221, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_PWM },
    {  13, PIN_MODE_0, MODE_GPIO },
    {  13, PIN_MODE_1, MODE_PWM },
    { 214, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { }
};

static struct mux_description desc_6[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_PWM },
    { 254, PIN_HIGH, MODE_GPIO_OUTPUT | MODE_PWM },
    { 254, PIN_LOW, MODE_GPIO_INPUT },
    { 222, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 222, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 222, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_PWM },
    { 182, PIN_MODE_0, MODE_GPIO },
    { 182, PIN_MODE_1, MODE_PWM },
    { 214, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { }
};

static struct mux_description desc_7[] = {
    { 214, PIN_LOW, MODE_GPIO },
    { 255, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 255, PIN_LOW, MODE_GPIO_INPUT },
    { 223, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 223, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 223, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ },
    {  48, PIN_MODE_0, MODE_GPIO },
    { 214, PIN_HIGH, MODE_GPIO },
    { }
};

static struct mux_description desc_8[] = {
    { 214, PIN_LOW, MODE_GPIO },
    { 256, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 256, PIN_LOW, MODE_GPIO_INPUT },
    { 224, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 224, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 224, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ },
    {  49, PIN_MODE_0, MODE_GPIO },
    { 214, PIN_HIGH, MODE_GPIO },
    { }
};

static struct mux_description desc_9[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_PWM },
    { 257, PIN_HIGH, MODE_GPIO_OUTPUT | MODE_PWM },
    { 257, PIN_LOW, MODE_GPIO_INPUT },
    { 225, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 225, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 225, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_PWM },
    { 183, PIN_MODE_0, MODE_GPIO },
    { 183, PIN_MODE_1, MODE_PWM },
    { 214, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { }
};

static struct mux_description desc_10[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_PWM },
    { 258, PIN_HIGH, MODE_GPIO_OUTPUT | MODE_PWM },
    { 258, PIN_LOW, MODE_GPIO_INPUT },
    { 226, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 226, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 226, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_PWM },
    { 240, PIN_LOW, MODE_GPIO },
    { 263, PIN_HIGH, MODE_GPIO },
    { 263, PIN_LOW, MODE_PWM },
    {  41, PIN_MODE_0, MODE_GPIO },
    { 214, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { }
};

static struct mux_description desc_11[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_SPI | MODE_PWM },
    { 259, PIN_HIGH, MODE_GPIO_OUTPUT | MODE_SPI | MODE_PWM },
    { 259, PIN_LOW, MODE_GPIO_INPUT },
    { 227, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 227, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 227, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_SPI },
    { 241, PIN_LOW, MODE_GPIO },
    { 241, PIN_HIGH, MODE_SPI },
    { 262, PIN_LOW, MODE_PWM },
    { 262, PIN_HIGH, MODE_GPIO | MODE_SPI },
    {  43, PIN_MODE_0, MODE_GPIO },
    { 115, PIN_MODE_1, MODE_SPI },
    { 214, PIN_HIGH, MODE_GPIO | MODE_SPI | MODE_PWM },
    { }
};

static struct mux_description desc_12[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_SPI },
    { 260, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 260, PIN_LOW, MODE_GPIO_INPUT | MODE_SPI },
    { 228, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 228, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 228, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_SPI },
    { 242, PIN_LOW, MODE_GPIO },
    { 242, PIN_HIGH, MODE_SPI },
    {  42, PIN_MODE_0, MODE_GPIO },
    { 114, PIN_MODE_1, MODE_SPI },
    { 214, PIN_HIGH, MODE_GPIO | MODE_SPI },
    { }
};

static struct mux_description desc_13[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_SPI },
    { 261, PIN_HIGH, MODE_GPIO_OUTPUT | MODE_SPI },
    { 261, PIN_LOW, MODE_GPIO_INPUT },
    { 229, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 229, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 229, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_SPI },
    { 243, PIN_LOW, MODE_GPIO },
    { 243, PIN_HIGH, MODE_SPI },
    {  40, PIN_MODE_0, MODE_GPIO },
    { 109, PIN_MODE_1, MODE_GPIO },
    { 214, PIN_HIGH, MODE_GPIO | MODE_SPI },
    { }
};

static struct mux_description desc_14[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_ANALOG },
    { 200, PIN_LOW, MODE_GPIO },
    { 200, PIN_HIGH, MODE_ANALOG },
    { 232, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 232, PIN_LOW, MODE_GPIO_INPUT | MODE_ANALOG },
    { 208, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 208, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 208, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_ANALOG },
    {  44, PIN_MODE_0, MODE_GPIO | MODE_ANALOG },
    { 214, PIN_HIGH, MODE_GPIO | MODE_ANALOG },
    { }
};

static struct mux_description desc_15[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_ANALOG },
    { 201, PIN_LOW, MODE_GPIO },
    { 201, PIN_HIGH, MODE_ANALOG },
    { 233, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 233, PIN_LOW, MODE_GPIO_INPUT | MODE_ANALOG },
    { 209, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 209, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 209, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_ANALOG },
    {  45, PIN_MODE_0, MODE_GPIO | MODE_ANALOG },
    { 214, PIN_HIGH, MODE_GPIO | MODE_ANALOG },
    { }
};

static struct mux_description desc_16[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_ANALOG },
    { 202, PIN_LOW, MODE_GPIO },
    { 202, PIN_HIGH, MODE_ANALOG },
    { 234, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 234, PIN_LOW, MODE_GPIO_INPUT | MODE_ANALOG },
    { 210, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 210, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 210, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_ANALOG },
    {  46, PIN_MODE_0, MODE_GPIO | MODE_ANALOG },
    { 214, PIN_HIGH, MODE_GPIO | MODE_ANALOG },
    { }
};

static struct mux_description desc_17[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_ANALOG },
    { 203, PIN_LOW, MODE_GPIO },
    { 203, PIN_HIGH, MODE_ANALOG },
    { 235, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 235, PIN_LOW, MODE_GPIO_INPUT | MODE_ANALOG },
    { 211, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 211, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 211, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_ANALOG },
    {  47, PIN_MODE_0, MODE_GPIO | MODE_ANALOG },
    { 214, PIN_HIGH, MODE_GPIO | MODE_ANALOG },
    { }
};

static struct mux_description desc_18[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_I2C | MODE_ANALOG },
    {  14, PIN_NONE, MODE_I2C },
    { 204, PIN_LOW, MODE_GPIO | MODE_I2C },
    { 204, PIN_HIGH, MODE_ANALOG },
    { 236, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 236, PIN_LOW, MODE_GPIO_INPUT | MODE_I2C | MODE_ANALOG },
    { 212, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 212, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 212, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_I2C | MODE_ANALOG },
    {  14, PIN_MODE_0, MODE_GPIO | MODE_ANALOG },
    {  27, PIN_MODE_1, MODE_I2C },
    { 214, PIN_HIGH, MODE_GPIO | MODE_I2C | MODE_ANALOG },
    { }
};

static struct mux_description desc_19[] = {
    { 214, PIN_LOW, MODE_GPIO | MODE_I2C | MODE_ANALOG },
    { 165, PIN_NONE, MODE_I2C },
    { 205, PIN_LOW, MODE_GPIO | MODE_I2C },
    { 205, PIN_HIGH, MODE_ANALOG },
    { 237, PIN_HIGH, MODE_GPIO_OUTPUT },
    { 237, PIN_LOW, MODE_GPIO_INPUT | MODE_I2C | MODE_ANALOG },
    { 213, PIN_HIGH, MODE_GPIO_INPUT_PULLUP },
    { 213, PIN_LOW, MODE_GPIO_INPUT_PULLDOWN },
    { 213, PIN_NONE, MODE_GPIO_OUTPUT | MODE_GPIO_INPUT_HIZ | MODE_I2C | MODE_ANALOG },
    { 165, PIN_MODE_0, MODE_GPIO | MODE_ANALOG },
    {  28, PIN_MODE_1, MODE_I2C },
    { 214, PIN_HIGH, MODE_GPIO | MODE_I2C | MODE_ANALOG },
    { }
};

// =============================================================================
// Edison Multiplexers
// =============================================================================

//AIO

// Recipe List for AIO device 1
static struct mux_description *aio_dev_1[] = {
    desc_14, desc_15, desc_16, desc_17, desc_18, desc_19
};

static struct mux_controller aio_controller_list[] = {
    { 0, NULL },
    { ARRAY_SIZE(aio_dev_1), aio_dev_1 },
};

//GPIO
static struct mux_description *gpio_dev_0[184] = {
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
static struct mux_description *i2c_dev_0[][2] = {
    { 0, NULL },
    { 0, NULL },
    { 0, NULL },
    { 0, NULL },
    { 0, NULL },
    { 0, NULL },
    { desc_18, desc_19 },
};

//PWM
static struct mux_description *pwm_dev_0[] = {
    desc_3, desc_5, desc_6, desc_9,
};

static struct mux_controller pwm_controller_list[] = {
    { ARRAY_SIZE(pwm_dev_0), pwm_dev_0 },
};

static struct mux_pin_map pin_map[] = {
    { .label = "A0", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 1, 0 }, .gpio = 44 },
    { .label = "A1", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 1, 1 }, .gpio = 45 },
    { .label = "A2", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 1, 2 }, .gpio = 46 },
    { .label = "A3", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 1, 3 }, .gpio = 47 },
    { .label = "A4", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 1, 4 }, .gpio = 14 },
    { .label = "A5", .cap = SOL_IO_AIO | SOL_IO_GPIO, .aio = { 1, 5 }, .gpio = 165 },
    { .label = "0", .cap = SOL_IO_GPIO, .gpio = 130 },
    { .label = "1", .cap = SOL_IO_GPIO, .gpio = 131 },
    { .label = "2", .cap = SOL_IO_GPIO, .gpio = 128 },
    { .label = "3", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 12, .pwm = { 0, 0 } },
    { .label = "4", .cap = SOL_IO_GPIO, .gpio = 129 },
    { .label = "5", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 13, .pwm = { 0, 1 } },
    { .label = "6", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 182, .pwm = { 0, 2 } },
    { .label = "7", .cap = SOL_IO_GPIO, .gpio = 48 },
    { .label = "8", .cap = SOL_IO_GPIO, .gpio = 49 },
    { .label = "9", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 183, .pwm = { 0, 9 } },
    { .label = "10", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 41, .pwm = { 0, 0 } }, //TODO: Swizzler
    { .label = "11", .cap = SOL_IO_GPIO | SOL_IO_PWM, .gpio = 43, .pwm = { 0, 1 } }, //TODO: Swizzler
    { .label = "12", .cap = SOL_IO_GPIO, .gpio = 42 },
    { .label = "13", .cap = SOL_IO_GPIO, .gpio = 40 },
    { }
};

// =============================================================================

static int
_pin_map(const char *label, const enum sol_io_protocol prot, va_list args)
{
    return mux_pin_map(pin_map, label, prot, args);
}

static int
_set_aio(const int device, const int pin)
{
    return mux_set_aio(device, pin, aio_controller_list, (int)ARRAY_SIZE(aio_controller_list));
}

static int
_set_gpio(const int pin, const enum sol_gpio_direction dir)
{
    return mux_set_gpio(pin, dir, gpio_dev_0, (int)ARRAY_SIZE(gpio_dev_0));
}

static int
_set_i2c(const uint8_t bus)
{
    return mux_set_i2c(bus, i2c_dev_0, ARRAY_SIZE(i2c_dev_0));
}

static int
_set_pwm(const int device, const int channel)
{
    return mux_set_pwm(device, channel, pwm_controller_list, (int)ARRAY_SIZE(pwm_controller_list));
}

SOL_PIN_MUX_DECLARE(INTEL_EDISON_REV_C,
    .plat_name = "intel-edison-rev-c",
    .shutdown = mux_shutdown,
    .pin_map = _pin_map,
    .aio = _set_aio,
    .gpio = _set_gpio,
    .i2c = _set_i2c,
    .pwm = _set_pwm
    );
