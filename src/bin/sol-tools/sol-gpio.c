/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sol-gpio.h"
#include "sol-log.h"
#include "sol-mainloop.h"

struct sol_gpio *gpio = NULL;
struct sol_gpio_config config = {0};

static void
sigint_handler(int signo)
{
    if (signo == SIGINT)
        sol_quit();
}

static void
get_cmd(uint32_t pin)
{
    int ret;

    SOL_SET_API_VERSION(config.api_version = SOL_GPIO_CONFIG_API_VERSION; )
    config.dir = SOL_GPIO_DIR_IN;

    gpio = sol_gpio_open(pin, &config);
    SOL_NULL_CHECK(gpio);

    ret = sol_gpio_read(gpio);
    if (ret < 0)
        fprintf(stderr, "%s\n", strerror(-ret));
    else
        fprintf(stdout, "value = %d\n", ret);

    sol_gpio_close(gpio);
}

static void
monitor_cb(void *_data, struct sol_gpio *_gpio, bool value)
{
    fprintf(stdout, "value = %d\n", value);
}

static void
monitor_cmd(uint32_t pin)
{
    SOL_SET_API_VERSION(config.api_version = SOL_GPIO_CONFIG_API_VERSION; )
    config.dir = SOL_GPIO_DIR_IN;
    config.in.trigger_mode = SOL_GPIO_EDGE_BOTH;
    config.in.poll_timeout = 100;
    config.in.cb = monitor_cb;

    gpio = sol_gpio_open(pin, &config);
    SOL_NULL_CHECK(gpio);

    signal(SIGINT, sigint_handler);
    sol_run();
    sol_gpio_close(gpio);
}

static void
set_cmd(uint32_t pin, bool value)
{
    SOL_SET_API_VERSION(config.api_version = SOL_GPIO_CONFIG_API_VERSION; )
    config.dir = SOL_GPIO_DIR_OUT;
    config.out.value = value;

    gpio = sol_gpio_open(pin, &config);
    SOL_NULL_CHECK(gpio);

    sol_gpio_close(gpio);
}

static void
usage(const char *program)
{
     fprintf(stdout,
        "Usage: %s [[get | monitor] [pin] | set [pin] [value]]\n",
        program);
}

int
main(int argc, char *argv[])
{
    uint32_t pin;
    unsigned int value;

    if (SOL_UNLIKELY(sol_init() < 0)) {
        fprintf(stderr, "Can't initialize Soletta.\n");
        return EXIT_FAILURE;
    }

    if (argc < 3)
        goto err_usage;

    if (!sscanf(argv[2], "%"SCNu32, &pin))
        goto err_usage;

    if (!strcmp(argv[1], "get")) {
        get_cmd(pin);
    } else if (!strcmp(argv[1], "monitor")) {
        monitor_cmd(pin);
    } else if (!strcmp(argv[1], "set")) {
        if (argc < 4 || !sscanf(argv[3], "%u", &value))
            goto err_usage;

        set_cmd(pin, !!value);
    } else
        goto err_usage;

    sol_shutdown();
    return EXIT_SUCCESS;

err_usage:
    usage(argv[0]);
    sol_shutdown();
    return EXIT_FAILURE;
}
