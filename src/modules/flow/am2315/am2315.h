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

struct am2315;

struct am2315 *am2315_open(uint8_t bus, uint8_t slave);

void am2315_close(struct am2315 *device);

void am2315_temperature_callback_set(struct am2315 *device, void (*cb)(float temperature, bool success, void *data), void *data);

void am2315_read_temperature(struct am2315 *device);

void am2315_humidity_callback_set(struct am2315 *device, void (*cb)(float humidity, bool success, void *data), void *data);

void am2315_read_humidity(struct am2315 *device);
