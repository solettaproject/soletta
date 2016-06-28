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

#include <stdlib.h>
#include <string.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "gpio");

#include "sol-gpio.h"
#include "sol-str-table.h"
#include "sol-util.h"

#ifdef USE_PIN_MUX
#include "sol-pin-mux.h"
#endif

static void
_log_init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
}

SOL_API struct sol_gpio *
sol_gpio_open_by_label(const char *label, const struct sol_gpio_config *config)
{
    uint32_t pin;

    _log_init();

#ifdef USE_PIN_MUX
    if (!sol_pin_mux_map(label, SOL_IO_GPIO, &pin))
        return sol_gpio_open(pin, config);

    SOL_WRN("Label '%s' couldn't be mapped or can't be used as GPIO", label);
#else
    SOL_INF("Pin Multiplexer support is necessary to open a 'board pin'.");
    (void)pin;
#endif

    return NULL;
}

SOL_API struct sol_gpio *
sol_gpio_open(uint32_t pin, const struct sol_gpio_config *config)
{
    struct sol_gpio *gpio;

    _log_init();

    SOL_NULL_CHECK(config, NULL);

    gpio = sol_gpio_open_raw(pin, config);
#ifdef USE_PIN_MUX
    if (gpio && sol_pin_mux_setup_gpio(pin, config)) {
        SOL_ERR("Pin Multiplexer Recipe for gpio=%d found, but couldn't be applied.", pin);
        sol_gpio_close(gpio);
        gpio = NULL;
    }
#endif

    return gpio;
}

SOL_API enum sol_gpio_direction
sol_gpio_direction_from_str(const char *direction)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("out", SOL_GPIO_DIR_OUT),
        SOL_STR_TABLE_ITEM("in", SOL_GPIO_DIR_IN),
        { }
    };

    SOL_NULL_CHECK(direction, SOL_GPIO_DIR_OUT);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(direction), SOL_GPIO_DIR_OUT);
}

SOL_API const char *
sol_gpio_direction_to_str(enum sol_gpio_direction direction)
{
    static const char *direction_names[] = {
        [SOL_GPIO_DIR_OUT] = "out",
        [SOL_GPIO_DIR_IN] = "in"
    };

    if (direction < sol_util_array_size(direction_names))
        return direction_names[direction];

    return NULL;
}

SOL_API enum sol_gpio_edge
sol_gpio_edge_from_str(const char *edge)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("none", SOL_GPIO_EDGE_NONE),
        SOL_STR_TABLE_ITEM("rising", SOL_GPIO_EDGE_RISING),
        SOL_STR_TABLE_ITEM("falling", SOL_GPIO_EDGE_FALLING),
        SOL_STR_TABLE_ITEM("any", SOL_GPIO_EDGE_BOTH),
        { }
    };

    SOL_NULL_CHECK(edge, SOL_GPIO_EDGE_NONE);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(edge), SOL_GPIO_EDGE_NONE);
}

SOL_API const char *
sol_gpio_edge_to_str(enum sol_gpio_edge edge)
{
    static const char *edge_names[] = {
        [SOL_GPIO_EDGE_NONE] = "none",
        [SOL_GPIO_EDGE_RISING] = "rising",
        [SOL_GPIO_EDGE_FALLING] = "falling",
        [SOL_GPIO_EDGE_BOTH] = "any"
    };

    if (edge < sol_util_array_size(edge_names))
        return edge_names[edge];

    return NULL;
}

SOL_API enum sol_gpio_drive
sol_gpio_drive_from_str(const char *drive)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("none", SOL_GPIO_DRIVE_NONE),
        SOL_STR_TABLE_ITEM("up", SOL_GPIO_DRIVE_PULL_UP),
        SOL_STR_TABLE_ITEM("down", SOL_GPIO_DRIVE_PULL_DOWN),
        { }
    };

    SOL_NULL_CHECK(drive, SOL_GPIO_DRIVE_NONE);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(drive), SOL_GPIO_DRIVE_NONE);
}

SOL_API const char *
sol_gpio_drive_to_str(enum sol_gpio_drive drive)
{
    static const char *drive_names[] = {
        [SOL_GPIO_DRIVE_NONE] = "none",
        [SOL_GPIO_DRIVE_PULL_UP] = "up",
        [SOL_GPIO_DRIVE_PULL_DOWN] = "down"
    };

    if (drive < sol_util_array_size(drive_names))
        return drive_names[drive];

    return NULL;
}
