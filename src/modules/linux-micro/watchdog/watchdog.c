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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/watchdog.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-watchdog");

#include "sol-platform-linux-micro.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"

static int watchdog_fd = -1;
static struct sol_timeout *watchdog_timeout;
static const char *service_name;
#define WATCHDOG_TIMEOUT_DEFAULT_SECS 60


static bool
watchdog_keep_alive(void *data)
{
    int reply, err;

    SOL_DBG("keep watchdog alive");
    err = ioctl(watchdog_fd, WDIOC_KEEPALIVE, &reply);
    if (reply != WDIOF_KEEPALIVEPING)
        SOL_WRN("unexpected watchdog keepalive reply=%#x, expected=%#x. Ignored.",
            reply, WDIOF_KEEPALIVEPING);
    if (err == 0)
        return true;

    SOL_WRN("failed to keep watchdog alive: %s", sol_util_strerrora(errno));
    close(watchdog_fd);
    watchdog_fd = -1;
    watchdog_timeout = NULL;
    sol_platform_linux_micro_inform_service_state(service_name, SOL_PLATFORM_SERVICE_STATE_FAILED);
    return false;
}

static void
watchdog_show_info_flags(const char *msg, uint32_t flags, uint32_t options)
{
    if (SOL_LOG_LEVEL_POSSIBLE(SOL_LOG_LEVEL_DEBUG)) {
        static const struct watchdog_flag_map {
            uint32_t flag;
            const char *desc;
        } *itr, *itr_end, map[] = {
            { WDIOF_OVERHEAT, "Reset due to CPU overheat" },
            { WDIOF_FANFAULT, "Fan failed" },
            { WDIOF_EXTERN1, "External relay 1" },
            { WDIOF_EXTERN2, "External relay 2" },
            { WDIOF_POWERUNDER, "Power bad/power fault" },
            { WDIOF_CARDRESET, "Card previously reset the CPU" },
            { WDIOF_POWEROVER, "Power over voltage" },
        };

        SOL_DBG("watchdog status %s: flags=%#x, options=%#x",
            msg, flags, options);

        itr = map;
        itr_end = itr + sol_util_array_size(map);
        for (; itr < itr_end; itr++) {
            if (itr->flag & options) {
                if (itr->flag & flags)
                    SOL_WRN("%s: %s", msg, itr->desc);
            }
        }
    }
}

static void
watchdog_show_info(void)
{
    if (SOL_LOG_LEVEL_POSSIBLE(SOL_LOG_LEVEL_DEBUG)) {
        struct watchdog_info ident;
        uint32_t options = 0, flags = 0;
        int err;

        err = ioctl(watchdog_fd, WDIOC_GETSUPPORT, &ident);
        if (err == 0) {
            SOL_DBG("watchdog identity '%.32s' firmware_version=%u options=%#x",
                ident.identity,
                ident.firmware_version,
                ident.options);
            options = ident.options;
        }

        err = ioctl(watchdog_fd, WDIOC_GETSTATUS, &flags);
        if (err == 0)
            watchdog_show_info_flags("Current", flags, options);

        err = ioctl(watchdog_fd, WDIOC_GETBOOTSTATUS, &flags);
        if (err == 0)
            watchdog_show_info_flags("Last Reboot", flags, options);

        err = ioctl(watchdog_fd, WDIOC_GETTEMP, &flags);
        if (err == 0)
            SOL_DBG("Temperature %d fahrenheit", flags);
    }
}

static int
watchdog_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    int err = 0;
    int timeout = 60;
    uint32_t timeout_ms;

    if (watchdog_fd >= 0)
        return 0;

    service_name = service;

    watchdog_fd = open("/dev/watchdog", O_CLOEXEC | O_WRONLY);
    if (watchdog_fd < 0) {
        err = -errno;
        SOL_WRN("could not open /dev/watchdog: %s", sol_util_strerrora(errno));
        goto end;
    }

    if (ioctl(watchdog_fd, WDIOC_GETTIMEOUT, &timeout) < 0 || timeout < 1) {
        timeout = WATCHDOG_TIMEOUT_DEFAULT_SECS;
        SOL_WRN("could not query watchdog timeout, use %ds", timeout);

        if (ioctl(watchdog_fd, WDIOC_SETTIMEOUT, &timeout) < 0) {
            SOL_WRN("could not set watchdog timeout to default %ds: %s. Ignored",
                timeout, sol_util_strerrora(errno));
        }
    }

    if (timeout > 5)
        timeout_ms = (uint32_t)(timeout - 5) * 1000U;
    else
        timeout_ms = (uint32_t)timeout * 900U;

    watchdog_timeout = sol_timeout_add(timeout_ms, watchdog_keep_alive, NULL);
    if (!watchdog_timeout) {
        err = -ENOMEM;
        SOL_WRN("could not create watchdog_timeout");
        close(watchdog_fd);
        watchdog_fd = -1;
        goto end;
    }

    watchdog_show_info();

end:
    if (err == 0)
        sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_ACTIVE);
    else
        sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_FAILED);

    return err;
}

static int
watchdog_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

static int
watchdog_restart(const struct sol_platform_linux_micro_module *module, const char *service)
{
    sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_ACTIVE);
    return 0;
}

static int
watchdog_stop(const struct sol_platform_linux_micro_module *module, const char *service, bool force_immediate)
{
    if (watchdog_fd < 0)
        return 0;

    sol_timeout_del(watchdog_timeout);
    watchdog_timeout = NULL;

    close(watchdog_fd);
    watchdog_fd = -1;

    sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_INACTIVE);
    service_name = NULL;
    return 0;
}

SOL_PLATFORM_LINUX_MICRO_MODULE(WATCHDOG,
    .name = "watchdog",
    .init = watchdog_init,
    .start = watchdog_start,
    .restart = watchdog_restart,
    .stop = watchdog_stop,
    );
