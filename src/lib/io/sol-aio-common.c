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

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "aio");

#include "sol-aio.h"
#ifdef USE_PIN_MUX
#include "sol-pin-mux.h"
#endif

static void
_log_init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
}

SOL_API struct sol_aio *
sol_aio_open_by_label(const char *label, unsigned int precision)
{
#ifdef USE_PIN_MUX
    int device, pin;
#endif

    _log_init();

#ifdef USE_PIN_MUX
    if (!sol_pin_mux_map(label, SOL_IO_AIO, &device, &pin))
        return sol_aio_open(device, pin, precision);

    SOL_WRN("Label '%s' couldn't be mapped or can't be used as Analog I/O", label);
#else
    SOL_INF("Pin Multiplexer support is necessary to open a 'board pin'.");
#endif

    return NULL;
}

SOL_API struct sol_aio *
sol_aio_open(int device, int pin, unsigned int precision)
{
    struct sol_aio *aio;

    _log_init();

    aio = sol_aio_open_raw(device, pin, precision);
#ifdef USE_PIN_MUX
    if (aio && sol_pin_mux_setup_aio(device, pin)) {
        SOL_WRN("Pin Multiplexer Recipe for aio device=%d pin=%d found, "
            "but couldn't be applied.", device, pin);
        sol_aio_close(aio);
        aio = NULL;
    }
#endif

    return aio;
}
