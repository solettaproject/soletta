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
// Galileo Gen1 Multiplexer Description
// =============================================================================

static struct mux_description desc_16[] = {
    { 42, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { 42, PIN_LOW, MODE_SPI },
    { }
};

static struct mux_description desc_18[] = {
    { 30, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { }
};

static struct mux_description desc_25[] = {
    { 43, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { 43, PIN_LOW, MODE_SPI },
    { }
};

static struct mux_description desc_32[] = {
    { 31, PIN_HIGH, MODE_GPIO },
    { }
};

static struct mux_description desc_38 [] = {
    { 54, PIN_HIGH, MODE_GPIO },
    { 54, PIN_LOW, MODE_SPI },
    { }
};

static struct mux_description desc_39[] = {
    { 55, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { 55, PIN_LOW, MODE_SPI },
    { }
};

static struct mux_description desc_44[] = {
    { 37, PIN_HIGH, MODE_GPIO },
    { 37, PIN_LOW, MODE_ANALOG },
    { }
};

static struct mux_description desc_45[] = {
    { 36, PIN_HIGH, MODE_GPIO },
    { 36, PIN_LOW, MODE_ANALOG },
    { }
};

static struct mux_description desc_46[] = {
    { 23, PIN_HIGH, MODE_GPIO },
    { 23, PIN_LOW, MODE_ANALOG },
    { }
};

static struct mux_description desc_47[] = {
    { 22, PIN_HIGH, MODE_GPIO },
    { 22, PIN_LOW, MODE_ANALOG },
    { }
};

static struct mux_description desc_48[] = {
    { 21, PIN_HIGH, MODE_GPIO },
    { 21, PIN_LOW, MODE_ANALOG },
    { 29, PIN_HIGH, MODE_ANALOG | MODE_GPIO },
    { 29, PIN_LOW, MODE_I2C },
    { }
};

static struct mux_description desc_49[] = {
    { 20, PIN_HIGH, MODE_GPIO },
    { 20, PIN_LOW, MODE_ANALOG },
    { 29, PIN_HIGH, MODE_ANALOG | MODE_GPIO },
    { 29, PIN_LOW, MODE_I2C },
    { }
};

static struct mux_description desc_50[] = {
    { 40, PIN_HIGH, MODE_GPIO },
    { 40, PIN_LOW, MODE_UART },
    { }
};

static struct mux_description desc_51[] = {
    { 41, PIN_HIGH, MODE_GPIO },
    { 41, PIN_LOW, MODE_UART },
    { }
};

// =============================================================================
// Galileo Gen1 Multiplexers
// =============================================================================

//AIO
static struct mux_description *aio_dev_0[] = {
    desc_44, desc_45, desc_46, desc_47, desc_48, desc_49
};

static struct mux_controller aio_controller_list[] = {
    { ARRAY_SIZE(aio_dev_0), aio_dev_0 },
};

//GPIO
static struct mux_description *gpio_dev_0[52] = {
    [16] = desc_16,
    [18] = desc_18,
    [25] = desc_25,
    [32] = desc_32,
    [38] = desc_38,
    [39] = desc_39,
    [44] = desc_44,
    [45] = desc_45,
    [46] = desc_46,
    [47] = desc_47,
    [48] = desc_48,
    [49] = desc_49,
    [50] = desc_50,
    [51] = desc_51,
};

//I2C
static struct mux_description *i2c_dev_0[][2] = {
    { desc_48, desc_49 }
};

//PWM
static struct mux_description *pwm_dev_0[8] = {
    [3] = desc_18,
    [4] = desc_25,
    [7] = desc_16,
};

static struct mux_controller pwm_controller_list[] = {
    { ARRAY_SIZE(pwm_dev_0), pwm_dev_0 },
};

// =============================================================================

static int
_set_aio(const int device, const int pin)
{
    return set_aio(device, pin, aio_controller_list, (int)ARRAY_SIZE(aio_controller_list));
}

static int
_set_gpio(const int pin, const enum sol_gpio_direction dir)
{
    return set_gpio(pin, dir, gpio_dev_0, (int)ARRAY_SIZE(gpio_dev_0));
}

static int
_set_i2c(const uint8_t bus)
{
    return set_i2c(bus, i2c_dev_0, ARRAY_SIZE(i2c_dev_0));
}

static int
_set_pwm(const int device, const int channel)
{
    return set_pwm(device, channel, pwm_controller_list, (int)ARRAY_SIZE(pwm_controller_list));
}

SOL_PIN_MUX_DECLARE(INTEL_GALILEO_REV_D,
    .plat_name = "intel-galileo-rev-d",
    .aio = _set_aio,
    .gpio = _set_gpio,
    .i2c = _set_i2c,
    .pwm = _set_pwm
    );
