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
        SOL_WRN("Failed to open PWM device %s", devs[device]->name);
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

SOL_API int
sol_pwm_set_enabled(struct sol_pwm *pwm, bool enabled)
{
    SOL_NULL_CHECK(pwm, -EINVAL);

    if (enabled)
        pwm_resume(pwm->dev);
    else
        pwm_suspend(pwm->dev);

    pwm->enabled = enabled;

    return 0;
}

SOL_API bool
sol_pwm_is_enabled(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, false);

    return pwm->enabled;
}

SOL_API int
sol_pwm_set_period(struct sol_pwm *pwm, uint32_t period_ns)
{
    int ret;

    SOL_NULL_CHECK(pwm, -EINVAL);
    pwm->period = period_ns / CLOCK_TICK_TIME_NS;

    if (pwm->duty_cycle > pwm->period) {
        SOL_WRN("Currently set duty cicle %" PRId32 " was greater than the "
            "requested period, making both equal.",
            (int32_t)(pwm->duty_cycle * CLOCK_TICK_TIME_NS));

        pwm->duty_cycle = pwm->period;
    }

    ret = pwm_pin_set_values(pwm->dev, pwm->channel,
        pwm->duty_cycle, pwm->period - pwm->duty_cycle);

    return ret == 0 ? 0 : -EIO;
}

SOL_API int32_t
sol_pwm_get_period(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, -EINVAL);

    return (int32_t)pwm->period * CLOCK_TICK_TIME_NS;
}

SOL_API int
sol_pwm_set_duty_cycle(struct sol_pwm *pwm, uint32_t duty_cycle_ns)
{
    int ret;
    uint32_t duty = duty_cycle_ns / CLOCK_TICK_TIME_NS;

    SOL_NULL_CHECK(pwm, -EINVAL);
    SOL_INT_CHECK(duty, > pwm->period, -EINVAL);

    pwm->duty_cycle = duty;
    ret = pwm_pin_set_values(pwm->dev, pwm->channel,
        pwm->duty_cycle, pwm->period - pwm->duty_cycle);

    return ret == 0 ? 0 : -EIO;
}

SOL_API int32_t
sol_pwm_get_duty_cycle(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, -EINVAL);

    return (int32_t)pwm->duty_cycle * CLOCK_TICK_TIME_NS;
}
