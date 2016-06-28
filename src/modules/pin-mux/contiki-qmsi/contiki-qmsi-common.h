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

#pragma once

#include <stdint.h>

#include <qm_pinmux.h>

#define HOW_MANY_INTS (QM_PIN_ID_NUM / 16 + (QM_PIN_ID_NUM % 16 ? 1 : 0))
#define SET_PIN_FN(pins, pin, fn) \
    pins[(pin) / 16] |= ((fn) & 0x3) << (((pin) % 16) * 2)

void contiki_qmsi_pin_mux_set(uint32_t *pins, uint32_t pin_count);
