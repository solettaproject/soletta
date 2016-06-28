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
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "pwm");

#include "sol-pwm.h"
#ifdef USE_PIN_MUX
#include "sol-pin-mux.h"
#endif
#include "sol-str-table.h"
#include "sol-util.h"

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

SOL_API enum sol_pwm_alignment
sol_pwm_alignment_from_str(const char *pwm_alignment)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("left", SOL_PWM_ALIGNMENT_LEFT),
        SOL_STR_TABLE_ITEM("right", SOL_PWM_ALIGNMENT_RIGHT),
        SOL_STR_TABLE_ITEM("center", SOL_PWM_ALIGNMENT_CENTER),
        { }
    };

    SOL_NULL_CHECK(pwm_alignment, SOL_PWM_ALIGNMENT_LEFT);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(pwm_alignment), SOL_PWM_ALIGNMENT_LEFT);
}

SOL_API const char *
sol_pwm_alignment_to_str(enum sol_pwm_alignment pwm_alignment)
{
    static const char *alignment_names[] = {
        [SOL_PWM_ALIGNMENT_LEFT] = "left",
        [SOL_PWM_ALIGNMENT_RIGHT] = "right",
        [SOL_PWM_ALIGNMENT_CENTER] = "center"
    };

    if (pwm_alignment < sol_util_array_size(alignment_names))
        return alignment_names[pwm_alignment];

    return NULL;
}

SOL_API enum sol_pwm_polarity
sol_pwm_polarity_from_str(const char *pwm_polarity)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("normal", SOL_PWM_POLARITY_NORMAL),
        SOL_STR_TABLE_ITEM("inversed", SOL_PWM_POLARITY_INVERSED),
        { }
    };

    SOL_NULL_CHECK(pwm_polarity, SOL_PWM_POLARITY_NORMAL);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(pwm_polarity), SOL_PWM_POLARITY_NORMAL);
}

SOL_API const char *
sol_pwm_polarity_to_str(enum sol_pwm_polarity pwm_polarity)
{
    static const char *polarity_names[] = {
        [SOL_PWM_POLARITY_NORMAL] = "normal",
        [SOL_PWM_POLARITY_INVERSED] = "inversed"
    };

    if (pwm_polarity < sol_util_array_size(polarity_names))
        return polarity_names[pwm_polarity];

    return NULL;
}
