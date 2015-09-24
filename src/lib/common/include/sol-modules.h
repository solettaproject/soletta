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

#include "sol-common-buildopts.h"

#ifdef SOL_DYNAMIC_MODULES
/**
 * Fetches the given symbol whether it's built-in or from a module.
 *
 * Returns the address where @a _sym is stored. This may come from the library
 * itself if it was built-in, or loaded from the module @a _mod of type @a _ns.
 */
#define sol_symbol_get(_ns, _mod, _sym) sol_modules_get_symbol(_ns, _mod, #_sym)

/**
 * Returns the requested symbol, loading the respective module if needed.
 *
 * Checks if the @a symbol can be found in the library, returning its address
 * in that case. If not, it loads the module @a modname of type @a nspace and
 * tries to get the symbol there. Returns NULL on error or if the symbol
 * could not be found, win which case errno is set to ENOENT.
 *
 * The module will be loaded from the @a nspace sub-directory under the main
 * modules directory of the library. For example, if the library is installed
 * under @c /usr, and the @c console module of type @c flow is requested, the path
 * will be @c /usr/lib/soletta/modules/flow/console.so
 *
 * It is strongly recommended to avoid calling this function directly. Instead,
 * use the macro #sol_symbol_get().
 *
 * @param nspace The namespace under which to look for the module
 * @param modname The name of the module to load, if the symbol is not found
 *                built-in.
 * @param symbol The name of the symbol to look for.
 *
 * @return The address where the symbol is stored, or NULL on error.
 */
void *sol_modules_get_symbol(const char *nspace, const char *modname, const char *symbol);
#else
#define sol_symbol_get(_ns, _mod, _sym) (void *)&_sym
#endif
