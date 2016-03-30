/*
 * This file is part of the Soletta Project
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

/* Zephyr includes */
#include "atomic.h"
#include "gpio.h"

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-gpio.h"
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-mainloop-zephyr.h"
#include "sol-util.h"
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "gpio");

static atomic_val_t int_flag = 0;
static uint32_t irq_pins;

static struct sol_ptr_vector opened_pins = SOL_PTR_VECTOR_INIT;

/* Zephyr structure used as a "bank of gpio pins" */
static struct device *device;

struct sol_gpio {
    uint32_t pin;
    bool active_low;
    void (*cb)(void *data, struct sol_gpio *gpio, bool value);
    const void *cb_data;
};

static void
sol_gpio_interrupt_process(void *data)
{
    struct sol_gpio *g;
    uint32_t irq;
    uint16_t i;

    if (!atomic_cas(&int_flag, 1, 0))
        return;

    irq = irq_pins;
    irq_pins = 0;

    SOL_PTR_VECTOR_FOREACH_IDX (&opened_pins, g, i) {
        if (irq & (1U << g->pin)) {
            bool val = sol_gpio_read(g);
            g->cb((void *)g->cb_data, g, val);
        }
    }
}

/* Run in interrupt context */
static void
gpio_isr_cb(struct device *port, uint32_t pin)
{
    struct mainloop_event w = {
        .cb = sol_gpio_interrupt_process,
        .data = NULL
    };

    irq_pins |= 1U << pin;

    if (!atomic_cas(&int_flag, 0, 1))
        return;

    sol_mainloop_event_post(&w);
}

SOL_API struct sol_gpio *
sol_gpio_open_raw(uint32_t pin, const struct sol_gpio_config *config)
{
    struct sol_gpio *g, *gpio;
    int flags = 0;
    uint16_t i;

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (unlikely(config->api_version != SOL_GPIO_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open gpio that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_GPIO_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    if (opened_pins.base.len == 0) {
        if (!device) {
            device = device_get_binding("GPIO_0");
            SOL_NULL_CHECK(device, NULL);
        }
    } else {
        SOL_PTR_VECTOR_FOREACH_IDX (&opened_pins, g, i) {
            if (g->pin == pin) {
                SOL_WRN("GPIO pin %" PRIu32 " is already opened", pin);
                return NULL;
            }
        }
    }

    gpio = malloc(sizeof(struct sol_gpio));
    SOL_NULL_CHECK(gpio, NULL);

    gpio->pin = pin;
    gpio->active_low = config->active_low;

    if (config->dir == SOL_GPIO_DIR_IN) {
        if (config->in.trigger_mode == SOL_GPIO_EDGE_NONE) {
            flags = flags | GPIO_DIR_IN;
            if (gpio_pin_configure(device, gpio->pin, flags) < 0) {
                SOL_WRN("Couldn't configure gpio");
                goto error;
            }
        } else {
            flags = flags
                | GPIO_INT
                | (config->dir == config->active_low ? GPIO_INT_ACTIVE_LOW : GPIO_INT_ACTIVE_HIGH)
                | (config->in.trigger_mode == SOL_GPIO_EDGE_BOTH ? GPIO_INT_DOUBLE_EDGE : GPIO_INT_EDGE);

            gpio->cb = config->in.cb;
            gpio->cb_data = (void *)config->in.user_data;

            if (gpio_pin_configure(device, gpio->pin, flags) < 0) {
                SOL_WRN("Couldn't configure gpio");
                goto error;
            }

            if (gpio_set_callback(device, gpio_isr_cb) < 0) {
                SOL_WRN("Couldn't set callback to gpio");
                goto error;
            }

            if (gpio_pin_enable_callback(device, gpio->pin) < 0) {
                SOL_WRN("Couldn't set callback to gpio");
                goto error;
            }
        }
    } else {
        if (gpio_pin_configure(device, gpio->pin, GPIO_DIR_OUT) < 0) {
            SOL_WRN("Couldn't configure gpio");
            goto error;
        }

        sol_gpio_write(gpio, config->out.value);
    }

    if (sol_ptr_vector_append(&opened_pins, gpio) < 0) {
        SOL_WRN("Couldn't configure gpio");
        goto error;
    }

    return gpio;

error:
    free(gpio);
    return NULL;
}

SOL_API void
sol_gpio_close(struct sol_gpio *gpio)
{
    struct sol_gpio *g;
    uint16_t i;

    SOL_NULL_CHECK(device);
    SOL_NULL_CHECK(gpio);

    SOL_PTR_VECTOR_FOREACH_IDX (&opened_pins, g, i) {
        if (g->pin == gpio->pin) {
            if (gpio_pin_disable_callback(device, g->pin) < 0) {
                SOL_WRN("Couldn't disable gpio callback");
            }
            sol_ptr_vector_del(&opened_pins, i);
            break;
        }
    }

    free(gpio);
}

SOL_API bool
sol_gpio_write(struct sol_gpio *gpio, bool value)
{
    SOL_NULL_CHECK(device, false);
    SOL_NULL_CHECK(gpio, false);

    if (gpio_pin_write(device, gpio->pin, gpio->active_low ^ value) < 0) {
        SOL_WRN("Couldn't write to gpio pin:%" PRIu32, gpio->pin);
        return false;
    }

    return true;
}

SOL_API int
sol_gpio_read(struct sol_gpio *gpio)
{
    uint32_t value;

    SOL_NULL_CHECK(device, -EINVAL);
    SOL_NULL_CHECK(gpio, -EINVAL);

    if (gpio_pin_read(device, gpio->pin, &value) < 0) {
        SOL_WRN("Couldn't read gpio pin:%" PRIu32, gpio->pin);
        return -EINVAL;
    }

    return gpio->active_low ^ !!value;
}
