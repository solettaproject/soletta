/*
 * This file is part of the Soletta Project
 *
 * Copyright (c) 2015, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include <stdio.h>
#include <sys/stat.h>

#include "intel-common.h"
#include "sol-log-internal.h"
#include "sol-util.h"

SOL_LOG_INTERNAL_DECLARE(_intel_mux_log_domain, "intel-mux");

#define GPIO_DRIVE_PULLUP   0
#define GPIO_DRIVE_PULLDOWN 1
#define GPIO_DRIVE_STRONG   2
#define GPIO_DRIVE_HIZ      3

#define BASE "/sys/class/gpio"
#define MODE_PATH "/sys/kernel/debug/gpio_debug/gpio%d/current_pinmux"

struct gpio_ref {
    int pin;
    struct sol_gpio *gpio;
};

static struct sol_vector _in_use = SOL_VECTOR_INIT(struct gpio_ref);

static struct sol_gpio *
_get_gpio(int pin, enum sol_gpio_direction dir, bool val)
{
    uint16_t i;
    struct gpio_ref *ref;
    struct sol_gpio *gpio;
    struct sol_gpio_config gpio_config = { 0 };

    SOL_VECTOR_FOREACH_IDX (&_in_use, ref, i) {
        if (ref->pin == pin)
            return ref->gpio;
    }

    SOL_SET_API_VERSION(gpio_config.api_version = SOL_GPIO_CONFIG_API_VERSION; )
    gpio_config.dir = dir;
    gpio_config.out.value = val;

    gpio = sol_gpio_open_raw(pin, &gpio_config);
    if (gpio) {
        ref = sol_vector_append(&_in_use);
        if (!ref) {
            sol_gpio_close(gpio);
            return NULL;
        }

        ref->pin = pin;
        ref->gpio = gpio;
    }

    return gpio;
}

static int
_set_gpio(int pin, enum sol_gpio_direction dir, int drive, bool val)
{
    int len;
    struct stat st;
    char path[PATH_MAX];
    struct sol_gpio *gpio;
    const char *drive_str;

    gpio = _get_gpio(pin, dir, val);
    if (!gpio) {
        SOL_WRN("Wasn't possible to open gpio=%d", pin);
        return -EINVAL;
    }

    sol_gpio_write(gpio, val);

    // Drive:
    // This is not standard interface in upstream Linux, so the
    // Linux implementation of sol-gpio doesn't handle it, thus the need
    // to set it here manually
    //
    // Not all gpio's will have this so its no problem to fail the if bellow.
    // Also, many of the gpio's that have this interface available, won't let
    // you used it and we have no way of differentiate those. So its ok to fail
    // to write
    len = snprintf(path, sizeof(path), BASE "/gpio%d/drive", pin);
    if (len < 0 || len > PATH_MAX || stat(path, &st) == -1)
        goto end;

    switch (drive) {
    case GPIO_DRIVE_PULLUP:
        drive_str = "pullup";
        break;
    case GPIO_DRIVE_PULLDOWN:
        drive_str = "pulldown";
        break;
    case GPIO_DRIVE_HIZ:
        drive_str = "hiz";
        break;
    default:
        drive_str = "strong";
    }

    sol_util_write_file(path, "%s", drive_str);
end:
    return 0;
}

static int
_set_mode(int pin, int mode)
{
    int len;
    struct stat st;
    char path[PATH_MAX];
    char mode_str[] = "mode0";

    len = snprintf(path, sizeof(path), MODE_PATH, pin);
    if (len < 0 || len > PATH_MAX || stat(path, &st) == -1)
        return -EINVAL;

    mode_str[4] = '0' + (mode - PIN_MODE_0);

    return sol_util_write_file(path, "%s", mode_str);
}

static int
_apply_mux_desc(struct mux_description *desc, unsigned int mode)
{
    int ret;

    while (desc->mode) {
        if (desc->mode & mode) {
            if (desc->val == PIN_NONE) {
                ret = _set_gpio(desc->gpio_pin, SOL_GPIO_DIR_IN, GPIO_DRIVE_HIZ, false);
            } else if (desc->val > PIN_NONE) {
                ret = _set_mode(desc->gpio_pin, desc->val);
            } else {
                ret = _set_gpio(desc->gpio_pin, SOL_GPIO_DIR_OUT, GPIO_DRIVE_STRONG, desc->val);
            }
            if (ret < 0)
                return ret;
        }
        desc++;
    }

    return 0;
}

void
mux_shutdown(void)
{
    uint16_t i;
    struct gpio_ref *ref;

    SOL_VECTOR_FOREACH_IDX (&_in_use, ref, i) {
        sol_gpio_close(ref->gpio);
    }

    sol_vector_clear(&_in_use);
}

#define SET_INT_ARG(args, i, v) \
    do { \
        i = va_arg(args, int *); \
        if (i) *i = v; \
    } while (0)

int
mux_pin_map(const struct mux_pin_map *map, const char *label, const enum sol_io_protocol prot,
    va_list args)
{
    int *i;

    if (!map || !label || *label == '\0')
        return -EINVAL;

    while (map->label) {
        if (streq(map->label, label)) {

            if (!(map->cap & prot))
                break;

            switch (prot) {
            case SOL_IO_AIO:
                SET_INT_ARG(args, i, map->aio.device);
                SET_INT_ARG(args, i, map->aio.pin);
                break;
            case SOL_IO_GPIO:
                SET_INT_ARG(args, i, map->gpio);
                break;
            case SOL_IO_PWM:
                SET_INT_ARG(args, i, map->pwm.device);
                SET_INT_ARG(args, i, map->pwm.pin);
                break;
            default:
                break;
            }
            return 0;
        }
        map++;
    }

    return -EINVAL;
}
#undef SET_INT_ARG

int
mux_set_aio(const int device, const int pin, const struct mux_controller *ctl_list, const int s)
{
    const struct mux_controller *ctl;

    if (device < 0) {
        SOL_WRN("Invalid AIO device: %d", device);
        return -EINVAL;
    }

    if (pin < 0) {
        SOL_WRN("Invalid AIO pin: %d", pin);
        return -EINVAL;
    }

    if (device >= s)
        return 0;

    ctl = ctl_list + device;
    if (pin >= (int)ctl->len || !ctl->recipe[pin])
        return 0;

    return _apply_mux_desc(ctl->recipe[pin], MODE_ANALOG);
}

int
mux_set_gpio(const int pin, const enum sol_gpio_direction dir,
    struct mux_description **const desc_list, const int s)
{
    if (pin < 0) {
        SOL_WRN("Invalid GPIO pin: %d", pin);
        return -EINVAL;
    }

    if (pin >= s || !desc_list[pin])
        return 0;

    return _apply_mux_desc(desc_list[pin], dir == SOL_GPIO_DIR_OUT ?
        MODE_GPIO_OUTPUT : MODE_GPIO_INPUT_PULLUP);
}

int
mux_set_i2c(const uint8_t bus, struct mux_description * (*const desc_list)[2], const unsigned int s)
{
    int ret;

    if (bus >= s || (!desc_list[bus][0] && !desc_list[bus][1]))
        return 0;

    ret = _apply_mux_desc(desc_list[bus][0], MODE_I2C);

    if (ret < 0)
        return ret;

    return _apply_mux_desc(desc_list[bus][1], MODE_I2C);
}

int
mux_set_pwm(const int device, const int channel, const struct mux_controller *ctl_list, const int s)
{
    const struct mux_controller *ctl;

    if (device < 0) {
        SOL_WRN("Invalid PWM device: %d", device);
        return -EINVAL;
    }

    if (channel < 0) {
        SOL_WRN("Invalid PWM channel: %d", channel);
        return -EINVAL;
    }

    if (device >= s)
        return 0;

    ctl = ctl_list + device;
    if (channel >= (int)ctl->len || !ctl->recipe[channel])
        return 0;

    return _apply_mux_desc(ctl->recipe[channel], MODE_PWM);
}
