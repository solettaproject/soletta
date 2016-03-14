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

#include <stdlib.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "pwm");

#include "sol-pwm.h"
#ifdef USE_PIN_MUX
#include "sol-pin-mux.h"
#endif

static void
_log_init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
}

SOL_API struct sol_pwm *
sol_pwm_open_by_label(const char *label, const struct sol_pwm_config *config)
{
    int device, channel;

    _log_init();

#ifdef USE_PIN_MUX
    if (!sol_pin_mux_map(label, SOL_IO_PWM, &device, &channel))
        return sol_pwm_open(device, channel, config);

    SOL_WRN("Label '%s' couldn't be mapped or can't be used as PWM", label);
#else
    SOL_INF("Pin Multiplexer support is necessary to open a 'board pin'.");
    (void)device;
    (void)channel;
#endif

    return NULL;
}

SOL_API struct sol_pwm *
sol_pwm_open(int device, int channel, const struct sol_pwm_config *config)
{
    struct sol_pwm *pwm;

    _log_init();

    pwm = sol_pwm_open_raw(device, channel, config);
#ifdef USE_PIN_MUX
    if (pwm && sol_pin_mux_setup_pwm(device, channel)) {
        SOL_WRN("Pin Multiplexer Recipe for pwm device=%d channel=%d found, \
            but couldn't be applied.", device, channel);
        sol_pwm_close(pwm);
        pwm = NULL;
    }
#endif

    return pwm;
}
