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

#include <float.h>


#include "sol-util-internal.h"
#include "sol-log-internal.h"

#include "test-module.h"

#include "sol-flow/test.h"

SOL_LOG_INTERNAL_DECLARE(_test_log_domain, "flow-test");

#include "result.h"
#include "boolean-generator.h"
#include "boolean-validator.h"
#include "byte-validator.h"
#include "byte-generator.h"
#include "float-generator.h"
#include "float-validator.h"
#include "int-validator.h"
#include "int-generator.h"
#include "blob-validator.h"
#include "string-validator.h"
#include "string-generator.h"

#include "test-gen.c"
