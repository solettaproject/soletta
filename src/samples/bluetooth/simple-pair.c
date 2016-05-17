/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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
#include <stdio.h>
#include <string.h>

#include "soletta.h"
#include "sol-bluetooth.h"
#include "sol-gatt.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-util.h"

static struct sol_bt_scan_pending *scan;
static struct sol_bt_session *session;

static struct sol_bt_conn *auth_conn;
static struct sol_network_link_addr pair_addr;

static struct sol_timeout *timeout;

static void
on_error(void *user_data, int error)
{
    SOL_DBG("error %d", error);

    auth_conn = NULL;
}

static void
paired_callback(void *user_data, bool success, struct sol_bt_conn *conn)
{
    const struct sol_bt_device_info *info = sol_bt_conn_get_device_info(conn);

    printf("Paired with %s\n", info->name);
}

static bool
on_connect(void *user_data, struct sol_bt_conn *conn)
{
    SOL_BUFFER_DECLARE_STATIC(str, SOL_BLUETOOTH_ADDR_STRLEN);
    int r;

    sol_network_link_addr_to_str(sol_bt_conn_get_addr(conn), &str);

    printf("Connected to device %.*s\n", (int)str.used, (char *)str.data);

    r = sol_bt_conn_pair(conn, paired_callback, NULL);
    SOL_INT_CHECK(r, < 0, false);

    return true;
}

static void
on_disconnect(void *user_data, struct sol_bt_conn *conn)
{
    SOL_BUFFER_DECLARE_STATIC(str, SOL_BLUETOOTH_ADDR_STRLEN);

    sol_network_link_addr_to_str(sol_bt_conn_get_addr(conn), &str);

    printf("Disconnected from device %.*s\n", (int)str.used, (char *)str.data);

    auth_conn = NULL;
}

static bool
on_timeout(void *data)
{
    auth_conn = sol_bt_connect(&pair_addr, on_connect, on_disconnect,
        on_error, NULL);
    SOL_NULL_CHECK(auth_conn, false);

    return false;
}

static void
on_found_device(void *user_data, const struct sol_bt_device_info *device)
{
    SOL_BUFFER_DECLARE_STATIC(str, SOL_BLUETOOTH_ADDR_STRLEN);
    const char *addr;

    addr = sol_network_link_addr_to_str(&device->addr, &str);
    SOL_NULL_CHECK(addr);

    printf("device %.*s in range %s\n", (int)str.used, (char *)str.data,
        device->in_range ? "yes" : "no");

    if (pair_addr.family != SOL_NETWORK_FAMILY_UNSPEC) {
        if (!sol_network_link_addr_eq(&pair_addr, &device->addr))
            return;

        sol_bt_stop_scan(scan);
        scan = NULL;

        timeout = sol_timeout_add(500, on_timeout, NULL);
    }
}

static void
pairing_confirm(void *data, struct sol_bt_conn *conn)
{
    if (auth_conn != conn) {
        sol_bt_agent_finish_cancel(conn);
        return;
    }

    sol_bt_agent_finish_pairing_confirm(conn);
}

static struct sol_bt_agent agent = {
    .pairing_confirm = pairing_confirm,
};

static void
on_enabled(void *data, bool powered)
{
    int r;

    if (!powered)
        return;

    SOL_INF("Bluetooth Adapter enabled");

    scan = sol_bt_start_scan(SOL_BT_TRANSPORT_ALL, on_found_device, NULL);
    SOL_NULL_CHECK(scan);

    r = sol_bt_register_agent(&agent, NULL);
    if (r < 0) {
        SOL_WRN("%s (%d) < 0", sol_util_strerrora(-r), r);
        sol_quit_with_code(EXIT_FAILURE);
    }
}

static void
shutdown(void)
{
    if (auth_conn)
        sol_bt_conn_unref(auth_conn);

    if (scan)
        sol_bt_stop_scan(scan);

    if (session)
        sol_bt_disable(session);

    sol_bt_register_agent(NULL, NULL);
}

static void
startup(void)
{
    if (sol_argc() > 1) {
        const struct sol_network_link_addr *addr;

        addr = sol_network_link_addr_from_str(&pair_addr, sol_argv()[1]);
        SOL_NULL_CHECK(addr);
    }

    session = sol_bt_enable(on_enabled, NULL);
    if (!session) {
        SOL_WRN("Couldn't create a Bluetooth session");
        sol_quit_with_code(EXIT_FAILURE);
    }
}

SOL_MAIN_DEFAULT(startup, shutdown);
