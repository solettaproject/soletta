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
// Galileo Gen1 Multiplexer Description
// =============================================================================

static struct sol_pin_mux_description desc_16[] = {
    { 42, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { 42, SOL_PIN_LOW, SOL_PIN_MODE_SPI },
    { }
};

static struct sol_pin_mux_description desc_18[] = {
    { 30, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { }
};

static struct sol_pin_mux_description desc_25[] = {
    { 43, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { 43, SOL_PIN_LOW, SOL_PIN_MODE_SPI },
    { }
};

static struct sol_pin_mux_description desc_32[] = {
    { 31, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { }
};

static struct sol_pin_mux_description desc_38 [] = {
    { 54, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 54, SOL_PIN_LOW, SOL_PIN_MODE_SPI },
    { }
};

static struct sol_pin_mux_description desc_39[] = {
    { 55, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO | SOL_PIN_MODE_PWM },
    { 55, SOL_PIN_LOW, SOL_PIN_MODE_SPI },
    { }
};

static struct sol_pin_mux_description desc_44[] = {
    { 37, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 37, SOL_PIN_LOW, SOL_PIN_MODE_ANALOG },
    { }
};

static struct sol_pin_mux_description desc_45[] = {
    { 36, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 36, SOL_PIN_LOW, SOL_PIN_MODE_ANALOG },
    { }
};

static struct sol_pin_mux_description desc_46[] = {
    { 23, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 23, SOL_PIN_LOW, SOL_PIN_MODE_ANALOG },
    { }
};

static struct sol_pin_mux_description desc_47[] = {
    { 22, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 22, SOL_PIN_LOW, SOL_PIN_MODE_ANALOG },
    { }
};

static struct sol_pin_mux_description desc_48[] = {
    { 21, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 21, SOL_PIN_LOW, SOL_PIN_MODE_ANALOG },
    { 29, SOL_PIN_HIGH, SOL_PIN_MODE_ANALOG | SOL_PIN_MODE_GPIO },
    { 29, SOL_PIN_LOW, SOL_PIN_MODE_I2C },
    { }
};

static struct sol_pin_mux_description desc_49[] = {
    { 20, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 20, SOL_PIN_LOW, SOL_PIN_MODE_ANALOG },
    { 29, SOL_PIN_HIGH, SOL_PIN_MODE_ANALOG | SOL_PIN_MODE_GPIO },
    { 29, SOL_PIN_LOW, SOL_PIN_MODE_I2C },
    { }
};

static struct sol_pin_mux_description desc_50[] = {
    { 40, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 40, SOL_PIN_LOW, SOL_PIN_MODE_UART },
    { }
};

static struct sol_pin_mux_description desc_51[] = {
    { 41, SOL_PIN_HIGH, SOL_PIN_MODE_GPIO },
    { 41, SOL_PIN_LOW, SOL_PIN_MODE_UART },
    { }
};

// =============================================================================
// Galileo Gen1 Multiplexers
// =============================================================================

//AIO
static struct sol_pin_mux_description *aio_0[] = {
    desc_44, desc_45, desc_46, desc_47, desc_48, desc_49
};

static struct sol_pin_mux_controller galileo_gen1_mux_aio[] = {
    { ARRAY_SIZE(aio_0), aio_0 },
};

//GPIO
static struct sol_pin_mux_description *galileo_gen1_mux_gpio[52] = {
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
static struct sol_pin_mux_description *galileo_gen1_mux_i2c[][2] = {
    { desc_48, desc_49 }
};

//PWM
static struct sol_pin_mux_description *pwm_0[8] = {
    [3] = desc_18,
    [4] = desc_25,
    [7] = desc_16,
};

static struct sol_pin_mux_controller galileo_gen1_mux_pwm[] = {
    { ARRAY_SIZE(pwm_0), pwm_0 },
};

SOL_PIN_MUX_DECLARE(INTEL_GALILEO_REV_D,
    .plat_name = "intel-galileo-rev-d",
    .aio = { ARRAY_SIZE(galileo_gen1_mux_aio), galileo_gen1_mux_aio },
    .gpio = { ARRAY_SIZE(galileo_gen1_mux_gpio), galileo_gen1_mux_gpio },
    .i2c = { ARRAY_SIZE(galileo_gen1_mux_i2c), galileo_gen1_mux_i2c },
    .pwm = { ARRAY_SIZE(galileo_gen1_mux_pwm), galileo_gen1_mux_pwm },
    );
