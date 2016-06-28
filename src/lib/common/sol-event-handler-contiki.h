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

#include <stdbool.h>

#include <contiki.h>

int sol_mainloop_contiki_event_handler_add(const process_event_t *ev, const process_data_t ev_data, void (*cb)(void *user_data, process_event_t ev, process_data_t ev_data), const void *data);
bool sol_mainloop_contiki_event_handler_del(const process_event_t *ev, const process_data_t ev_data, void (*cb)(void *user_data, process_event_t ev, process_data_t ev_data), const void *data);
