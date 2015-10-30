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

/*
 * This is a regular Soletta application, there are no linux-micro
 * specific bits in it, just a timer, an optional gpio writer and some
 * monitors for platform and service states. The purpose is to show
 * that it can be considered a /init (PID1) binary if Soletta is
 * compiled with linux-micro platform and if it runs as PID1, then
 * /proc, /sys, /dev are all mounted as well as other bits of the
 * system are configured.
 */

#include <ctype.h>
#include <stdio.h>

#include "sol-mainloop.h"
#include "sol-platform.h"
#include "sol-gpio.h"
#include "sol-util.h"

static int
parse_cmdline_pin(void)
{
    char **argv = sol_argv();
    int argc = sol_argc();
    int i;

    for (i = 1; i < argc; i++) {
        if (streqn(argv[i], "led-pin=", sizeof("led-pin=") - 1)) {
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
