/*
 * This file is part of the Soletta™ Project
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

#include <stdio.h>
#include <sys/stat.h>

#define SOL_LOG_DOMAIN &_intel_mux_log_domain

#include "intel-common.h"
#include "sol-log-internal.h"
#include "sol-util-file.h"
#include "sol-util-internal.h"

SOL_LOG_INTERNAL_DECLARE(_intel_mux_log_domain, "intel-mux");

#define GPIO_DRIVE_PULLUP   0
#define GPIO_DRIVE_PULLDOWN 1
#define GPIO_DRIVE_STRONG   2
#define GPIO_DRIVE_HIZ      3

#define BASE "/sys/class/gpio"
#define MODE_PATH "/sys/kernel/debug/gpio_debug/gpio%u/current_pinmux"

struct gpio_ref {
    uint32_t pin;
    struct sol_gpio *gpio;
};

static struct sol_vector _in_use = SOL_VECTOR_INIT(struct gpio_ref);

static struct sol_gpio *
_get_gpio(uint32_t pin, enum sol_gpio_direction dir, bool val)
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
_set_gpio(uint32_t pin, enum sol_gpio_direction dir, int drive, bool val)
{
    int len;
    struct stat st;
    char path[PATH_MAX];
    struct sol_gpio *gpio;
    const char *drive_str;

    gpio = _get_gpio(pin, dir, val);
    SOL_NULL_CHECK_MSG(gpio, -EINVAL, "Wasn't possible to open gpio=%d", pin);

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
    if (len < 0 || len >= PATH_MAX || stat(path, &st) == -1)
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
_set_mode(uint32_t pin, int mode)
{
    int len;
    struct stat st;
    char path[PATH_MAX];
    char mode_str[] = "mode0";

    len = snprintf(path, sizeof(path), MODE_PATH, pin);
    if (len < 0 || len >= PATH_MAX || stat(path, &st) == -1)
        return -EINVAL;

    mode_str[4] = '0' + (mode - PIN_MODE_0);

    return sol_util_write_file(path, "%s", mode_str);
}

int
apply_mux_desc(const struct mux_description *desc, unsigned int mode)
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

#define SET_UINT_ARG(args, u, v) \
    do { \
        u = va_arg(args, uint32_t *); \
        if (u) *u = v; \
    } while (0)

int
mux_pin_map(const struct mux_pin_map *map, const char *label, const enum sol_io_protocol prot,
    va_list args)
{
    int *i;
    uint32_t *u;

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
                SET_UINT_ARG(args, u, map->gpio);
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
mux_set_aio(int device, int pin, const struct mux_controller *const ctl_list, int s)
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

    return apply_mux_desc(ctl->recipe[pin], MODE_ANALOG);
}

int
mux_set_gpio(uint32_t pin, const struct sol_gpio_config *config,
    const struct mux_description *const *desc_list, uint32_t s)
{
    unsigned int mode = MODE_GPIO_OUTPUT;

    if (pin >= s || !desc_list[pin])
        return 0;

    if (config->dir == SOL_GPIO_DIR_IN) {
        if (config->drive_mode == SOL_GPIO_DRIVE_NONE)
            mode = MODE_GPIO_INPUT_HIZ;
        else if (config->drive_mode == SOL_GPIO_DRIVE_PULL_UP)
            mode = MODE_GPIO_INPUT_PULLUP;
        else if (config->drive_mode == SOL_GPIO_DRIVE_PULL_DOWN)
            mode = MODE_GPIO_INPUT_PULLDOWN;
    }

    return apply_mux_desc(desc_list[pin], mode);
}

int
mux_set_i2c(uint8_t bus, const struct mux_description *const (*desc_list)[2], unsigned int s)
{
    int ret;

    if (bus >= s || (!desc_list[bus][0] && !desc_list[bus][1]))
        return 0;

    ret = apply_mux_desc(desc_list[bus][0], MODE_I2C);

    if (ret < 0)
        return ret;

    return apply_mux_desc(desc_list[bus][1], MODE_I2C);
}

int
mux_set_pwm(int device, int channel, const struct mux_controller *const ctl_list, int s)
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

    return apply_mux_desc(ctl->recipe[channel], MODE_PWM);
}
