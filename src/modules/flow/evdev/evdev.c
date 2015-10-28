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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "sol-flow/evdev.h"
#include "sol-buffer.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-monitors.h"
#include "sol-util.h"
#include "sol-util-file.h"
#include "sol-vector.h"

struct evdev_fd_handler {
    const struct sol_flow_node *node;
    struct sol_monitors monitors;
    const char *realpath;
    struct sol_fd *handler;
    int fd;
};

struct evdev_data {
    struct evdev_fd_handler *handler;
    uint16_t ev_type;
    uint16_t ev_code;
    int32_t value;
    bool on_press : 1;
    bool on_release : 1;
};

typedef void (*evdev_cb)(const struct sol_flow_node *, const struct input_event *);

static struct sol_ptr_vector evdev_handlers = SOL_PTR_VECTOR_INIT;
static struct sol_idle *evdev_idle_handler_check = NULL;
static bool evdev_check_handlers(void *data);

static void
evdev_event_handler(const struct sol_flow_node *node, const struct input_event *ev)
{
    struct evdev_data *mdata = sol_flow_node_get_private_data(node);

    if (ev->type != mdata->ev_type)
        return;
    if (ev->code != mdata->ev_code)
        return;

    /* ignore auto-repeat for now */
    if (ev->value == 2)
        return;

    mdata->value = ev->value;

    if ((ev->value && mdata->on_press) || (!ev->value && mdata->on_release))
        sol_flow_send_boolean_packet((struct sol_flow_node *)node,
            SOL_FLOW_NODE_TYPE_EVDEV_BOOLEAN__OUT__OUT,
            (bool)mdata->value);
}

static void
evdev_add_handler_check(void)
{
    if (evdev_idle_handler_check) return;
    evdev_idle_handler_check = sol_idle_add(evdev_check_handlers, NULL);
}

static bool
evdev_fd_handler_cb(void *data, int fd, unsigned int active_flags)
{
    struct evdev_fd_handler *fdh = data;
    bool retval = true;

    if (active_flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL))
        retval = false;

    while (retval) {
        struct input_event ev[8];
        ssize_t ret;
        int i, count;
        struct sol_buffer buffer = SOL_BUFFER_INIT_FLAGS(ev, sizeof(ev),
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

        ret = sol_util_fill_buffer(fd, &buffer, buffer.capacity);
        if (ret < 0) {
            retval = false;
            break;
        }

        count = buffer.used / sizeof(*ev);
        for (i = 0; i < count; i++) {
            struct sol_monitors_entry *e;
            uint16_t j;
            SOL_MONITORS_WALK (&fdh->monitors, e, j) {
                if (e->cb)
                    ((evdev_cb)e->cb)(e->data, &ev[i]);
            }
        }
    }

    if (!retval) {
        fdh->handler = NULL;
        close(fdh->fd);
        fdh->fd = -1;
        sol_flow_send_error_packet((struct sol_flow_node *)fdh->node, EIO, NULL);
    }
    evdev_add_handler_check();

    return retval;
}

#define BITS_PER_LONG   (sizeof(long) * 8)
#define NBITS(n)        ((((n) - 1) / BITS_PER_LONG) + 1)
#define OFF(x)          ((x) % BITS_PER_LONG)
#define BIT(x)          (1UL << OFF(x))
#define LONG(x)         ((x) / BITS_PER_LONG)
#define test_bit(b, a)  ((a[LONG(b)] >> OFF(b)) & 1)

static bool
evdev_supports_event(int fd, uint16_t ev_type, uint16_t ev_code)
{
    unsigned long bits[NBITS(KEY_MAX)];

    if (ioctl(fd, EVIOCGBIT(0, EV_MAX), bits) == -1)
        return false;
    if (!test_bit(ev_type, bits)) {
        errno = ENOTSUP;
        return false;
    }
    if (ioctl(fd, EVIOCGBIT(ev_type, KEY_MAX), bits) == -1)
        return false;
    if (!test_bit(ev_code, bits)) {
        errno = ENOTSUP;
        return false;
    }
    return true;
}

