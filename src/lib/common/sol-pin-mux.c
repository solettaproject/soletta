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

#include <errno.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "sol-log-internal.h"
#include "sol-util.h"
#include "sol-pin-mux.h"
#include "sol-pin-mux-impl.h"
#include "sol-pin-mux-builtins-gen.h"
#include "sol-platform.h"

#define GPIO_DRIVE_PULLUP   0
#define GPIO_DRIVE_PULLDOWN 1
#define GPIO_DRIVE_STRONG   2
#define GPIO_DRIVE_HIZ      3

#define BASE "/sys/class/gpio"

SOL_LOG_INTERNAL_DECLARE(_sol_pin_mux_log_domain, "pin-mux");

static const struct sol_pin_mux *mux = NULL;

int sol_pin_mux_init(void);
void sol_pin_mux_shutdown(void);

#ifdef ENABLE_DYNAMIC_MODULES
static void *dl_handle = NULL;
#endif

static bool
_load_mux(const char *name)
{
#ifdef ENABLE_DYNAMIC_MODULES
    int r;
    void *handle;
    char path[PATH_MAX], install_rootdir[PATH_MAX] = { NULL };
    const struct sol_pin_mux *p_sym;

    r = sol_util_get_rootdir(install_rootdir, sizeof(install_rootdir));
    SOL_INT_CHECK(r, >= (int)sizeof(install_rootdir), false);

    r = snprintf(path, sizeof(path), "%s%s/%s.so", install_rootdir, PINMUXDIR, name);
    SOL_INT_CHECK(r, >= (int)sizeof(path), false);
    SOL_INT_CHECK(r, < 0, false);

    handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
    if (!handle) {
        SOL_INF("Could not load platform pin multiplexer '%s': %s", path, dlerror());
        return true; // Not find a mux isn't necessarily an error, so we are returning true here
    }

    p_sym = dlsym(handle, "SOL_PIN_MUX");
    if (!p_sym) {
        SOL_WRN("Could not find symbol SOL_PIN_MUX in module '%s': %s", path, dlerror());
        goto error;
    }

    if (p_sym->api_version != SOL_PIN_MUX_API_VERSION) {
        SOL_WRN("Mux '%s' has incorrect api_version: %lu expected %lu", path, p_sym->api_version,
            SOL_PIN_MUX_API_VERSION);
        goto error;
    }

    if (dl_handle)
        dlclose(dl_handle);

    mux = p_sym;
    dl_handle = handle;

    SOL_INF("Loaded pin multiplexer '%s' from '%s'", mux->plat_name, path);
    return true;

error:
    dlclose(handle);
#endif
    return false;
}

static bool
_find_mux(const char *name)
{
#if (SOL_PIN_MUX_BUILTIN_COUNT > 0)
    unsigned int i;
    const struct sol_pin_mux *_mux;

    for (i = 0; i < SOL_PIN_MUX_BUILTIN_COUNT; i++) {
        _mux = SOL_PIN_MUX_BUILTINS_ALL[i];
        if (streq(_mux->plat_name, name)) {
            mux = _mux;
            SOL_INF("Loaded built-in pin multiplexer '%s'", mux->plat_name);
            return true;
        }
    }
#endif
    return false;
}

SOL_API bool
sol_pin_mux_select_mux(const char *_board)
{
    if (!_board || _board[0] == '\0')
        return true;

    if (mux && streq(_board, mux->plat_name))
        return true;

    // it only returns error if found a mux but failed to setup it.
    return _find_mux(_board) ? true : _load_mux(_board);
}

int
sol_pin_mux_init(void)
{
    sol_log_domain_init_level(SOL_LOG_DOMAIN);

    if (!sol_pin_mux_select_mux(sol_platform_get_name())) {
        SOL_WRN("Pin Multiplexer found, but failed to be loaded.");
        return -1;
    }

    return 0;
}

void
sol_pin_mux_shutdown(void)
{
    mux = NULL;
#ifdef ENABLE_DYNAMIC_MODULES
    if (dl_handle)
        dlclose(dl_handle);
    dl_handle = NULL;
#endif
}

