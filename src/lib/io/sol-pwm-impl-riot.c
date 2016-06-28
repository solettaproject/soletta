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

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-pwm.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#include "periph/pwm.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "pwm");

// Resolution of PWM period, how many divisions a period can have
#define RESOLUTION  255

struct dev_ref {
    uint16_t device;
    uint16_t ref;
};

static struct sol_vector _dev_ref = SOL_VECTOR_INIT(struct dev_ref);

struct sol_pwm {
    pwm_t dev;
    int channel;

    pwm_mode_t phase;
    uint32_t period;
    uint32_t duty_cycle;
    bool enable;
};

static void
_power_on(const int device)
{
    uint16_t i;
    struct dev_ref *ref;

    SOL_VECTOR_FOREACH_IDX (&_dev_ref, ref, i) {
        if (ref->device == device) {
            ref->ref++;
            return;
        }
    }

    ref = sol_vector_append(&_dev_ref);
    ref->device = device;
    ref->ref = 1;
    pwm_poweron(device);
}

static void
_power_off(const int device)
{
    uint16_t i;
    struct dev_ref *ref;

    SOL_VECTOR_FOREACH_IDX (&_dev_ref, ref, i) {
        if (ref->device == device) {
            if (!--ref->ref) {
                sol_vector_del(&_dev_ref, i);
                pwm_poweroff(device);
            }
            return;
        }
    }

    SOL_DBG("pwm: Trying to power off device %d, but reference was not found.", device);
}

SOL_API struct sol_pwm *
sol_pwm_open_raw(int device, int channel, const struct sol_pwm_config *config)
{
    struct sol_pwm *pwm;

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(config->api_version != SOL_PWM_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open pwm that has unsupported version '%" PRIu16 "', "
            "expected version is '%" PRIu16 "'",
            config->api_version, SOL_PWM_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    pwm = calloc(1, sizeof(struct sol_pwm));
    SOL_NULL_CHECK(pwm, NULL);

    pwm->dev = device;
    pwm->channel = channel;
    pwm->phase = config->alignment;

    _power_on(pwm->dev);
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
    _power_off(pwm->dev);
    free(pwm);
}

SOL_API int
sol_pwm_set_enabled(struct sol_pwm *pwm, bool enable)
{
    SOL_NULL_CHECK(pwm, -EINVAL);

    if (enable)
        pwm_start(pwm->dev);
    else
        pwm_stop(pwm->dev);
    pwm->enable = enable;

    return 0;
}

SOL_API bool
sol_pwm_is_enabled(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, false);
    return pwm->enable;
}

SOL_API int
sol_pwm_set_period(struct sol_pwm *pwm, uint32_t period_ns)
{
    SOL_NULL_CHECK(pwm, -EINVAL);
    pwm->period = period_ns;

    return pwm_init(pwm->dev, pwm->phase, SOL_UTIL_NSEC_PER_SEC / pwm->period, RESOLUTION);
}

SOL_API int32_t
sol_pwm_get_period(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, -EINVAL);
    return (int32_t)pwm->period;
}

SOL_API int
sol_pwm_set_duty_cycle(struct sol_pwm *pwm, uint32_t duty_cycle_ns)
{
    double value;

    SOL_NULL_CHECK(pwm, -EINVAL);
    SOL_INT_CHECK(duty_cycle_ns, > pwm->period, -EINVAL);

    pwm->duty_cycle = duty_cycle_ns;
    value = (RESOLUTION * duty_cycle_ns) / pwm->period;

    pwm_set(pwm->dev, pwm->channel, value);

    return 0;
}

SOL_API int32_t
sol_pwm_get_duty_cycle(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, -EINVAL);
    return (int32_t)pwm->duty_cycle;
}
