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

#ifdef MODULES

/*
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

#endif /* MODULES */

void sol_modules_clear_cache(void);
