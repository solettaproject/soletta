/*
 * This file is part of the Soletta Project
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

#include "device.h"

#include "sol-log.h"

static inline int
zephyr_err_to_errno(int z_err)
{
    const int table[] = {
        [DEV_OK] = 0,  /* No error */
        [DEV_FAIL] = -EIO, /* General operation failure */
        [DEV_INVALID_OP] = -EOPNOTSUPP, /* Invalid operation */
        [DEV_INVALID_CONF] = -EINVAL, /* Invalid configuration */
        [DEV_USED] = -EBUSY, /* Device controller in use */
        [DEV_NO_ACCESS] = -EACCES, /* Controller not accessible */
        [DEV_NO_SUPPORT] = -ENOTSUP, /* Device type not supported */
        [DEV_NOT_CONFIG] = -ENXIO /* Device not configured */
    };

    SOL_EXP_CHECK(z_err < 0
        || (unsigned)z_err > (sizeof(table) / sizeof(int)), -EINVAL);

    return table[z_err];
}
