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

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "sol-util-internal.h"

#define SOL_LOG_DOMAIN &sol_fbp_log_domain
#include "sol-fbp-internal-log.h"
SOL_LOG_INTERNAL_DECLARE(sol_fbp_log_domain, "fbp");

void
sol_fbp_init_log_domain(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
}
