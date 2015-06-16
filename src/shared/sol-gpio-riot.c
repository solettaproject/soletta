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

#include <errno.h>
#include <stdlib.h>

// Riot includes
#include <periph/gpio.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-gpio.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-interrupt_scheduler_riot.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "gpio");

struct sol_gpio {
    int pin;
    bool active_low;
    struct {
        void (*cb)(void *data, struct sol_gpio *gpio);
        const void *data;
        void *int_handler;
    } irq;
};

static void
gpio_process_cb(void *data)
{
    struct sol_gpio *gpio = data;

    gpio->irq.cb((void *)gpio->irq.data, gpio);
}

struct sol_gpio *
sol_gpio_open(int pin, const struct sol_gpio_config *config)
{
    struct sol_gpio *gpio;
    gpio_pp_t pull;
    const unsigned int drive_table[] = {
        [SOL_GPIO_PULL_RESISTOR_NONE] = GPIO_NOPULL,
        [SOL_GPIO_PULL_RESISTOR_UP] = GPIO_PULLUP,
        [SOL_GPIO_PULL_RESISTOR_DOWN] = GPIO_PULLDOWN
    };

    SOL_LOG_INTERNAL_INIT_ONCE;

    gpio = malloc(sizeof(struct sol_gpio));
    SOL_NULL_CHECK(gpio, NULL);

    gpio->pin = pin;
    gpio->active_low = config->active_low;
    gpio->irq.int_handler = NULL;

    pull = drive_table[config->drive_mode];

    if (config->dir == SOL_GPIO_DIR_OUT) {
        if (gpio_init_out(gpio->pin, gpio->pull) < 0)
            goto error;
        sol_gpio_write(gpio, config->out.value);
    } else {
        if (config->in.trigger_mode == SOL_GPIO_EDGE_NONE) {
            if (gpio_init_in(gpio->pin, gpio->pull) < 0)
                goto error;
        } else {
            gpio_flank_t flank;
            const unsigned int trigger_table[] = {
                [SOL_GPIO_EDGE_RISING] = GPIO_RISING,
                [SOL_GPIO_EDGE_FALLING] = GPIO_FALLING,
                [SOL_GPIO_EDGE_BOTH] = GPIO_BOTH
            };

            flank = trigger_table[config->in.trigger_mode];

            gpio->irq.cb = config->in.cb;
            gpio->irq.data = config->in.user_data;
            if (sol_interrupt_scheduler_gpio_init_int(config->pin, pull, flank,
                    gpio_process_cb, gpio,
                    &gpio->irq.int_handler) < 0)
                goto error;
        }
    }

    return gpio;
error:
    free(gpio);
    return NULL;
}

void
sol_gpio_close(struct sol_gpio *gpio)
{
    SOL_NULL_CHECK(gpio);
    if (gpio->irq.int_handler != NULL) {
        sol_interrupt_scheduler_gpio_stop(gpio->pin, gpio->irq.int_handler);
    }
    free(gpio);
}

bool
sol_gpio_write(struct sol_gpio *gpio, bool value)
{
    SOL_NULL_CHECK(gpio, false);
    gpio_write(gpio->pin, gpio->active_low ^ value);
    return true;
}

int
sol_gpio_read(struct sol_gpio *gpio)
{
    SOL_NULL_CHECK(gpio, -EINVAL);
    return gpio->active_low ^ !!gpio_read(gpio->pin);
}
