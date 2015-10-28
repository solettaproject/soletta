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
 * Structure containing the recipes (lists of rules) that should be used
 * to multiplex the pins of a given platform
 */
#define SOL_PIN_MUX_API_VERSION (2UL)
struct sol_pin_mux {
    unsigned long int api_version; /**< API version */
    const char *plat_name; /**< Name this multiplexer target platform */

    int (*init)(void);
    void (*shutdown)(void);

    int (*pin_map)(const char *label, const enum sol_io_protocol prot, va_list args);

    int (*aio)(const int device, const int pin);
    int (*gpio)(const int pin, const enum sol_gpio_direction dir);
    int (*i2c)(const uint8_t bus);
    int (*pwm)(const int device, const int channel);

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
