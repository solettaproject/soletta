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
#include <errno.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-pwm.h"
#include "sol-util.h"

#include "periph/pwm.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "pwm");

// Resolution of PWM period, how many divisions a period can have
#define RESOLUTION  255

struct sol_pwm {
    pwm_t dev;
    int channel;

    pwm_mode_t phase;
    uint32_t period;
    uint32_t duty_cycle;
    bool enable;
};

SOL_API struct sol_pwm *
sol_pwm_open_raw(int device, int channel, const struct sol_pwm_config *config)
{
    struct sol_pwm *pwm;

    SOL_LOG_INTERNAL_INIT_ONCE;

    if (unlikely(config->api_version != SOL_PWM_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open pwm that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_PWM_CONFIG_API_VERSION);
        return NULL;
    }

    pwm = calloc(1, sizeof(struct sol_pwm));
    SOL_NULL_CHECK(pwm, NULL);

    pwm->dev = device;
    pwm->channel = channel;
    pwm->phase = config->alignment;

    pwm_poweron(pwm->dev);
    if (config->period_ns != -1)
        sol_pwm_set_period(pwm, config->period_ns);
    if (config->duty_cycle_ns != -1)
        sol_pwm_set_duty_cycle(pwm, config->duty_cycle_ns);
    sol_pwm_set_enabled(pwm, config->enabled);

    return pwm;
}

SOL_API void
sol_pwm_close(struct sol_pwm *pwm)
{
    sol_pwm_set_duty_cycle(pwm, 0);
    sol_pwm_set_period(pwm, 0);
    pwm_stop(pwm->dev);
    pwm_poweroff(pwm->dev);
    free(pwm);
}

SOL_API bool
sol_pwm_set_enabled(struct sol_pwm *pwm, bool enable)
{
    SOL_NULL_CHECK(pwm, false);

    if (enable)
        pwm_start(pwm->dev);
    else
        pwm_stop(pwm->dev);
    pwm->enable = enable;

    return true;
}

SOL_API bool
sol_pwm_get_enabled(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, false);
    return pwm->enable;
}

SOL_API bool
sol_pwm_set_period(struct sol_pwm *pwm, uint32_t period_ns)
{
    SOL_NULL_CHECK(pwm, false);
    pwm->period = period_ns;

    return pwm_init(pwm->dev, pwm->phase, NSEC_PER_SEC / pwm->period, RESOLUTION) == 0;
}

SOL_API int32_t
sol_pwm_get_period(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, -EINVAL);
    return (int32_t)pwm->period;
}

SOL_API bool
sol_pwm_set_duty_cycle(struct sol_pwm *pwm, uint32_t duty_cycle_ns)
{
    double value;

    SOL_NULL_CHECK(pwm, false);
    SOL_INT_CHECK(duty_cycle_ns, > pwm->period, false);

    pwm->duty_cycle = duty_cycle_ns;
    value = (RESOLUTION * duty_cycle_ns) / pwm->period;

    return pwm_set(pwm->dev, pwm->channel, value) == 0;
}

SOL_API int32_t
sol_pwm_get_duty_cycle(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, -EINVAL);
    return (int32_t)pwm->duty_cycle;
}
