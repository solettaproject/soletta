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

/*
 * Library loader wraps the functionality of using dlopen() / dlsym()
 * and caching the results.
 *
 * The function 'sol_lib_loader_new_in_rootdir()' should be preferred,
 * since it automatically prepends the correct path where Soletta was
 * installed.
 */

struct sol_lib_loader;

/* The parameter 'check_func' is called every time a symbol was
 * successfully loaded to decide whether it is valid or not. It can be
 * used to check versioning and perform initialization. */
struct sol_lib_loader *sol_lib_loader_new(
    const char *dir,
    const char *symbol_name,
    bool (*check_func)(const char *path, const char *symbol_name, void *symbol));

struct sol_lib_loader *sol_lib_loader_new_in_rootdir(
    const char *dir,
    const char *symbol_name,
    bool (*check_func)(const char *path, const char *symbol_name, void *symbol));

/* Will dlclose() all the libraries loaded. */
void sol_lib_loader_del(struct sol_lib_loader *loader);

/* Load a library and cache it so next calls are cheaper. Return the
 * symbol exported by that library. */
void *sol_lib_load(struct sol_lib_loader *loader, const char *name);
