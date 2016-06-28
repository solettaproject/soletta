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

#include "sol-log.h"

extern struct sol_log_domain _global_domain;
extern uint8_t _abort_level;
extern bool _show_colors;
extern bool _show_file;
extern bool _show_function;
extern bool _show_line;
extern void (*_print_function)(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args);
extern const void *_print_function_data;

int sol_log_impl_init(void);
void sol_log_impl_shutdown(void);
bool sol_log_impl_lock(void);
void sol_log_impl_unlock(void);
bool sol_log_level_parse(const char *str, size_t size, uint8_t *storage);
bool sol_log_levels_parse(const char *str, size_t size);
void sol_log_impl_print_function_stderr(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args);
