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
