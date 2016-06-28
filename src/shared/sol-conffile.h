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
#include "sol-str-slice.h"

/* This conffile resolve is used on resolver-conffile and sol-fbp-generator */

int sol_conffile_resolve(const char *id, const char **type, const char ***opts);

int sol_conffile_resolve_path(const char *id, const char **type, const char ***opts, const char *path);

int sol_conffile_resolve_memmap(struct sol_ptr_vector **memmaps);

int sol_conffile_resolve_memmap_path(struct sol_ptr_vector **memmaps, const char *path);

const char *sol_conffile_resolve_alias(const struct sol_str_slice alias);
