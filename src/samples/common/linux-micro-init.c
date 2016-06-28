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

/*
 * This is a regular Soletta application, there are no linux-micro
 * specific bits in it, just a timer, an optional gpio writer and some
 * monitors for platform and service states. The purpose is to show
 * that it can be considered a /init (PID1) binary if Soletta is
 * compiled with linux-micro platform and if it runs as PID1, then
 * /proc, /sys, /dev are all mounted as well as other bits of the
 * system are configured.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "soletta.h"
#include "sol-platform.h"
#include "sol-gpio.h"
#include "sol-util.h"
#include "sol-util-file.h"

static int
parse_cmdline_pin(void)
{
    char **argv = sol_argv();
    int argc = sol_argc();
    int i;

    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "led-pin=", sizeof("led-pin=") - 1)) {
            const char *value = argv[i] + sizeof("led-pin=") - 1;
            int pin = -1;
            if (sscanf(value, "%d", &pin) == 1) {
                if (pin > -1)
                    return pin;
            }
        }
    }

    return -1;
}

static int
parse_kcmdline_pin_entry(const char *start, size_t len)
{
    const char prefix[] = "soletta.led-pin=";
    const size_t prefixlen = sizeof("soletta.led-pin=") - 1;
    char *tmp;
    int pin = -1;

    if (len <= prefixlen)
        return -1;

    if (memcmp(start, prefix, prefixlen) != 0)
        return -1;

    start += prefixlen;
    len -= prefixlen;
    tmp = strndupa(start, len);
    if (sscanf(tmp, "%d", &pin) == 1) {
        if (pin > -1)
            return pin;
    }

    return -1;
}

static int
parse_kcmdline_pin(void)
{
    char buf[4096] = {};
    const char *p, *start, *end;
    int err, pin = -1;

    err = sol_util_read_file("/proc/cmdline", "%4095[^\n]", buf);
    if (err < 1)
        return err;

    start = buf;
    end = start + strlen(buf);
    for (p = start; pin < 0 && p < end; p++) {
        if (isblank(*p) && start < p) {
            pin = parse_kcmdline_pin_entry(start, p - start);
            start = p + 1;
        }
    }
    if (pin < 0 && start < end)
        pin = parse_kcmdline_pin_entry(start, end - start);
    return pin;
}

static struct sol_timeout *timeout;
static struct sol_gpio *gpio;
static bool gpio_state = false;

static const char *services[] = {
    "console",
    "hostname",
    "network-up",
    "sysctl",
    "watchdog",
    NULL
};

static bool
on_timeout(void *data)
{
    puts("soletta is ticking!");
    if (gpio) {
        gpio_state = !gpio_state;
        sol_gpio_write(gpio, gpio_state);
    }
    return true;
}

static void
on_platform_state_change(void *data, enum sol_platform_state state)
{
    printf("platform state changed to: %d\n", state);
}

static void
on_service_change(void *data, const char *service, enum sol_platform_service_state state)
{
    printf("service %s state changed to: %d\n", service, state);
}

static void
startup(void)
{
    const char **itr;
    int pin = parse_cmdline_pin();

    if (pin < 0)
        pin = parse_kcmdline_pin();

    if (pin >= 0) {
        struct sol_gpio_config cfg = {
            SOL_SET_API_VERSION(.api_version = SOL_GPIO_CONFIG_API_VERSION, )
            .dir = SOL_GPIO_DIR_OUT,
        };
        gpio = sol_gpio_open(pin, &cfg);
        if (gpio)
            printf("blinking led on gpio pin=%d\n", pin);
        else
            fprintf(stderr, "failed to open gpio pin=%d for writing.\n", pin);
    }

    timeout = sol_timeout_add(1000, on_timeout, NULL);

    sol_platform_add_state_monitor(on_platform_state_change, NULL);
    printf("platform state: %d\n", sol_platform_get_state());
    for (itr = services; *itr != NULL; itr++) {
        sol_platform_add_service_monitor(on_service_change, *itr, NULL);
        printf("service %s state: %d\n",
            *itr, sol_platform_get_service_state(*itr));
    }
}

static void
shutdown(void)
{
    const char **itr;

    if (gpio)
        sol_gpio_close(gpio);
    sol_timeout_del(timeout);

    for (itr = services; *itr != NULL; itr++)
        sol_platform_del_service_monitor(on_service_change, *itr, NULL);
    sol_platform_del_state_monitor(on_platform_state_change, NULL);
}

SOL_MAIN_DEFAULT(startup, shutdown);
