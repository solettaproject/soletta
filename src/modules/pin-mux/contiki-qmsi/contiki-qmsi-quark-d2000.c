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

#include "sol-pin-mux-modules.h"
#include "contiki-qmsi-common.h"

static int
d2000_mux_init(void)
{
    uint32_t pins[HOW_MANY_INTS] = { 0 };

    SET_PIN_FN(pins, QM_PIN_ID_3, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_4, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_6, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_7, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_12, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_13, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_14, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_15, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_16, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_17, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_18, QM_PMUX_FN_2);

    contiki_qmsi_pin_mux_set(pins, QM_PIN_ID_NUM);
    return 0;
}

SOL_PIN_MUX_DECLARE(CONTIKI_QMSI_QUARK_D2000,
    .plat_name = "quark-d2000-devboard",
    .init = d2000_mux_init,
    );
