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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <sol-common-buildopts.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for PWM access under Soletta.
 */

/**
 * @defgroup PWM PWM
 * @ingroup IO
 *
 * PWM (Pulse-width modulation) API for Soletta.
 *
 * @{
 */

struct sol_pwm;

/* No API for this on Linux, so we simply ignore it there */
enum sol_pwm_alignment {
    SOL_PWM_ALIGNMENT_LEFT,
    SOL_PWM_ALIGNMENT_RIGHT,
    SOL_PWM_ALIGNMENT_CENTER /* Also known as phase-correct */
};

/* This is ignored on RIOT (no API there) and not always supported on Linux */
enum sol_pwm_polarity {
    SOL_PWM_POLARITY_NORMAL,
    SOL_PWM_POLARITY_INVERSED
};

struct sol_pwm_config {
#ifndef SOL_NO_API_VERSION
#define SOL_PWM_CONFIG_API_VERSION (1)
    uint16_t api_version;
#endif
    int32_t period_ns; /* if == -1, won't set */
    int32_t duty_cycle_ns; /* if == -1, won't set, but if period is set, duty cycle is zeroed */
    enum sol_pwm_alignment alignment;
    enum sol_pwm_polarity polarity;
    bool enabled;
};

struct sol_pwm *sol_pwm_open_by_label(const char *label, const struct sol_pwm_config *config);
struct sol_pwm *sol_pwm_open(int device, int channel, const struct sol_pwm_config *config);
struct sol_pwm *sol_pwm_open_raw(int device, int channel, const struct sol_pwm_config *config);
void sol_pwm_close(struct sol_pwm *pwm);

bool sol_pwm_set_enabled(struct sol_pwm *pwm, bool enable);
bool sol_pwm_get_enabled(const struct sol_pwm *pwm);

bool sol_pwm_set_period(struct sol_pwm *pwm, uint32_t period_ns);
int32_t sol_pwm_get_period(const struct sol_pwm *pwm);
bool sol_pwm_set_duty_cycle(struct sol_pwm *pwm, uint32_t duty_cycle_ns);
int32_t sol_pwm_get_duty_cycle(const struct sol_pwm *pwm);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
