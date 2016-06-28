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

#include "sol-vector.h"

struct runner;

struct runner *runner_new_from_file(const char *filename, const char **options_strv, struct sol_ptr_vector *fbps);
struct runner *runner_new_from_type(const char *typename, const char **options_strv);

int runner_attach_simulation(struct runner *r);
int runner_run(struct runner *r);
void runner_del(struct runner *r);
