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

#include <contiki.h>
#include <dev/button-sensor.h>
#include <dev/leds.h>
#include <lib/sensors.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-gpio.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-event-handler-contiki.h"

static SOL_LOG_INTERNAL_DECLARE(_log_domain, "gpio");

struct sol_gpio {
    uint32_t pin;
    struct sensors_sensor *button_sensor;
    bool active_low;
    struct {
        void (*cb)(void *data, struct sol_gpio *gpio, bool value);
        void *data;
    } irq;
};

static void
event_handler_cb(void *user_data, process_event_t ev, process_data_t ev_data)
{
    struct sol_gpio *gpio = user_data;
    bool val;

    val = sol_gpio_read(gpio);
    gpio->irq.cb(gpio->irq.data, gpio, val);
}

struct sol_gpio *
sol_gpio_open_raw(uint32_t pin, const struct sol_gpio_config *config)
{
    struct sol_gpio *gpio;
    struct sensors_sensor *found = NULL;

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(config->api_version != SOL_GPIO_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open gpio that has unsupported version '%" PRIu16 "', "
            "expected version is '%" PRIu16 "'",
            config->api_version, SOL_GPIO_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    process_start(&sensors_process, NULL);

    if (config->drive_mode != SOL_GPIO_DRIVE_NONE) {
        SOL_ERR("Unable to set pull resistor on pin=%" PRIu32, pin);
        return NULL;
    }

    if (config->dir == SOL_GPIO_DIR_IN) {
        const struct sensors_sensor *sensor = sensors_first();
        uint32_t i = 0;

        while (sensor) {
            if (strcmp(sensor->type, BUTTON_SENSOR)) {
                sensor = sensors_next(sensor);
                continue;
            }
            if (i == pin) {
                found = (struct sensors_sensor *)sensor;
                break;
            }
            sensor = sensors_next(sensor);
            i++;
        }

        if (!found) {
            SOL_ERR("GPIO pin=%" PRIu32 " not found.", pin);
            return NULL;
        }
    } else {
        if (pin > 7) {
            SOL_ERR("GPIO pin=%" PRIu32 " not found.", pin);
            return NULL;
        }
    }

    gpio = malloc(sizeof(struct sol_gpio));
    SOL_NULL_CHECK(gpio, NULL);

    gpio->pin = pin;
    gpio->button_sensor = found;
    gpio->active_low = config->active_low;

    if (config->dir == SOL_GPIO_DIR_IN) {
        gpio->irq.cb = config->in.cb;
        gpio->irq.data = (void *)config->in.user_data;
        if (config->in.cb)
            sol_mainloop_contiki_event_handler_add(&sensors_event, found,
                event_handler_cb, gpio);
    } else
        sol_gpio_write(gpio, config->out.value);

    return gpio;
}

void
sol_gpio_close(struct sol_gpio *gpio)
{
    SOL_NULL_CHECK(gpio);
    if (gpio->irq.cb)
        sol_mainloop_contiki_event_handler_del(&sensors_event,
            gpio->button_sensor, event_handler_cb, gpio);
    free(gpio);
}

bool
sol_gpio_write(struct sol_gpio *gpio, bool value)
{
    SOL_NULL_CHECK(gpio, false);

    if (gpio->button_sensor)
        return false;

    value = gpio->active_low ^ value;

    if (value)
        leds_set(leds_get() | (1 << gpio->pin));
    else
        leds_set(leds_get() & (~(1 << gpio->pin)));
    return true;
}

int
sol_gpio_read(struct sol_gpio *gpio)
{
    SOL_NULL_CHECK(gpio, -EINVAL);

    if (gpio->button_sensor)
        return gpio->active_low ^ !!gpio->button_sensor->value(0);

    return gpio->active_low ^ !!(leds_get() & (1 << gpio->pin));
}
