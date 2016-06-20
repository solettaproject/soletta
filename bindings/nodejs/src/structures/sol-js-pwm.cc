/*
 * This file is part of the Soletta™ Project
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

#include <nan.h>
#include "sol-js-pwm.h"
#include "../common.h"

using namespace v8;

bool c_sol_pwm_config(v8::Local<v8::Object> jsPWMConfig, sol_pwm_config *config) {
    SOL_SET_API_VERSION(config->api_version = SOL_PWM_CONFIG_API_VERSION;)

    VALIDATE_AND_ASSIGN((*config), period_ns,  int32_t, IsInt32,
        "(PWM Period)", false, jsPWMConfig, Int32Value);

    VALIDATE_AND_ASSIGN((*config), duty_cycle_ns,  int32_t, IsInt32,
        "(PWM Duty Cycle)", false, jsPWMConfig, Int32Value);

    VALIDATE_AND_ASSIGN((*config), alignment, sol_pwm_alignment, IsInt32,
        "(PWM Alignment)", false, jsPWMConfig, Int32Value);

    VALIDATE_AND_ASSIGN((*config), polarity, sol_pwm_polarity, IsInt32,
        "(PWM polarity)", false, jsPWMConfig, Int32Value);

    VALIDATE_AND_ASSIGN((*config), enabled, bool, IsBoolean,
        "(PWM enabled)", false, jsPWMConfig, BooleanValue);

    return true;
}
