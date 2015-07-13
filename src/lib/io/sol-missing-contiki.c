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

#include <stdint.h>
#include <stdlib.h>

#include "sol-pwm.h"

struct sol_pwm *
sol_pwm_open_raw(int device, int channel, const struct sol_pwm_config *config)
{
    return NULL;
}

void
sol_pwm_close(struct sol_pwm *pwm)
{
}

bool
sol_pwm_set_enabled(struct sol_pwm *pwm, bool enable)
{
    return false;
}

bool
sol_pwm_get_enabled(const struct sol_pwm *pwm)
{
    return false;
}

bool
sol_pwm_set_period(struct sol_pwm *pwm, uint32_t period_ns)
{
    return false;
}

int32_t
sol_pwm_get_period(const struct sol_pwm *pwm)
{
    return -1;
}

bool
sol_pwm_set_duty_cycle(struct sol_pwm *pwm, uint32_t duty_cycle_ns)
{
    return false;
}

int32_t
sol_pwm_get_duty_cycle(const struct sol_pwm *pwm)
{
    return -1;
}
