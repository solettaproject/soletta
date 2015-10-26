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

#include <linux/netlink.h>
#include <sys/socket.h>

#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-platform-linux-micro.h"
#include "sol-uevent.h"
#include "sol-util.h"

#define LIBUDEV_ID "libudev"

struct callback {
    const char *action;
    const char *subsystem;
    const void *cb_data;
    void (*uevent_cb)(void *cb_data, struct sol_uevent *uevent);
};

struct context {
    bool running;
    struct sol_vector callbacks;
    struct {
        int fd;
        struct sol_fd *watch;
    } uevent;
};

static struct context _ctx;

static void
sol_uevent_event_dispatch(struct context *ctx, struct sol_uevent *uevent)
{
    uint16_t idx;
    struct callback *cb;

    SOL_VECTOR_FOREACH_IDX (&ctx->callbacks, cb, idx) {
        if ((!cb->action || (cb->action && sol_str_slice_str_eq(uevent->action, cb->action))) &&
            (!cb->subsystem || (cb->subsystem && sol_str_slice_str_eq(uevent->subsystem, cb->subsystem)))) {
            cb->uevent_cb((void *)cb->cb_data, uevent);
        }
    }
}

static void
sol_uevent_read_msg(struct context *ctx, char *msg, int len)
{
    struct sol_uevent uevent = {
        .modalias = SOL_STR_SLICE_EMPTY,
        .action = SOL_STR_SLICE_EMPTY,
        .subsystem = SOL_STR_SLICE_EMPTY,
        .devtype = SOL_STR_SLICE_EMPTY,
        .devname = SOL_STR_SLICE_EMPTY,
    };
    int i = 0;

    /** lets avoid it to misbehave when people trying to test it with systemd running */
    if (!strncmp(msg, LIBUDEV_ID, sizeof(LIBUDEV_ID))) {
        SOL_INF("We're running side-by-side with udevd, skipping udevd generated event");
        return;
    }

    while (i < len) {
        char *curr, *del;
        size_t value_len;

        curr = msg + i;
        del = strrchr(curr, '=');
        value_len = del - curr;

        if (!strncmp(curr, "MODALIAS", value_len))
            uevent.modalias = sol_str_slice_from_str(++del);
        else if (!strncmp(curr, "ACTION", value_len))
            uevent.action = sol_str_slice_from_str(++del);
        else if (!strncmp(curr, "SUBSYSTEM", value_len))
            uevent.subsystem = sol_str_slice_from_str(++del);
        else if (!strncmp(curr, "DEVTYPE", value_len))
            uevent.devtype = sol_str_slice_from_str(++del);
        else if (!strncmp(curr, "DEVNAME", value_len))
            uevent.devname = sol_str_slice_from_str(++del);

        i += strlen(msg + i) + 1;
    }

    sol_uevent_event_dispatch(ctx, &uevent);
}

static bool
sol_uevent_handler(void *data, int fd, unsigned int cond)
{
    int len;
    struct context *ctx = data;
    char buffer[512];

    len = recv(ctx->uevent.fd, buffer, sizeof(buffer), MSG_WAITALL);
    if (len == -1) {
        SOL_ERR("Could not read netlink socket. %s", sol_util_strerrora(errno));
        return false;
    } else {
        sol_uevent_read_msg(ctx, buffer, len);
    }

    return true;
}

static int
sol_uevent_register(struct context *ctx)
{
    struct sockaddr_nl nls;

    memset(&nls, 0, sizeof(struct sockaddr_nl));
    nls.nl_family = AF_NETLINK;
    nls.nl_pid = getpid();
    nls.nl_groups = -1;

    ctx->uevent.fd = socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC,
        NETLINK_KOBJECT_UEVENT);
    if (ctx->uevent.fd == -1) {
        SOL_ERR("Could not open uevent netlink socket.");
        return -errno;
    }

    if (bind(ctx->uevent.fd, (void *)&nls, sizeof(struct sockaddr_nl))) {
        SOL_ERR("Could not bind to uevent socket.");
        close(ctx->uevent.fd);
        ctx->uevent.fd = 0;
        return -errno;
    }

    ctx->uevent.watch = sol_fd_add(ctx->uevent.fd, SOL_FD_FLAGS_IN,
        sol_uevent_handler, ctx);
    return 0;
}

int
sol_uevent_subscribe_events(const char *action, const char *subsystem,
    void (*uevent_cb)(void *cb_data, struct sol_uevent *uevent),
    const void *cb_data)
{
    struct callback *cb;

    if (!_ctx.running)
        sol_vector_init(&_ctx.callbacks, sizeof(struct callback));

    cb = sol_vector_append(&_ctx.callbacks);
    SOL_NULL_CHECK(cb, -1);

    cb->action = action;
    cb->subsystem = subsystem;
    cb->uevent_cb = uevent_cb;
    cb->cb_data = cb_data;

    if (!_ctx.running) {
        sol_uevent_register(&_ctx);
    }

    _ctx.running = true;
    return 0;
}

static void
sol_uevent_cleaup(struct context *ctx)
{
    if (ctx->uevent.watch)
        sol_fd_del(ctx->uevent.watch);

    if (ctx->uevent.fd)
        close(ctx->uevent.fd);

    sol_vector_clear(&ctx->callbacks);
    ctx->running = false;
}

int
sol_uevent_unsubscribe_events(const char *action, const char *subsystem,
    void (*uevent_cb)(void *cb_data, struct sol_uevent *uevent))
{
    struct callback *callback;
    uint16_t idx;
    int res = -1;

    SOL_NULL_CHECK(uevent_cb, -1);

    SOL_VECTOR_FOREACH_REVERSE_IDX (&_ctx.callbacks, callback, idx) {
        if (callback->uevent_cb == uevent_cb) {
            sol_vector_del(&_ctx.callbacks, idx);
            res = 0;
        }
    }

    if (!_ctx.callbacks.len)
        sol_uevent_cleaup(&_ctx);

    return res;
}
