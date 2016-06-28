/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "soletta.h"
#include "sol-gpio.h"

struct sol_gpio *gpio = NULL;
struct sol_gpio_config config = { 0 };

static void
get_cmd(uint32_t pin)
{
    int ret;

    SOL_SET_API_VERSION(config.api_version = SOL_GPIO_CONFIG_API_VERSION; )
    config.dir = SOL_GPIO_DIR_IN;

    gpio = sol_gpio_open(pin, &config);
    SOL_NULL_CHECK_GOTO(gpio, get_error);

    ret = sol_gpio_read(gpio);
    if (ret < 0)
        fprintf(stderr, "%s\n", strerror(-ret));
    else
        fprintf(stdout, "value = %d\n", ret);

    sol_quit();
    return;

get_error:
    sol_quit_with_code(EXIT_FAILURE);
}

static void
monitor_cb(void *_data, struct sol_gpio *_gpio, bool value)
{
    fprintf(stdout, "value = %d\n", value);
}

static void
monitor_cmd(uint32_t pin)
{
    SOL_SET_API_VERSION(config.api_version = SOL_GPIO_CONFIG_API_VERSION);
    config.dir = SOL_GPIO_DIR_IN;
    config.in.trigger_mode = SOL_GPIO_EDGE_BOTH;
    config.in.poll_timeout = 100;
    config.in.cb = monitor_cb;

    gpio = sol_gpio_open(pin, &config);
    SOL_NULL_CHECK_GOTO(gpio, monitor_error);
    return;

monitor_error:
    sol_quit_with_code(EXIT_FAILURE);
}

static void
set_cmd(uint32_t pin, bool value)
{
    SOL_SET_API_VERSION(config.api_version = SOL_GPIO_CONFIG_API_VERSION);
    config.dir = SOL_GPIO_DIR_OUT;
    config.out.value = value;

    gpio = sol_gpio_open(pin, &config);
    SOL_NULL_CHECK_GOTO(gpio, set_error);
    sol_quit();
    return;

set_error:
    sol_quit_with_code(EXIT_FAILURE);
}

static void
usage(const char *program)
{
    fprintf(stdout,
        "Usage:\n"
        "   %s set [pin] [value]\n"
        "   %s get [pin]]\n"
        "   %s monitor [pin]\n",
        program, program, program);
}

static void
startup(void)
{
    char **argv = sol_argv();
    int argc = sol_argc();
    uint32_t pin;
    unsigned int value;

    if (argc < 3)
        goto err_usage;

    if (!sscanf(argv[2], "%" SCNu32, &pin))
        goto err_usage;

    if (!strcmp(argv[1], "get")) {
        get_cmd(pin);
        return;
    }

    if (!strcmp(argv[1], "set")) {
        if (argc < 4 || !sscanf(argv[3], "%u", &value))
            goto err_usage;

        set_cmd(pin, !!value);
        return;
    }

    if (!strcmp(argv[1], "monitor")) {
        monitor_cmd(pin);
        return;
    }

err_usage:
    usage(argv[0]);
    sol_quit_with_code(EXIT_FAILURE);
}

static void
shutdown(void)
{
    if (gpio)
        sol_gpio_close(gpio);
}

SOL_MAIN_DEFAULT(startup, shutdown);
