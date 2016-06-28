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

#pragma once

#include <time.h>

#include "sol-flow.h"
#include "sol-flow-packet.h"
#include "sol-types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PLANT INFORMATION */

struct sol_flower_power_data {
    struct sol_drange fertilizer;
    struct sol_drange fertilizer_max;
    struct sol_drange fertilizer_min;
    struct sol_drange light;
    struct sol_drange light_max;
    struct sol_drange light_min;
    struct sol_drange temperature;
    struct sol_drange temperature_max;
    struct sol_drange temperature_min;
    struct sol_drange water;
    struct sol_drange water_max;
    struct sol_drange water_min;
    struct timespec timestamp;
    char *id;
};

#define SOL_FLOWER_POWER_DATA_INIT_VALUE(value_) \
    { \
        .fertilizer = SOL_DRANGE_INIT_VALUE(value_), \
        .fertilizer_max = SOL_DRANGE_INIT_VALUE(value_), \
        .fertilizer_min = SOL_DRANGE_INIT_VALUE(value_), \
        .light = SOL_DRANGE_INIT_VALUE(value_), \
        .light_max = SOL_DRANGE_INIT_VALUE(value_), \
        .light_min = SOL_DRANGE_INIT_VALUE(value_), \
        .temperature = SOL_DRANGE_INIT_VALUE(value_), \
        .temperature_max = SOL_DRANGE_INIT_VALUE(value_), \
        .temperature_min = SOL_DRANGE_INIT_VALUE(value_), \
        .water = SOL_DRANGE_INIT_VALUE(value_), \
        .water_max = SOL_DRANGE_INIT_VALUE(value_), \
        .water_min = SOL_DRANGE_INIT_VALUE(value_), \
        .timestamp = { 0 }, \
        .id = NULL \
    }

/* SENSOR INFORMATION */

struct sol_flower_power_sensor_data {
    struct sol_drange battery_level;
    struct timespec timestamp;
    struct timespec battery_end_of_life;
    char *id;
};

#ifdef __cplusplus
}
#endif