static struct evdev_fd_handler *
handler_evdev_do_open(const struct sol_flow_node *node, const char *rpath, uint16_t ev_type,  uint16_t ev_code)
{
    struct evdev_fd_handler *fdh;

    errno = 0;

    fdh = calloc(1, sizeof(*fdh));
    if (!fdh) {
        errno = ENOMEM;
        return NULL;
    }

    fdh->realpath = rpath;
    fdh->fd = open(rpath, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fdh->fd < 0) {
        goto open_err;
    }

    if (!evdev_supports_event(fdh->fd, ev_type, ev_code)) {
        errno = ENOTSUP;
        goto evdev_supports_err;
    }

    fdh->node = node;
    fdh->handler = sol_fd_add(fdh->fd, SOL_FD_FLAGS_IN, evdev_fd_handler_cb, fdh);
    if (!fdh->handler) {
        errno = ENOMEM;
        goto sol_fd_add_err;
    }

    sol_monitors_init(&fdh->monitors, NULL);

    return fdh;

sol_fd_add_err:
evdev_supports_err:
    close(fdh->fd);
open_err:
    free(fdh);
    return NULL;
}

static struct evdev_fd_handler *
handler_evdev_open(const struct sol_flow_node *node, const char *path, uint16_t ev_type, uint16_t ev_code)
{
    char *rpath;
    struct evdev_fd_handler *fdh;
    uint16_t i;

    rpath = realpath(path, NULL);
    if (!rpath)
        return NULL;

    if (evdev_handlers.base.data) {
        SOL_PTR_VECTOR_FOREACH_IDX (&evdev_handlers, fdh, i) {
            if (streq(fdh->realpath, rpath)) {
                free(rpath);
                if (!evdev_supports_event(fdh->fd, ev_type, ev_code)) {
                    errno = ENOTSUP;
                    return NULL;
                }
                return fdh;
            }
        }
    }

    fdh = handler_evdev_do_open(node, rpath, ev_type, ev_code);
    if (!fdh) {
        free(rpath);
        return NULL;
    }
    sol_ptr_vector_append(&evdev_handlers, fdh);

    return fdh;
}

static void
handler_evdev_do_close(struct evdev_fd_handler *fdh)
{
    sol_monitors_clear(&fdh->monitors);
    if (fdh->handler) sol_fd_del(fdh->handler);
    if (fdh->fd > -1) close(fdh->fd);
    free((void *)fdh->realpath);
    free(fdh);
}

static void
handler_evdev_close(struct evdev_fd_handler *fdh)
{
    struct evdev_fd_handler *itr;
    uint16_t i;

    if (sol_monitors_count(&fdh->monitors)) {
        evdev_add_handler_check();
        return;
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&evdev_handlers, itr, i) {
        if (itr == fdh) {
            sol_ptr_vector_del(&evdev_handlers, i);
            break;
        }
    }
    handler_evdev_do_close(fdh);
}

static bool
evdev_check_handlers(void *data)
{
    struct evdev_fd_handler *itr;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&evdev_handlers, itr, i) {
        if (!sol_monitors_count(&itr->monitors)) {
            sol_ptr_vector_del(&evdev_handlers, i);
            handler_evdev_do_close(itr);
        }
    }
    if (!evdev_handlers.base.len)
        sol_ptr_vector_clear(&evdev_handlers);
    evdev_idle_handler_check = NULL;
    return false;
}

static int
evdev_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct evdev_data *mdata = sol_flow_node_get_private_data(node);
    const struct sol_flow_node_type_evdev_boolean_options *opts =
        (const struct sol_flow_node_type_evdev_boolean_options *)options;

    if (opts->ev_code >= KEY_MAX)
        return -EINVAL;

    mdata->handler = handler_evdev_open(node, opts->path, EV_KEY, opts->ev_code);
    SOL_NULL_CHECK(mdata->handler, -EINVAL);

    if (!sol_monitors_append(&mdata->handler->monitors,
        (sol_monitors_cb_t)evdev_event_handler, node)) {
        handler_evdev_close(mdata->handler);
        return -EINVAL;
    }

    mdata->value = 0;

    mdata->ev_type = EV_KEY;
    mdata->ev_code = opts->ev_code;
    mdata->on_press = opts->on_press;
    mdata->on_release = opts->on_release;

    return 0;
}

static void
evdev_close(struct sol_flow_node *node, void *data)
{
    struct evdev_data *mdata = data;
    uint16_t idx;

    idx = sol_monitors_find(&mdata->handler->monitors,
        (sol_monitors_cb_t)evdev_event_handler, node);
    sol_monitors_del(&mdata->handler->monitors, idx);
    handler_evdev_close(mdata->handler);
}


#include "evdev-gen.c"
