/*
 * This file is part of the Soletta™ Project
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

#include "contiki-qmsi-common.h"

static inline void
set_mux(uint32_t *pins, uint32_t pin)
{
    qm_pmux_fn_t fn;

    fn = (pins[pin / 16] >> ((pin % 16) * 2)) & 0x3;
    qm_pmux_select(pin, fn);
}

void
contiki_qmsi_pin_mux_set(uint32_t *pins, uint32_t pin_count)
{
    uint32_t i;

    for (i = 0; i < pin_count; i++) {
        set_mux(pins, i++);
    }
}
