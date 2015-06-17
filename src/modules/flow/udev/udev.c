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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libudev.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-udev");

#include "udev-gen.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"

struct udev_data {
    struct sol_flow_node *node;
    struct udev *udev;
    struct udev_monitor *monitor;
    struct sol_fd *watch;
    char *addr;
};

static bool
_on_event(void *data, int fd, unsigned int cond)
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
        sol_flow_send_boolean_packet(mdata->node,
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

    return sol_flow_send_boolean_packet(node,
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
