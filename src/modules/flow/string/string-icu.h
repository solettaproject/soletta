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

#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

#include "sol-flow.h"

UChar *string_replace(struct sol_flow_node *node, UChar *value, UChar *change_from, UChar *change_to, bool *replaced, size_t max_count);

int icu_str_from_utf8(const char *utf_str, UChar **ret_icu_str, UErrorCode *ret_icu_err);
