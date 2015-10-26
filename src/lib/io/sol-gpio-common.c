/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "gpio");

#include "sol-gpio.h"

#ifdef USE_PIN_MUX
#include "sol-pin-mux.h"
#endif

SOL_API struct sol_gpio *
sol_gpio_label_open(const char *label, const struct sol_gpio_config *config)
{
    int pin;

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifdef USE_PIN_MUX
    if (!sol_pin_mux_map(label, SOL_IO_GPIO, &pin))
        return sol_gpio_open(pin, config);

    SOL_WRN("Label '%s' couldn't be mapped or can't be used as GPIO", label);
#else
    SOL_INF("Pin Multiplexer support is necessary to open a 'board pin'.");
#endif

    return NULL;
}

SOL_API struct sol_gpio *
sol_gpio_open(int pin, const struct sol_gpio_config *config)
{
    struct sol_gpio *gpio;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(config, NULL);

    gpio = sol_gpio_open_raw(pin, config);
#ifdef USE_PIN_MUX
    if (gpio && sol_pin_mux_setup_gpio(pin, config->dir)) {
        SOL_ERR("Pin Multiplexer Recipe for gpio=%d found, but couldn't be applied.", pin);
        sol_gpio_close(gpio);
        gpio = NULL;
    }
#endif

    return gpio;
}
