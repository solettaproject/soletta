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

#include <contiki.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the event and data that woke up the Soletta protothread,
 * so this event can be internally handled.
 *
 * @warning Should only be used on Contiki implementation of SOL_MAIN_DEFAULT().
 *
 * @param ev the event that woke up the Soletta protothread, this can be a
 * timer or a sensor event for example.
 *
 * @param data the data of the event
 *
 * @see SOL_MAIN_DEFAULT()
 */
void sol_mainloop_contiki_event_set(process_event_t ev, process_data_t data);

/**
 * @brief Do a single mainloop iteration.
 *
 * @warning Should only be used on Contiki implementation of SOL_MAIN_DEFAULT().
 *
 * @see SOL_MAIN_DEFAULT()
 */
bool sol_mainloop_contiki_iter(void);

#ifdef __cplusplus
}
#endif
