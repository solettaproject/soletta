/*
 * This file is part of the Soletta (TM) Project
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
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-bluetooth.h"

static struct sol_bt_scan_pending *scan;
static struct sol_bt_session *session;

static struct sol_bt_conn *paired_device_conn;
static struct sol_network_link_addr paired_device_addr;

static void
on_error(void *user_data, int error)
{
    paired_device_conn = NULL;
}

static bool
on_connect(void *user_data, struct sol_bt_conn *conn)
{
    SOL_BUFFER_DECLARE_STATIC(str, SOL_BLUETOOTH_ADDR_STRLEN);

    sol_network_link_addr_to_str(sol_bt_conn_get_addr(conn), &str);

    SOL_INF("Connected to device %.*s", (int)str.used, (char *)str.data);

    return true;
}

static void
on_disconnect(void *user_data, struct sol_bt_conn *conn)
{
    SOL_BUFFER_DECLARE_STATIC(str, SOL_BLUETOOTH_ADDR_STRLEN);

    sol_network_link_addr_to_str(sol_bt_conn_get_addr(conn), &str);

    SOL_INF("Disconnected from device %.*s, trying again", (int)str.used, (char *)str.data);
}

static void
found_device(void *user_data, const struct sol_bt_device_info *device)
{
    if (paired_device_conn)
        return;

    if (!device->paired || !device->in_range)
        return;

    memcpy(&paired_device_addr, &device->addr, sizeof(paired_device_addr));

    paired_device_conn = sol_bt_connect(&device->addr, on_connect, on_disconnect,
        on_error, NULL);
    SOL_NULL_CHECK(paired_device_conn);

    sol_bt_stop_scan(scan);
    scan = NULL;
}

static void
enabled(void *data, bool powered)
{
    if (!powered)
        return;

    SOL_INF("Bluetooth Adapter enabled");

    scan = sol_bt_start_scan(SOL_BT_TRANSPORT_ALL, found_device, NULL);
    SOL_NULL_CHECK(scan);
}

static void
shutdown(void)
{
    if (paired_device_conn)
        sol_bt_disconnect(paired_device_conn);

    if (scan)
        sol_bt_stop_scan(scan);

    if (session)
        sol_bt_disable(session);
}

static void
startup(void)
{
    session = sol_bt_enable(enabled, NULL);
    if (!session) {
        SOL_WRN("Couldn't create a Bluetooth session");
        sol_quit_with_code(-ENOMEM);
    }
}

SOL_MAIN_DEFAULT(startup, shutdown);
