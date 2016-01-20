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
