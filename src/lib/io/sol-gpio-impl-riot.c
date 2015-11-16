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
    uint32_t pin;
    bool active_low;
    struct {
        void (*cb)(void *data, struct sol_gpio *gpio, bool value);
        const void *data;
        void *int_handler;
        struct sol_timeout *timeout;
        bool last_value : 1;
        bool on_raise : 1;
        bool on_fall : 1;
    } irq;
};

static void
gpio_process_cb(void *data)
{
    struct sol_gpio *gpio = data;
    bool val;

    val = sol_gpio_read(gpio);
    gpio->irq.cb((void *)gpio->irq.data, gpio, val);
}

static bool
gpio_timeout_cb(void *data)
{
    struct sol_gpio *gpio = data;
    int val;

    val = sol_gpio_read(gpio);
    if (gpio->irq.last_value != val) {
        gpio->irq.last_value = val;
        if ((val && gpio->irq.on_raise)
            || (!val && gpio->irq.on_fall))
            gpio->irq.cb((void *)gpio->irq.data, gpio, val);
    }
    return true;
}

SOL_API struct sol_gpio *
sol_gpio_open_raw(uint32_t pin, const struct sol_gpio_config *config)
{
    struct sol_gpio *gpio;
    gpio_pp_t pull;
    const unsigned int drive_table[] = {
        [SOL_GPIO_DRIVE_NONE] = GPIO_NOPULL,
        [SOL_GPIO_DRIVE_PULL_UP] = GPIO_PULLUP,
        [SOL_GPIO_DRIVE_PULL_DOWN] = GPIO_PULLDOWN
    };

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (unlikely(config->api_version != SOL_GPIO_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open gpio that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_GPIO_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    gpio = calloc(1, sizeof(struct sol_gpio));
    SOL_NULL_CHECK(gpio, NULL);

    gpio->pin = pin;
    gpio->active_low = config->active_low;

    pull = drive_table[config->drive_mode];

    if (config->dir == SOL_GPIO_DIR_OUT) {
        if (gpio_init(gpio->pin, GPIO_DIR_OUT, pull) < 0)
            goto error;
        sol_gpio_write(gpio, config->out.value);
    } else {
        uint32_t poll_timeout = 0;
        enum sol_gpio_edge trig = config->in.trigger_mode;

        if (trig != SOL_GPIO_EDGE_NONE) {
            gpio_flank_t flank;
            const unsigned int trigger_table[] = {
                [SOL_GPIO_EDGE_RISING] = gpio->active_low ? GPIO_FALLING : GPIO_RISING,
                [SOL_GPIO_EDGE_FALLING] = gpio->active_low ? GPIO_RISING : GPIO_FALLING,
                [SOL_GPIO_EDGE_BOTH] = GPIO_BOTH
            };

            flank = trigger_table[trig];

            gpio->irq.cb = config->in.cb;
            gpio->irq.data = config->in.user_data;
            if (!sol_interrupt_scheduler_gpio_init_int(gpio->pin, pull, flank,
                gpio_process_cb, gpio, &gpio->irq.int_handler))
                goto end;

            SOL_WRN("gpio #%" PRIu32 ": Could not set interrupt mode, falling back to polling", pin);

            if (!(poll_timeout = config->in.poll_timeout)) {
                SOL_WRN("gpio #%" PRIu32 ": No timeout set, cannot fallback to polling mode", pin);
                goto error;
            }
        }

        if (gpio_init(gpio->pin, GPIO_DIR_IN, pull) < 0)
            goto error;
        if (poll_timeout) {
            gpio->irq.timeout = sol_timeout_add(poll_timeout, gpio_timeout_cb, gpio);
            SOL_NULL_CHECK_GOTO(gpio->irq.timeout, error);

            gpio->irq.on_raise = (trig == SOL_GPIO_EDGE_BOTH || trig == SOL_GPIO_EDGE_RISING);
            gpio->irq.on_fall = (trig == SOL_GPIO_EDGE_BOTH || trig == SOL_GPIO_EDGE_FALLING);
            gpio->irq.last_value = sol_gpio_read(gpio);
        }
    }

end:
    return gpio;
error:
    free(gpio);
    return NULL;
}

SOL_API void
sol_gpio_close(struct sol_gpio *gpio)
{
    SOL_NULL_CHECK(gpio);
    if (gpio->irq.int_handler != NULL) {
        sol_interrupt_scheduler_gpio_stop(gpio->pin, gpio->irq.int_handler);
    }
    if (gpio->irq.timeout)
        sol_timeout_del(gpio->irq.timeout);
    free(gpio);
}

SOL_API bool
sol_gpio_write(struct sol_gpio *gpio, bool value)
{
    SOL_NULL_CHECK(gpio, false);
    gpio_write(gpio->pin, gpio->active_low ^ value);
    return true;
}

SOL_API int
sol_gpio_read(struct sol_gpio *gpio)
{
    SOL_NULL_CHECK(gpio, -EINVAL);
    return gpio->active_low ^ !!gpio_read(gpio->pin);
}
