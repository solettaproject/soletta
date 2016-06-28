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
#include <stdlib.h>
#include <string.h>

#include "sol-platform.h"
#include "sol-platform-impl.h"
#include "sol-util-internal.h"

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
    if (!strncasecmp(target, SOL_PLATFORM_TARGET_POWER_OFF, strlen(SOL_PLATFORM_TARGET_POWER_OFF))) {
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

#ifdef CPUID_LEN
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
#endif

int
sol_platform_impl_get_machine_id(char id[SOL_STATIC_ARRAY_SIZE(33)])
{
#ifdef CPUID_LEN
    char cpuid[CPUID_LEN];

    /* Assume, for now, the the cpuid we get is a valid UUID */
    cpuid_get(cpuid);
    serial_to_string(cpuid, CPUID_LEN, id);

    return 0;
#else
    return -ENOSYS;
#endif
}

int
sol_platform_impl_get_serial_number(char **number)
{
#ifdef CPUID_LEN
    char cpuid[CPUID_LEN];

    if (!number)
        return -EINVAL;

    *number = malloc(CPUID_LEN * 2 + 1);
    SOL_NULL_CHECK(*number, -ENOMEM);

    cpuid_get(cpuid);
    serial_to_string(cpuid, CPUID_LEN, *number);

    return 0;
#else
    return -ENOSYS;
#endif
}

int
sol_platform_impl_get_os_version(char **version)
{
    SOL_NULL_CHECK(version, -EINVAL);

    *version = strdup(RIOT_VERSION);
    if (!*version)
        return -ENOMEM;

    return 0;
}

int
sol_platform_impl_get_mount_points(struct sol_ptr_vector *vector)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_impl_umount(const char *mpoint, void (*cb)(void *data, const char *mpoint, int error), const void *data)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_unregister_hostname_monitor(void)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_register_hostname_monitor(void)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

const char *
sol_platform_impl_get_hostname(void)
{
    SOL_WRN("Not implemented");
    return NULL;
}

int
sol_platform_impl_set_hostname(const char *name)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_impl_set_system_clock(int64_t timestamp)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int64_t
sol_platform_impl_get_system_clock(void)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_unregister_system_clock_monitor(void)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_register_system_clock_monitor(void)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_impl_set_timezone(const char *timezone)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

const char *
sol_platform_impl_get_timezone(void)
{
    SOL_WRN("Not implemented");
    return NULL;
}

int
sol_platform_register_timezone_monitor(void)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_unregister_timezone_monitor(void)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_impl_set_locale(char **locales)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

const char *
sol_platform_impl_get_locale(enum sol_platform_locale_category type)
{
    SOL_WRN("Not implemented");
    return NULL;
}

int
sol_platform_register_locale_monitor(void)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_unregister_locale_monitor(void)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_impl_apply_locale(enum sol_platform_locale_category type, const char *locale)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_impl_load_locales(char **locale_cache)
{
    SOL_WRN("Not implemented");
    return 0;
}

int
sol_platform_impl_locale_to_c_category(enum sol_platform_locale_category category)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

const char *
sol_platform_impl_locale_to_c_str_category(enum sol_platform_locale_category category)
{
    SOL_WRN("Not implemented");
    return NULL;
}
