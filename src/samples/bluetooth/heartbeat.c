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

#define UUID_HRS 0x180d
#define UUID_HRS_MEASUREMENT 0x2a37

static struct sol_bt_session *session;
static struct sol_timeout *timeout;

static int
hrs_measurement_read(struct sol_gatt_pending *op, uint16_t offset)
{
    struct sol_buffer buf;
    uint8_t pdu[2] = { 0x06 };
    int r;

    /* FIXME */
    pdu[1] = 60;

    sol_buffer_init_flags(&buf, pdu, sizeof(pdu),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

    buf.used = sizeof(pdu);

    r = sol_gatt_pending_reply(op, 0, &buf);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static struct sol_gatt_attr attrs[] = {
    SOL_GATT_SERVICE_UUID_16(UUID_HRS),
    SOL_GATT_CHARACTERISTIC_UUID_16(UUID_HRS_MEASUREMENT,
        SOL_GATT_CHR_FLAGS_READ | SOL_GATT_CHR_FLAGS_INDICATE,
        .read = hrs_measurement_read),
    SOL_GATT_ATTR_INVALID,
};

static bool
timeout_cb(void *data)
{
    int r;

    r = sol_gatt_notify(NULL, &attrs[1]);
    SOL_INT_CHECK(r, < 0, false);

    return true;
}

static void
enabled(void *data, bool powered)
{
    int r;

    if (!powered)
        return;

    SOL_INF("Bluetooth Adapter enabled");

    r = sol_gatt_register_attributes(attrs);
    SOL_INT_CHECK(r, < 0);

    timeout = sol_timeout_add(5 * 1000, timeout_cb, NULL);
    SOL_NULL_CHECK(timeout);
}

static void
shutdown(void)
{
    sol_gatt_unregister_attributes(attrs);

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
