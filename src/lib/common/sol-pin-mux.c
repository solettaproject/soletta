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
#include <dlfcn.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN &_sol_pin_mux_log_domain
#endif

#include "sol-log-internal.h"
#include "sol-util-internal.h"
#include "sol-pin-mux.h"
#include "sol-pin-mux-modules.h"
#include "sol-pin-mux-builtins-gen.h"
#include "sol-platform.h"

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
    char path[PATH_MAX], install_rootdir[PATH_MAX] = { 0 };
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
    SOL_NULL_CHECK_MSG_GOTO(p_sym, error,
        "Could not find symbol SOL_PIN_MUX in module '%s': %s", path, dlerror());

#ifndef SOL_NO_API_VERSION
    if (p_sym->api_version != SOL_PIN_MUX_API_VERSION) {
        SOL_WRN("Mux '%s' has incorrect api_version: %" PRIu16 " expected %"
            PRIu16, path, p_sym->api_version, SOL_PIN_MUX_API_VERSION);
        goto error;
    }
#endif

    if (dl_handle)
        dlclose(dl_handle);

    mux = p_sym;
    dl_handle = handle;

    SOL_INF("Loaded pin multiplexer '%s' from '%s'", mux->plat_name, path);
    return true;

error:
    dlclose(handle);
    return false;
#else
    return true;
#endif
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

    if (mux) {
        if (streq(_board, mux->plat_name))
            return true;

        if (mux->shutdown)
            mux->shutdown();

        mux = NULL;
    }

    // 'load_mux' only returns error if found a mux but failed to setup it.
    if (_find_mux(_board) || _load_mux(_board)) {
        if (mux && mux->init && mux->init())
            return false;

        return true;
    }

    return false;
}

int
sol_pin_mux_init(void)
{
    sol_log_domain_init_level(SOL_LOG_DOMAIN);

    if (!sol_pin_mux_select_mux(sol_platform_get_board_name())) {
        SOL_WRN("Pin Multiplexer found, but failed to be loaded.");
        return -1;
    }

    return 0;
}

void
sol_pin_mux_shutdown(void)
{
    if (mux && mux->shutdown)
        mux->shutdown();

    mux = NULL;
#ifdef ENABLE_DYNAMIC_MODULES
    if (dl_handle)
        dlclose(dl_handle);
    dl_handle = NULL;
#endif
}

SOL_API int
sol_pin_mux_map(const char *label, const enum sol_io_protocol prot, ...)
{
    int ret;
    va_list args;

    va_start(args, prot);
    ret = (mux && mux->pin_map) ? mux->pin_map(label, prot, args) : -EINVAL;
    va_end(args);

    return ret;
}

SOL_API int
sol_pin_mux_setup_aio(const int device, const int pin)
{
    return (mux && mux->aio) ? mux->aio(device, pin) : 0;
}

SOL_API int
sol_pin_mux_setup_gpio(const uint32_t pin, const struct sol_gpio_config *config)
{
    return (mux && mux->gpio) ? mux->gpio(pin, config) : 0;
}

SOL_API int
sol_pin_mux_setup_i2c(const uint8_t bus)
{
    return (mux && mux->i2c) ? mux->i2c(bus) : 0;
}

SOL_API int
sol_pin_mux_setup_pwm(const int device, const int channel)
{
    return (mux && mux->pwm) ? mux->pwm(device, channel) : 0;
}
