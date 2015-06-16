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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <fcntl.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-keyboard");

#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-vector.h"
#include "sol-util.h"

#include "keyboard-gen.h"

struct keyboard_common_data {
    struct sol_flow_node *node;
    uint64_t last_code, last_read_code;
    void (*on_code)(struct keyboard_common_data *mdata,
        unsigned char *buf,
        size_t len);
};

struct keyboard_boolean_data {
    struct keyboard_common_data common;
    uint64_t binary_code;
    bool toggle;
};

struct keyboard_irange_data {
    struct keyboard_common_data common;
};

static bool keyboard_done = false;
static struct termios keyboard_termios;
static int keyboard_users_walking = 0;
static int keyboard_users_pending_deletion = 0;
static struct sol_ptr_vector keyboard_users = SOL_PTR_VECTOR_INIT;
static struct sol_ptr_vector keyboard_types = SOL_PTR_VECTOR_INIT;
static struct sol_fd *keyboard_watch;

static bool
keyboard_termios_setup(void)
{
    struct termios tio;

    SOL_DBG("setup termios in raw mode");
    memcpy(&tio, &keyboard_termios, sizeof(tio));

    tio.c_iflag &= ~(ISTRIP | INLCR | ICRNL | IGNCR | IXON | IXOFF);
    tio.c_lflag &= ~(ECHO | ICANON);

    tio.c_cc[VTIME] = 0;
    tio.c_cc[VMIN] = 0;
    tcflush(STDIN_FILENO, TCIFLUSH);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &tio) < 0) {
        SOL_WRN("could not setup termios in raw mode: %s", sol_util_strerrora(errno));
        return false;
    }

    return true;
}

static void
keyboard_termios_reset(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW,
        &keyboard_termios);
}

static void
keyboard_users_cleanup(void)
{
    struct keyboard_data *mdata;
    uint16_t i;

    if (keyboard_users_walking > 0)
        return;

    // Walk backwards so deletion doesn't impact the indices.
    if (keyboard_users_pending_deletion > 0) {
        SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&keyboard_users, mdata, i) {
            if (mdata == NULL) {
                keyboard_users_pending_deletion--;
                sol_ptr_vector_del(&keyboard_types, i);
                sol_ptr_vector_del(&keyboard_users, i);
                if (keyboard_users_pending_deletion == 0)
                    break;
            }
        }
    }

    if (sol_ptr_vector_get_len(&keyboard_users) == 0) {
        keyboard_termios_reset();

        if (keyboard_watch) {
            sol_fd_del(keyboard_watch);
            keyboard_watch = NULL;
        }
    }
}

static bool
packet_irange_send(struct sol_flow_node *node, int32_t value)
{
    struct sol_irange val = { 0, 0, INT32_MAX, 1 };

    val.val = value;
    sol_flow_send_irange_packet(node, 0, &val);
    return true;
}

static void
keyboard_boolean_on_code(struct keyboard_common_data *data,
    unsigned char *buf,
    size_t len)
{
    struct keyboard_boolean_data *mdata = (struct keyboard_boolean_data *)data;
    uint64_t code = 0;
    uint16_t i;

    memcpy(&code, buf, len > sizeof(code) ? sizeof(code) : len);

    if (code == 0)
        return;

    keyboard_users_walking++;

    SOL_PTR_VECTOR_FOREACH_IDX (&keyboard_users, mdata, i) {
        struct sol_flow_node *handle;

        if (!mdata) continue;

        handle = mdata->common.node;

        if (mdata->binary_code != code)
            continue;

        if (mdata->toggle) {
            if (mdata->common.last_code == code)
                mdata->common.last_code = 0;
            else
                mdata->common.last_code = code;
            sol_flow_send_boolean_packet(handle, 0,
                mdata->common.last_code == mdata->binary_code);
        } else {
            mdata->common.last_code = code;
            sol_flow_send_boolean_packet(handle, 0, true);
            mdata->common.last_code = 0;
            sol_flow_send_boolean_packet(handle, 0, false);
        }
    }
    keyboard_users_walking--;
    keyboard_users_cleanup();
}

static void
keyboard_irange_on_code(struct keyboard_common_data *data,
    unsigned char *buf,
    size_t len)
{
    struct keyboard_irange_data *mdata = (struct keyboard_irange_data *)data;
    uint64_t code = 0;
    uint16_t i;

    for (i = 0; i < len; i++)
        code = (code << 8) | buf[i];

    if (code == 0)
        return;

    keyboard_users_walking++;

    SOL_PTR_VECTOR_FOREACH_IDX (&keyboard_users, mdata, i) {
        struct sol_flow_node *handle;

        if (!mdata) continue;

        handle = mdata->common.node;

        packet_irange_send(handle, mdata->common.last_code);
    }
    keyboard_users_walking--;
    keyboard_users_cleanup();
}

