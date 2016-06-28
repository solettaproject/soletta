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

#ifdef SOL_LOG_ENABLED
extern struct sol_log_domain sol_fbp_log_domain;
void sol_fbp_init_log_domain(void);

#undef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN &sol_fbp_log_domain
#else

static inline void
sol_fbp_init_log_domain(void)
{
}

#endif
#include "sol-log-internal.h"
