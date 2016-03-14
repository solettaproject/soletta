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
 * @brief PWM (Pulse-width modulation) API for Soletta.
 *
 * @{
 */

struct sol_pwm;

/**
 * @brief Alignment determines how the pulse is aligned within the PWM period.
 *
 * No API for this on Linux (and other OSes), so we simply ignore it there
 */
enum sol_pwm_alignment {
    SOL_PWM_ALIGNMENT_LEFT, /**< The pulse is aligned to the leading-edge (left) of the PWM period. */
    SOL_PWM_ALIGNMENT_RIGHT, /**< The pulse is aligned to the trailing-edge (right) of the PWM period. */
    SOL_PWM_ALIGNMENT_CENTER /**< The pulse is aligned to the center of the PWM period. Also known as phase-correct. */
};

/**
 * @brief Polarity is whether the output is active-high or active-low.
 *
 * In the paired and complementary configurations, the polarity of the
 * secondary PWM output is determined by the polarity of the
 * primary PWM channel.
 *
 * This is ignored on RIOT (no API there) and not always supported on Linux.
 */
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

/**
 * @brief Opens a given pin by its board label as pwm.
 *
 * This function only works when the board was successfully detected
 * by Soletta and a corresponding
 * pin multiplexer module was found.
 *
 * A pin should be opened just once, calling this function more than once
 * for the same pin results in undefined behavior - per platform basis.
 *
 * @see sol_pwm_open_raw(), sol_pwm_close().
 *
 * @param label The pin to be opened.
 * @param config Contains the pin configuration.
 *
 * @return A new @c sol_pwm instance on success, @c NULL otherwise.
 */
struct sol_pwm *sol_pwm_open_by_label(const char *label, const struct sol_pwm_config *config);

/**
 * @brief Opens a given pin as pwm.
 *
 * A pin (defined by device and channel) should be opened just once,
 * calling this function more than once
 * for the same pin results in undefined behaviour - per platform basis.
 *
 * @see sol_pwm_open_raw(), sol_pwm_close().
 *
 * The difference between sol_pwm_open_raw() and sol_pwm_open() is that
 * the last will setup pin mux, if enabled.
 *
 * @param device The device controlling the pin to be opened.
 * @param channel The channel used to communicate with the pin to be opened.
 * @param config Contains the pin configuration.
 *
 * @return A new @c sol_pwm instance on success, @c NULL otherwise.
 */
struct sol_pwm *sol_pwm_open(int device, int channel, const struct sol_pwm_config *config);

/**
 * @brief Opens a given pin as pwm.
 *
 * A pin (defined by device and channel) should be opened just once,
 * calling this function more than once
 * for the same pin results in undefined behaviour - per platform basis.
 *
 * @see sol_pwm_open(), sol_pwm_close().
 *
 * @param device The device controlling the pin to be opened.
 * @param channel The channel used to communicate with the pin to be opened.
 * @param config Contains the pin configuration.
 *
 * @return A new @c sol_pwm instance on success, @c NULL otherwise.
 */
struct sol_pwm *sol_pwm_open_raw(int device, int channel, const struct sol_pwm_config *config);

/**
 * @brief Closes a given PWM pin.
 *
 * @see sol_pwm_open(), sol_pwm_open_raw().
 *
 * @param pwm The open @c sol_pwm representing the pin to be closed.
 */
void sol_pwm_close(struct sol_pwm *pwm);

/**
 * @brief Enable or disable a given pwm pin.
 *
 * @param pwm PWM pin to be modified.
 *
 * @param enable If @c true pwm is enabled and if @c false it's disabled.
 *
 * @return @c true on success or @c false on error.
 */
bool sol_pwm_set_enabled(struct sol_pwm *pwm, bool enable);

/**
 * @brief Check wheter a pmw pin is enabled or disabled.
 *
 * @param pwm PWM pin to get property.
 *
 * @return @c true if enabled or @c false if disabled or on error.
 */
bool sol_pwm_get_enabled(const struct sol_pwm *pwm);

/**
 * @brief Set PWM period in nanoseconds.
 *
 * Period is the amount of time that a cycle (on / off state) takes.
 * It's the inverse of the frequency of the waveform.
 *
 * @param pwm PWM pin to be modified.
 *
 * @param period_ns Period in nanoseconds.
 *
 * @return @c true on success and @c false on error.
 */
bool sol_pwm_set_period(struct sol_pwm *pwm, uint32_t period_ns);

/**
 * @brief Get PWM period in nanoseconds.
 *
 * @see sol_pwm_set_period() for more details.
 *
 * @param pwm PWM pin to get property.
 *
 * @return Period in nanoseconds. It may be a negative value on error.
 */
int32_t sol_pwm_get_period(const struct sol_pwm *pwm);

/**
 * @brief Set PWM duty cycle in nanoseconds.
 *
 * Duty cycle describes the proportion of 'on' time to the regular interval
 * or 'period' of time.
 *
 * A low duty cycle corresponds to low power, because the power is off
 * for most of the time.
 *
 * @param pwm PWM pin to be modified.
 *
 * @param duty_cycle_ns Duty cycle in nanoseconds.
 *
 * @return @c true on success and @c false on error.
 */
bool sol_pwm_set_duty_cycle(struct sol_pwm *pwm, uint32_t duty_cycle_ns);

/**
 * @brief Get PWM duty cycle in nanoseconds.
 *
 * @see sol_pwm_set_duty_cycle() for more details.
 *
 * @param pwm PWM pin to get property.
 *
 * @return Duty cycle in nanoseconds. It may be a negative value on error.
 */
int32_t sol_pwm_get_duty_cycle(const struct sol_pwm *pwm);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