static bool
keyboard_on_event(void *data, int fd, unsigned int cond)
{
    struct keyboard_common_data *mdata = data;

    if (cond & SOL_FD_FLAGS_IN) {
        unsigned char buf[8];
        int r = read(STDIN_FILENO, buf, sizeof(buf));
        if (r < 0) {
            SOL_WRN("could not read stdin: %s", sol_util_strerrora(errno));
            cond |= SOL_FD_FLAGS_ERR;
        } else if (r > 0) {
            mdata->on_code(mdata, buf, r);
        }
    }

    if (cond & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL)) {
        struct keyboard_data *user;
        uint16_t i;

        SOL_WRN("error reading from stdin.");

        keyboard_users_walking++;

        SOL_PTR_VECTOR_FOREACH_IDX (&keyboard_users, user, i) {
            if (!user) continue;

            //FIXME: error packet to the rescue, not ready yet
        }

        keyboard_users_walking--;
        keyboard_users_cleanup();
        sol_flow_send_error_packet(mdata->node, EIO, NULL);
    }

    return true;
}

static int
keyboard_open(struct sol_flow_node *node,
    void *data)
{
    struct keyboard_common_data *mdata = data;

    if (!keyboard_done) {
        int flags;

        if (!isatty(STDIN_FILENO)) {
            SOL_WRN("stdin is not a TTY");
            return -errno;
        }

        flags = fcntl(STDIN_FILENO, F_GETFL);
        flags |= O_NONBLOCK;
        fcntl(STDIN_FILENO, F_SETFL, flags);

        tcgetattr(STDIN_FILENO, &keyboard_termios);
        keyboard_done = true;
        atexit(keyboard_termios_reset);
    }

    mdata->node = node;

    if (!keyboard_watch) {
        keyboard_watch = sol_fd_add(
            STDIN_FILENO, SOL_FD_FLAGS_IN | SOL_FD_FLAGS_ERR,
            keyboard_on_event, mdata);

        if (!keyboard_termios_setup()) {
            if (keyboard_watch) {
                sol_fd_del(keyboard_watch);
                keyboard_watch = NULL;
            }
            return -errno;
        }
    }

    sol_ptr_vector_append(&keyboard_users, mdata);

    return 0;
}

static int
keyboard_boolean_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct keyboard_boolean_data *mdata = data;
    const struct sol_flow_node_type_keyboard_boolean_options *opts =
        (const struct sol_flow_node_type_keyboard_boolean_options *)options;

    SOL_NULL_CHECK(options, -EINVAL);

    mdata->binary_code = opts->binary_code.val;
    mdata->toggle = opts->toggle;
    mdata->common.on_code = keyboard_boolean_on_code;

    return keyboard_open(node, data);
}

static void
keyboard_close(struct sol_flow_node *node, void *data)
{
    struct keyboard_common_data *mdata = data;
    struct keyboard_common_data *it;
    uint16_t i;

    SOL_DBG("keyboard close %p", mdata);

    keyboard_users_walking++;
    SOL_PTR_VECTOR_FOREACH_IDX (&keyboard_users, it, i) {
        if (it == mdata) {
            if (keyboard_users_walking > 1) {
                keyboard_users_pending_deletion++;
                sol_ptr_vector_set(&keyboard_users, i, NULL);
            } else
                sol_ptr_vector_del(&keyboard_types, i);
            sol_ptr_vector_del(&keyboard_users, i);
            break;
        }
    }
    keyboard_users_walking--;
    keyboard_users_cleanup();

    if (sol_ptr_vector_get_len(&keyboard_users) == 0) {
        keyboard_termios_reset();

        if (keyboard_watch) {
            sol_fd_del(keyboard_watch);
            keyboard_watch = NULL;
        }
    }
}

static void
keyboard_boolean_close(struct sol_flow_node *node, void *data)
{
    return keyboard_close(node, data);
}


static int
keyboard_irange_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct keyboard_irange_data *mdata = data;

    mdata->common.on_code = keyboard_irange_on_code;
    return keyboard_open(node, data);
}

static void
keyboard_irange_close(struct sol_flow_node *node, void *data)
{
    return keyboard_close(node, data);
}


#include "keyboard-gen.c"