static int
_set_gpio(int pin, enum sol_gpio_direction dir, int drive, bool val)
{
    int ret = 0;
    int len;
    struct stat st;
    char path[PATH_MAX];
    struct sol_gpio *gpio;
    const char *drive_str;
    struct sol_gpio_config gpio_config = { 0 };

    gpio_config.dir = dir;
    gpio_config.out.value = val;

    gpio = sol_gpio_open_raw(pin, &gpio_config);
    if (!gpio)
        return -EINVAL;

    // Drive:
    // This is not standard interface in upstream Linux, so the
    // Linux implementation of sol-gpio doesn't handle it, thus the need
    // to set it here manually
    //
    // Not all platforms will have this (this will move in the future)
    // so its no problem to fail the if bellow
    len = snprintf(path, sizeof(path), BASE "/gpio%d/drive", pin);
    if (len < 0 || len > PATH_MAX || stat(path, &st) == -1)
        goto err;

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

    ret = sol_util_write_file(path, "%s", drive_str);

err:
    sol_gpio_close(gpio);
    return ret;
}

static int
_apply_mux_desc(struct sol_pin_mux_description *desc, unsigned int mode)
{
    int ret;

    while (desc->mode) {
        if (desc->mode & mode) {
            if (desc->val == SOL_PIN_NONE) {
                ret = _set_gpio(desc->gpio_pin, SOL_GPIO_DIR_IN, GPIO_DRIVE_HIZ, false);
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

SOL_API int
sol_pin_mux_setup_aio(const int device, const int pin)
{
    struct sol_pin_mux_controller *ctl;

    if (device < 0) {
        SOL_WRN("Invalid AIO device: %d", device);
        return -EINVAL;
    }

    if (pin < 0) {
        SOL_WRN("Invalid AIO pin: %d", pin);
        return -EINVAL;
    }

    if (!mux || device >= (int)mux->aio.len)
        return 0;

    ctl = &mux->aio.controllers[device];
    if (pin >= (int)ctl->len || !ctl->recipe[pin])
        return 0;

    return _apply_mux_desc(ctl->recipe[pin], SOL_PIN_MODE_ANALOG);
}

SOL_API int
sol_pin_mux_setup_gpio(const int pin, const enum sol_gpio_direction dir)
{
    if (pin < 0) {
        SOL_WRN("Invalid GPIO pin: %d", pin);
        return -EINVAL;
    }

    if (!mux || pin >= (int)mux->gpio.len || !mux->gpio.recipe[pin])
        return 0;

    return _apply_mux_desc(mux->gpio.recipe[pin], dir == SOL_GPIO_DIR_OUT ?
        SOL_PIN_MODE_GPIO_OUTPUT : SOL_PIN_MODE_GPIO_INPUT_PULLUP);
}

SOL_API int
sol_pin_mux_setup_i2c(const uint8_t bus)
{
    int ret;

    if (!mux || (unsigned int)bus >= mux->i2c.len ||
        (!mux->i2c.recipe[bus][0] && !mux->i2c.recipe[bus][1]))
        return 0;

    ret = _apply_mux_desc(mux->i2c.recipe[bus][0], SOL_PIN_MODE_I2C);

    if (ret < 0)
        return ret;

    return _apply_mux_desc(mux->i2c.recipe[bus][1], SOL_PIN_MODE_I2C);
}

SOL_API int
sol_pin_mux_setup_pwm(const int device, const int channel)
{
    struct sol_pin_mux_controller *ctl;

    if (device < 0) {
        SOL_WRN("Invalid PWM device: %d", device);
        return -EINVAL;
    }

    if (channel < 0) {
        SOL_WRN("Invalid PWM channel: %d", channel);
        return -EINVAL;
    }

    if (!mux || device >= (int)mux->pwm.len)
        return 0;

    ctl = &mux->pwm.controllers[device];
    if (channel >= (int)ctl->len || !ctl->recipe[channel])
        return 0;

    return _apply_mux_desc(ctl->recipe[channel], SOL_PIN_MODE_PWM);
}
