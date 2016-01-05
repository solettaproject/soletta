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

/* Zephyr includes */
#include "pwm.h"

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "pwm");

#include "sol-pwm.h"
#include "sol-util.h"
#include "sol-vector.h"

/* The current DW PWM driver implementation works on top of a nominal
 * system clock at 32MHz, thus 31.25ns period/tick-time */
#define CLOCK_TICK_TIME_NS (31.25)

struct pwm_dev {
    const char *name;
};

static struct pwm_dev pwm_0_dev = {
    .name = "PWM_DW"
};

static struct pwm_dev *devs[1] = {
    &pwm_0_dev
};

struct sol_pwm {
    struct device *dev;

    int channel;

    uint32_t period;
    uint32_t duty_cycle;
    bool enabled;
};

SOL_API struct sol_pwm *
sol_pwm_open_raw(int device, int channel, const struct sol_pwm_config *config)
{
    struct sol_pwm *pwm = NULL;
    struct device *dev = NULL;

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (unlikely(config->api_version != SOL_PWM_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open pwm that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_PWM_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    if (device != 0) {
        SOL_WRN("Unsupported AIO device %d", device);
        goto err;
    }

    dev = device_get_binding((char *)devs[device]->name);
    if (!dev) {
        SOL_WRN("Failed to open AIO device %s", devs[device]->name);
        return NULL;
    }

    pwm = calloc(1, sizeof(struct sol_pwm));
    SOL_NULL_CHECK(pwm, NULL);

    pwm->dev = dev;
    pwm->channel = channel;

    if (config->period_ns != -1)
        sol_pwm_set_period(pwm, config->period_ns);
    if (config->duty_cycle_ns != -1)
        sol_pwm_set_duty_cycle(pwm, config->duty_cycle_ns);
    sol_pwm_set_enabled(pwm, config->enabled);

    pwm->dev = dev;

err:
    return pwm;
}

SOL_API void
sol_pwm_close(struct sol_pwm *pwm)
{
    sol_pwm_set_duty_cycle(pwm, 0);
    sol_pwm_set_period(pwm, 0);
    sol_pwm_set_enabled(pwm, false);
    free(pwm);
}

SOL_API bool
sol_pwm_set_enabled(struct sol_pwm *pwm, bool enabled)
{
    SOL_NULL_CHECK(pwm, false);

    if (enabled)
        pwm_resume(pwm->dev);
    else
        pwm_suspend(pwm->dev);

    pwm->enabled = enabled;

    return true;
}

SOL_API bool
sol_pwm_get_enabled(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, false);

    return pwm->enabled;
}

SOL_API bool
sol_pwm_set_period(struct sol_pwm *pwm, uint32_t period_ns)
{
    int ret;

    SOL_NULL_CHECK(pwm, false);
    pwm->period = period_ns / CLOCK_TICK_TIME_NS;

    if (pwm->duty_cycle > pwm->period) {
        SOL_WRN("Currently set duty cicle %" PRId32 " was greater than the "
            "requested period, making both equal.",
            (int32_t)(pwm->duty_cycle * CLOCK_TICK_TIME_NS));

        pwm->duty_cycle = pwm->period;
    }

    ret = pwm_pin_set_values(pwm->dev, pwm->channel,
        pwm->duty_cycle, pwm->period - pwm->duty_cycle);

    return ret == DEV_OK;
}

SOL_API int32_t
sol_pwm_get_period(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, -EINVAL);

    return (int32_t)pwm->period * CLOCK_TICK_TIME_NS;
}

SOL_API bool
sol_pwm_set_duty_cycle(struct sol_pwm *pwm, uint32_t duty_cycle_ns)
{
    int ret;
    uint32_t duty = duty_cycle_ns / CLOCK_TICK_TIME_NS;

    SOL_NULL_CHECK(pwm, false);
    SOL_INT_CHECK(duty, > pwm->period, false);

    pwm->duty_cycle = duty;
    ret = pwm_pin_set_values(pwm->dev, pwm->channel,
        pwm->duty_cycle, pwm->period - pwm->duty_cycle);

    return ret == DEV_OK;
}

SOL_API int32_t
sol_pwm_get_duty_cycle(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, -EINVAL);

    return (int32_t)pwm->duty_cycle * CLOCK_TICK_TIME_NS;
}
