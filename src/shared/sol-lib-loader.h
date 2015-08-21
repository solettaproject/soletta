/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

/* The parameter 'check_func' is called everytime a symbol was
 * sucessfully loaded to decide whether it is valid or not. It can be
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
