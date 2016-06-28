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

/* Zephyr includes */
#include "atomic.h"
#include "gpio.h"
#include "misc/util.h"

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-gpio.h"
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-mainloop-zephyr.h"
#include "sol-util.h"
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "gpio");

struct sol_gpio_port {
    struct device *device;
    struct gpio_callback cb;
    struct sol_ptr_vector opened_pins;
    uint32_t irq_pins;
    atomic_val_t int_flag;
};

struct sol_gpio {
    uint32_t pin;
    struct sol_gpio_port *port;
    void (*cb)(void *data, struct sol_gpio *gpio, bool value);
    const void *cb_data;
    bool active_low;
};

static void
sol_gpio_interrupt_process(void *data)
{
    struct sol_gpio_port *port = data;
    struct sol_gpio *g;
    uint32_t irq;
    uint16_t i;

    if (!atomic_cas(&port->int_flag, 1, 0))
        return;

    irq = port->irq_pins;
    port->irq_pins = 0;

    SOL_PTR_VECTOR_FOREACH_IDX (&port->opened_pins, g, i) {
        if (irq & BIT(g->pin)) {
            bool val = sol_gpio_read(g);
            g->cb((void *)g->cb_data, g, val);
        }
    }
}

/* Run in interrupt context */
static void
gpio_isr_cb(struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    struct sol_gpio_port *port = CONTAINER_OF(cb, struct sol_gpio_port, cb);
    struct mainloop_event w = {
        .cb = sol_gpio_interrupt_process,
        .data = port
    };

    port->irq_pins |= pins;

    if (!atomic_cas(&port->int_flag, 0, 1))
        return;

    sol_mainloop_event_post(&w);
}

static struct sol_gpio_port *
gpio_get_port(uint32_t pin)
{
    /* We only support a single port for the time being */
    static struct sol_gpio_port port = {
        .opened_pins = SOL_PTR_VECTOR_INIT
    };

    if (!port.device) {
        port.device = device_get_binding("GPIO_0");
        SOL_NULL_CHECK(port.device, NULL);
        gpio_init_callback(&port.cb, gpio_isr_cb, 0);
        /* For simplicity, add the callback here. It won't be called if
         * the pin mask is 0 */
        gpio_add_callback(port.device, &port.cb);
    }

    return &port;
}

SOL_API struct sol_gpio *
sol_gpio_open_raw(uint32_t pin, const struct sol_gpio_config *config)
{
    struct sol_gpio_port *port;
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

    port = gpio_get_port(pin);
    SOL_NULL_CHECK(port, NULL);

    SOL_PTR_VECTOR_FOREACH_IDX (&port->opened_pins, g, i) {
        if (g->pin == pin) {
            SOL_WRN("GPIO pin %" PRIu32 " is already opened", pin);
            return NULL;
        }
    }

    gpio = malloc(sizeof(struct sol_gpio));
    SOL_NULL_CHECK(gpio, NULL);

    gpio->pin = pin;
    gpio->port = port;
    gpio->active_low = config->active_low;

    if (config->dir == SOL_GPIO_DIR_IN) {
        if (config->in.trigger_mode == SOL_GPIO_EDGE_NONE) {
            flags = flags | GPIO_DIR_IN;
            if (gpio_pin_configure(port->device, gpio->pin, flags) < 0) {
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

            if (gpio_pin_configure(port->device, gpio->pin, flags) < 0) {
                SOL_WRN("Couldn't configure gpio");
                goto error;
            }

            if (gpio_pin_enable_callback(port->device, gpio->pin) < 0) {
                SOL_WRN("Couldn't set callback to gpio");
                goto error;
            }

            port->cb.pin_mask |= BIT(pin);
        }
    } else {
        if (gpio_pin_configure(port->device, gpio->pin, GPIO_DIR_OUT) < 0) {
            SOL_WRN("Couldn't configure gpio");
            goto error;
        }

        sol_gpio_write(gpio, config->out.value);
    }

    if (sol_ptr_vector_append(&port->opened_pins, gpio) < 0) {
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
    struct sol_gpio_port *port;

    SOL_NULL_CHECK(gpio);

    port = gpio->port;
    port->cb.pin_mask &= ~BIT(gpio->pin);
    gpio_pin_disable_callback(port->device, gpio->pin);
    sol_ptr_vector_remove(&port->opened_pins, gpio);

    free(gpio);
}

SOL_API bool
sol_gpio_write(struct sol_gpio *gpio, bool value)
{
    SOL_NULL_CHECK(gpio, false);

    if (gpio_pin_write(gpio->port->device, gpio->pin, gpio->active_low ^ value) < 0) {
        SOL_WRN("Couldn't write to gpio pin:%" PRIu32, gpio->pin);
        return false;
    }

    return true;
}

SOL_API int
sol_gpio_read(struct sol_gpio *gpio)
{
    uint32_t value;

    SOL_NULL_CHECK(gpio, -EINVAL);

    if (gpio_pin_read(gpio->port->device, gpio->pin, &value) < 0) {
        SOL_WRN("Couldn't read gpio pin:%" PRIu32, gpio->pin);
        return -EINVAL;
    }

    return gpio->active_low ^ !!value;
}
