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
#include "sol-gatt.h"

static struct sol_bt_scan_pending *scan;
static struct sol_bt_session *session;

static struct sol_bt_conn *browse_conn;
static struct sol_network_link_addr browse_addr;

static struct sol_timeout *timeout;

static void
on_error(void *user_data, int error)
{
    SOL_DBG("error %d", error);

    browse_conn = NULL;
}

static bool
notify_callback(void *user_data,
    const struct sol_gatt_attr *attr, const struct sol_buffer *buffer)
{
    SOL_INF("attr %p update %zu bytes", attr, buffer->used);

    return true;
}

static bool
print_attr(void *user_data, struct sol_bt_conn *conn,
    const struct sol_gatt_attr *attr)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    int r;

    if (!conn || !attr) {
        return false;
    }

    r = sol_bt_uuid_to_str(&attr->uuid, &buf);
    SOL_INT_CHECK_GOTO(r, < 0, done);

    SOL_INF("type %d uuid %.*s flags %#x", attr->type, (int)buf.used,
        (char *)buf.data, attr->flags);

    if (attr->type != SOL_GATT_ATTR_TYPE_CHARACTERISTIC)
        goto done;

    if (!(attr->flags & (SOL_GATT_CHR_FLAGS_NOTIFY | SOL_GATT_CHR_FLAGS_INDICATE)))
        goto done;

    r = sol_gatt_subscribe(conn, attr, notify_callback, NULL);
    SOL_INT_CHECK_GOTO(r, < 0, done);

done:
    sol_buffer_fini(&buf);

    if (r)
        return false;

    return true;

}
static bool
on_connect(void *user_data, struct sol_bt_conn *conn)
{
    SOL_BUFFER_DECLARE_STATIC(str, SOL_BLUETOOTH_ADDR_STRLEN);
    int r;

    sol_network_link_addr_to_str(sol_bt_conn_get_addr(conn), &str);

    SOL_INF("Connected to device %.*s", (int)str.used, (char *)str.data);

    r = sol_gatt_discover(conn, 0, NULL, NULL, print_attr, NULL);
    SOL_INT_CHECK(r, < 0, false);

    return true;
}

static void
on_disconnect(void *user_data, struct sol_bt_conn *conn)
{
    SOL_BUFFER_DECLARE_STATIC(str, SOL_BLUETOOTH_ADDR_STRLEN);

    sol_network_link_addr_to_str(sol_bt_conn_get_addr(conn), &str);

    SOL_INF("Disconnected from device %.*s", (int)str.used, (char *)str.data);

    browse_conn = NULL;
}

static bool
timeout_cb(void *data)
{
    browse_conn = sol_bt_connect(&browse_addr, on_connect, on_disconnect,
        on_error, NULL);
    SOL_NULL_CHECK(browse_conn, false);

    return false;
}

static void
found_device(void *user_data, const struct sol_bt_device_info *device)
{
    SOL_BUFFER_DECLARE_STATIC(str, SOL_BLUETOOTH_ADDR_STRLEN);
    const char *addr;

    addr = sol_network_link_addr_to_str(&device->addr, &str);
    SOL_NULL_CHECK(addr);

    SOL_INF("device %.*s in range %s", (int)str.used, (char *)str.data,
        device->in_range ? "yes" : "no");

    if (!device->in_range)
        return;

    if (browse_addr.family != SOL_NETWORK_FAMILY_UNSPEC) {
        if (!sol_network_link_addr_eq(&browse_addr, &device->addr))
            return;

        sol_bt_stop_scan(scan);
        scan = NULL;

        timeout = sol_timeout_add(500, timeout_cb, NULL);
    }
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
    if (browse_conn)
        sol_bt_conn_unref(browse_conn);

    if (scan)
        sol_bt_stop_scan(scan);

    if (session)
        sol_bt_disable(session);
}

static void
startup(void)
{
    if (sol_argc() > 1) {
        const struct sol_network_link_addr *addr;

        addr = sol_network_link_addr_from_str(&browse_addr, sol_argv()[1]);
        SOL_NULL_CHECK(addr);
    }

    session = sol_bt_enable(enabled, NULL);
    if (!session) {
        SOL_WRN("Couldn't create a Bluetooth session");
        sol_quit_with_code(-ENOMEM);
    }
}

SOL_MAIN_DEFAULT(startup, shutdown);
