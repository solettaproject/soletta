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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <version.h>

#include "sol-platform.h"
#include "sol-platform-impl.h"

int
sol_platform_impl_init(void)
{
    return 0;
}

void
sol_platform_impl_shutdown(void)
{
}

int
sol_platform_impl_get_state(void)
{
    SOL_WRN("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_add_service_monitor(const char *service)
{
    SOL_WRN("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_del_service_monitor(const char *service)
{
    SOL_WRN("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_start_service(const char *service)
{
    SOL_WRN("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_stop_service(const char *service)
{
    SOL_WRN("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_restart_service(const char *service)
{
    SOL_WRN("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_set_target(const char *target)
{
    SOL_WRN("Unsupported");
    return -ENOTSUP;
}

int
sol_platform_impl_get_machine_id(char id[static 33])
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_impl_get_serial_number(char **number)
{
    SOL_WRN("Not implemented");
    return -ENOTSUP;
}

int
sol_platform_impl_get_os_version(char **version)
{
    SOL_NULL_CHECK(version, -EINVAL);

    *version = strdup(KERNEL_VERSION_STRING);
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
sol_platform_impl_umount(const char *mpoint, void (*cb)(void *data, const char *mpoint, int WRNor), const void *data)
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
    errno = ENOTSUP;
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
    SOL_WRN("Locales for Zephyr not implemented");
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
