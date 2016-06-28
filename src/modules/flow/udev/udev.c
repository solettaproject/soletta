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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libudev.h>

#include "sol-flow/udev.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-vector.h"
#include "sol-flow-internal.h"

struct udev_data {
    struct sol_flow_node *node;
    struct udev *udev;
    struct udev_monitor *monitor;
    struct sol_fd *watch;
    char *addr;
};

static bool
_on_event(void *data, int fd, uint32_t cond)
{
    struct udev_data *mdata = data;
    struct udev_device *device;
    bool event, inform = true;
    const char *action;

    if (cond & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP)) {
        SOL_WRN("Error with the monitor");
        mdata->watch = NULL;
        sol_flow_send_error_packet(mdata->node, EIO, NULL);
        return false;
    }

    device = udev_monitor_receive_device(mdata->monitor);
    if (device == NULL)
        return true;

    if (mdata->addr && !streq(udev_device_get_syspath(device), mdata->addr))
        goto end;

    action = udev_device_get_action(device);
    if (streq(action, "add"))
        event = true;
    else if (streq(action, "remove"))
        event = false;
    else
        inform = false;

    if (inform)
        sol_flow_send_bool_packet(mdata->node,
            SOL_FLOW_NODE_TYPE_UDEV_BOOLEAN__OUT__OUT,
            event);

end:
    udev_device_unref(device);
    return true;
}

static int
udev_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct udev_data *mdata = data;
    struct udev_device *device;
    bool value;
    const struct sol_flow_node_type_udev_boolean_options *opts =
        (const struct sol_flow_node_type_udev_boolean_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UDEV_BOOLEAN_OPTIONS_API_VERSION, -EINVAL);

    mdata->udev = udev_new();
    SOL_NULL_CHECK(mdata->udev, -EINVAL);

    mdata->monitor = udev_monitor_new_from_netlink(mdata->udev, "udev");
    if (!mdata->monitor) {
        SOL_WRN("Fail on create the udev monitor");
        goto monitor_error;
    }

    if (udev_monitor_enable_receiving(mdata->monitor) < 0) {
        SOL_WRN("error: unable to subscribe to udev events");
        goto receive_error;
    }

    mdata->addr = strdup(opts->address);

    mdata->node = node;
    mdata->watch = sol_fd_add(udev_monitor_get_fd(mdata->monitor),
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP,
        _on_event, mdata);

    device = udev_device_new_from_syspath(mdata->udev, mdata->addr);
    if (device) {
        value = true;
        udev_device_unref(device);
    } else {
        value = false;
    }

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_UDEV_BOOLEAN__OUT__OUT, value);

receive_error:
    mdata->monitor = udev_monitor_unref(mdata->monitor);
monitor_error:
    mdata->udev = udev_unref(mdata->udev);
    return -EINVAL;
}

static void
udev_close(struct sol_flow_node *node, void *data)
{
    struct udev_data *mdata = data;

    if (mdata->watch) {
        sol_fd_del(mdata->watch);
        mdata->watch = NULL;
    }

    mdata->monitor = udev_monitor_unref(mdata->monitor);
    mdata->udev = udev_unref(mdata->udev);
    free(mdata->addr);
}


#include "udev-gen.c"
