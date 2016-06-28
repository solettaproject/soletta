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

#include <errno.h>
#include <stdlib.h>

// Riot includes
#include <periph/gpio.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-gpio.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
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
    gpio_mode_t mode;
    const unsigned int drive_table[] = {
        [SOL_GPIO_DRIVE_NONE] = GPIO_IN,
        [SOL_GPIO_DRIVE_PULL_UP] = GPIO_IN_PU,
        [SOL_GPIO_DRIVE_PULL_DOWN] = GPIO_IN_PD
    };

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(config->api_version != SOL_GPIO_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open gpio that has unsupported version '%" PRIu16 "', "
            "expected version is '%" PRIu16 "'",
            config->api_version, SOL_GPIO_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    gpio = calloc(1, sizeof(struct sol_gpio));
    SOL_NULL_CHECK(gpio, NULL);

    gpio->pin = pin;
    gpio->active_low = config->active_low;

    mode = drive_table[config->drive_mode];

    if (config->dir == SOL_GPIO_DIR_OUT) {
        if (gpio_init(gpio->pin, GPIO_OUT) < 0)
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
            if (!sol_interrupt_scheduler_gpio_init_int(gpio->pin, mode, flank,
                gpio_process_cb, gpio, &gpio->irq.int_handler))
                goto end;

            SOL_WRN("gpio #%" PRIu32 ": Could not set interrupt mode, falling back to polling", pin);

            if (!(poll_timeout = config->in.poll_timeout)) {
                SOL_WRN("gpio #%" PRIu32 ": No timeout set, cannot fallback to polling mode", pin);
                goto error;
            }
        } else {
            SOL_INF("gpio #%" PRIu32 ": Trigger mode set to 'none': events will never trigger.", pin);
        }

        if (gpio_init(gpio->pin, mode) < 0)
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
