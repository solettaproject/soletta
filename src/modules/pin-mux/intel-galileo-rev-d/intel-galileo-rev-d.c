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
// Galileo Gen1 Multiplexer Description
// =============================================================================

static const struct mux_description desc_16[] = {
    { 42, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { 42, PIN_LOW, MODE_SPI },
    { }
};

static const struct mux_description desc_18[] = {
    { 30, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { }
};

static const struct mux_description desc_25[] = {
    { 43, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { 43, PIN_LOW, MODE_SPI },
    { }
};

static const struct mux_description desc_32[] = {
    { 31, PIN_HIGH, MODE_GPIO },
    { }
};

static const struct mux_description desc_38 [] = {
    { 54, PIN_HIGH, MODE_GPIO },
    { 54, PIN_LOW, MODE_SPI },
    { }
};

static const struct mux_description desc_39[] = {
    { 55, PIN_HIGH, MODE_GPIO | MODE_PWM },
    { 55, PIN_LOW, MODE_SPI },
    { }
};

static const struct mux_description desc_44[] = {
    { 37, PIN_HIGH, MODE_GPIO },
    { 37, PIN_LOW, MODE_ANALOG },
    { }
};

static const struct mux_description desc_45[] = {
    { 36, PIN_HIGH, MODE_GPIO },
    { 36, PIN_LOW, MODE_ANALOG },
    { }
};

static const struct mux_description desc_46[] = {
    { 23, PIN_HIGH, MODE_GPIO },
    { 23, PIN_LOW, MODE_ANALOG },
    { }
};

static const struct mux_description desc_47[] = {
    { 22, PIN_HIGH, MODE_GPIO },
    { 22, PIN_LOW, MODE_ANALOG },
    { }
};

static const struct mux_description desc_48[] = {
    { 21, PIN_HIGH, MODE_GPIO },
    { 21, PIN_LOW, MODE_ANALOG },
    { 29, PIN_HIGH, MODE_ANALOG | MODE_GPIO },
    { 29, PIN_LOW, MODE_I2C },
    { }
};

static const struct mux_description desc_49[] = {
    { 20, PIN_HIGH, MODE_GPIO },
    { 20, PIN_LOW, MODE_ANALOG },
    { 29, PIN_HIGH, MODE_ANALOG | MODE_GPIO },
    { 29, PIN_LOW, MODE_I2C },
    { }
};

static const struct mux_description desc_50[] = {
    { 40, PIN_HIGH, MODE_GPIO },
    { 40, PIN_LOW, MODE_UART },
    { }
};

static const struct mux_description desc_51[] = {
    { 41, PIN_HIGH, MODE_GPIO },
    { 41, PIN_LOW, MODE_UART },
    { }
};

// =============================================================================
// Galileo Gen1 Multiplexers
// =============================================================================

//AIO
static const struct mux_description *const aio_dev_0[] = {
    desc_44, desc_45, desc_46, desc_47, desc_48, desc_49
};

static const struct mux_controller aio_controller_list[] = {
    { sol_util_array_size(aio_dev_0), aio_dev_0 },
};

//GPIO
static const struct mux_description *const gpio_dev_0[52] = {
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
static const struct mux_description *const i2c_dev_0[][2] = {
    { desc_48, desc_49 }
};

//PWM
static const struct mux_description *const pwm_dev_0[8] = {
    [3] = desc_18,
    [4] = desc_25,
    [7] = desc_16,
};

static const struct mux_controller pwm_controller_list[] = {
    { sol_util_array_size(pwm_dev_0), pwm_dev_0 },
};

// =============================================================================

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

SOL_PIN_MUX_DECLARE(INTEL_GALILEO_REV_D,
    .plat_name = "intel-galileo-rev-d",
    .shutdown = mux_shutdown,
    .aio = _set_aio,
    .gpio = _set_gpio,
    .i2c = _set_i2c,
    .pwm = _set_pwm
    );
