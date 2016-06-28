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

#include "sol-buffer.h"
#include "sol-mainloop.h"

#ifndef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN &_string_format_log_domain
extern struct sol_log_domain _string_format_log_domain;
#endif

#include "sol-flow-internal.h"

int do_integer_markup(struct sol_flow_node *node, const char *format, struct sol_irange args, struct sol_buffer *out) SOL_ATTR_WARN_UNUSED_RESULT;
int do_float_markup(struct sol_flow_node *node, const char *format, struct sol_drange args, struct sol_buffer *out) SOL_ATTR_WARN_UNUSED_RESULT;

