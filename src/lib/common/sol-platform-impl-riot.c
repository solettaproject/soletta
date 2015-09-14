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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "sol-platform.h"
#include "sol-platform-impl.h"
#include "sol-util.h"

#include "lpm.h"
#include "periph/cpuid.h"

int
sol_platform_impl_init(void)
{
    return 0;
}

void
sol_platform_impl_shutdown(void)
{
    lpm_set(LPM_POWERDOWN);
}

int
sol_platform_impl_get_state(void)
{
    switch (lpm_get()) {
    case LPM_POWERDOWN:
    case LPM_SLEEP:
        return SOL_PLATFORM_STATE_STOPPING;
    case LPM_UNKNOWN:
        return SOL_PLATFORM_STATE_UNKNOWN;
    default:
        return SOL_PLATFORM_STATE_RUNNING;
    }
}

int
sol_platform_impl_add_service_monitor(const char *service)
{
    SOL_CRI("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_del_service_monitor(const char *service)
{
    SOL_CRI("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_start_service(const char *service)
{
    SOL_CRI("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_stop_service(const char *service)
{
    SOL_CRI("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_restart_service(const char *service)
{
    SOL_CRI("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_set_target(const char *target)
{
    if (!strncasecmp(target, SOL_PLATFORM_TARGET_POWEROFF, strlen(SOL_PLATFORM_TARGET_POWEROFF))) {
        lpm_set(LPM_POWERDOWN);
        return 0;
    }
    if (!strncasecmp(target, SOL_PLATFORM_TARGET_SUSPEND, strlen(SOL_PLATFORM_TARGET_SUSPEND))) {
        lpm_set(LPM_SLEEP);
        return 0;
    }
    if (!strncasecmp(target, SOL_PLATFORM_TARGET_DEFAULT, strlen(SOL_PLATFORM_TARGET_DEFAULT))) {
        lpm_set(LPM_ON);
        return 0;
    }
    SOL_CRI("Unsupported set target %s.", target);
    return -ENOTSUP;
}

static inline char
to_hex(int num)
{
    return num > 9 ? num - 10 + 'a' : num + '0';
}

static void
serial_to_string(const char *buf, size_t len, char *dst)
{
    char *ptr;
    int i;

    for (ptr = dst, i = 0; i < len; i++) {
        int h, l;

        h = ((unsigned)buf[i] & 0xf0) >> 4;
        l = buf[i] & 0x0f;
        *(ptr++) = to_hex(h);
        *(ptr++) = to_hex(l);
    }
    *ptr = 0;
}

int
sol_platform_impl_get_machine_id(char id[static 33])
{
#ifdef CPUID_ID_LEN
    char cpuid[CPUID_ID_LEN];

    /* Assume, for now, the the cpuid we get is a valid UUID */
    cpuid_get(cpuid);
    serial_to_string(cpuid, CPUID_ID_LEN, id);

    return 0;
#else
    return -ENOSYS;
#endif
}

int
sol_platform_impl_get_serial_number(char **number)
{
#ifdef CPUID_ID_LEN
    char cpuid[CPUID_ID_LEN];

    if (!number)
        return -EINVAL;

    *number = malloc(CPUID_ID_LEN * 2 + 1);
    SOL_NULL_CHECK(*number, -ENOMEM);

    cpuid_get(cpuid);
    serial_to_string(cpuid, CPUID_ID_LEN, *number);

    return 0;
#else
    return -ENOSYS;
#endif
}

char *
sol_platform_impl_get_os_version(void)
{
    return strdup(RIOT_VERSION);
}
