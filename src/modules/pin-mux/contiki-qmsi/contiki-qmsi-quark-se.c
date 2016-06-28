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
se_mux_init(void)
{
    uint32_t pins[HOW_MANY_INTS] = { 0 };

    SET_PIN_FN(pins, QM_PIN_ID_0, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_1, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_2, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_3, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_8, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_9, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_16, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_17, QM_PMUX_FN_2);
    SET_PIN_FN(pins, QM_PIN_ID_33, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_40, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_41, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_42, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_43, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_44, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_55, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_56, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_57, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_63, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_64, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_65, QM_PMUX_FN_1);
    SET_PIN_FN(pins, QM_PIN_ID_66, QM_PMUX_FN_1);

    contiki_qmsi_pin_mux_set(pins, QM_PIN_ID_NUM);
    return 0;
}

SOL_PIN_MUX_DECLARE(CONTIKI_QMSI_QUARK_SE,
    .plat_name = "quark-se-devboard",
    .init = se_mux_init,
    );

