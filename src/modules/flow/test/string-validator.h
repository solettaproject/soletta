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

#include "test-module.h"
#include "sol-vector.h"

struct string_validator_data {
    bool done;
    char *sequence;
    uint16_t next_index;
    struct sol_vector values;
};

DECLARE_OPEN_FUNCTION(string_validator_open);
DECLARE_CLOSE_FUNCTION(string_validator_close);
DECLARE_PROCESS_FUNCTION(string_validator_process);
