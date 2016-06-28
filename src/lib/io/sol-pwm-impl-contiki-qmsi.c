/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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
#include <stdlib.h>

#include <qm_pwm.h>
#include <qm_scss.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-pwm.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "pwm");

#define CLOCK_TICK_TIME_NS (31.25)

struct sol_pwm {
    qm_pwm_t dev;
    qm_pwm_id_t channel;
    uint32_t period;
    uint32_t duty_cycle;
    bool enabled;
};

SOL_API struct sol_pwm *
sol_pwm_open_raw(int device, int channel, const struct sol_pwm_config *config)
{
    struct sol_pwm *pwm;
    qm_pwm_config_t cfg;
    qm_rc_t ret;
    int v;
    bool r;

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (unlikely(config->api_version != SOL_PWM_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open PWM that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_PWM_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    if (device >= QM_PWM_NUM) {
        SOL_WRN("PWM device number #%d does not exist.", device);
        return NULL;
    }
    if (channel >= QM_PWM_ID_NUM) {
        SOL_WRN("PWM channel #%d for device #%d does not exist.", channel, device);
        return NULL;
    }

    pwm = calloc(1, sizeof(*pwm));
    SOL_NULL_CHECK(pwm, NULL);

    pwm->dev = device;
    pwm->channel = channel;
    pwm->period = 1;
    pwm->duty_cycle = 1;

    clk_periph_enable(CLK_PERIPH_PWM_REGISTER | CLK_PERIPH_CLK);

    ret = qm_pwm_get_config(device, channel, &cfg);
    SOL_EXP_CHECK_GOTO(ret != QM_RC_OK, error);

    cfg.mode = QM_PWM_MODE_PWM;
    cfg.mask_interrupt = true;
    cfg.lo_count = 1;
    cfg.hi_count = 1;

    ret = qm_pwm_set_config(device, channel, &cfg);
    SOL_EXP_CHECK_GOTO(ret != QM_RC_OK, error);

    if (config->period_ns != -1)
        sol_pwm_set_period(pwm, config->period_ns);
    if (config->duty_cycle_ns != -1)
        sol_pwm_set_duty_cycle(pwm, config->duty_cycle_ns);

    v = sol_pwm_set_enabled(pwm, config->enabled);
    SOL_INT_CHECK_GOTO(v, < 0, error);

    return pwm;

error:
    free(pwm);
    return NULL;
}

SOL_API void
sol_pwm_close(struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm);

    qm_pwm_set(pwm->dev, pwm->channel, 0, 1);
    qm_pwm_stop(pwm->dev, pwm->channel);
    free(pwm);
}

SOL_API int
sol_pwm_set_enabled(struct sol_pwm *pwm, bool enable)
{
    qm_rc_t ret;

    SOL_NULL_CHECK(pwm, -EINVAL);

    if (enable)
        ret = qm_pwm_start(pwm->dev, pwm->channel);
    else
        ret = qm_pwm_stop(pwm->dev, pwm->channel);
    pwm->enabled = enable;

    return (ret == QM_RC_OK) ? 0 : -EIO;
}

SOL_API bool
sol_pwm_is_enabled(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, false);

    return pwm->enabled;
}

static qm_rc_t
pwm_set_values(struct sol_pwm *pwm)
{
    uint32_t lo;

    lo = pwm->period - pwm->duty_cycle;
    if (!lo)
        lo = 1;
    return qm_pwm_set(pwm->dev, pwm->channel, lo, pwm->duty_cycle);
}

SOL_API int
sol_pwm_set_period(struct sol_pwm *pwm, uint32_t period_ns)
{
    qm_rc_t ret;

    SOL_NULL_CHECK(pwm, -EINVAL);

    pwm->period = period_ns / CLOCK_TICK_TIME_NS;
    if (!pwm->period)
        pwm->period = 1;
    if (pwm->duty_cycle > pwm->period)
        pwm->duty_cycle = pwm->period;
    ret = pwm_set_values(pwm);
    return ret == QM_RC_OK ? 0 : -EIO;
}

SOL_API int32_t
sol_pwm_get_period(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, -EINVAL);

    return (int32_t)(pwm->period * CLOCK_TICK_TIME_NS);
}

SOL_API int
sol_pwm_set_duty_cycle(struct sol_pwm *pwm, uint32_t duty_cycle_ns)
{
    qm_rc_t ret;
    uint32_t duty = duty_cycle_ns / CLOCK_TICK_TIME_NS;

    SOL_NULL_CHECK(pwm, -EINVAL);
    SOL_INT_CHECK(duty, > pwm->period, -EINVAL);

    pwm->duty_cycle = duty;
    if (!pwm->duty_cycle)
        pwm->duty_cycle = 1;
    ret = pwm_set_values(pwm);
    return ret == QM_RC_OK ? 0 : -EIO;
}

SOL_API int32_t
sol_pwm_get_duty_cycle(const struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm, -EINVAL);

    return (int32_t)(pwm->duty_cycle * CLOCK_TICK_TIME_NS);
}
