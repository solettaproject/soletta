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

#include "sol-flow/keyboard.h"

#include "sol-flow.h"
#include "sol-buffer.h"
#include "sol-mainloop.h"
#include "sol-vector.h"
#include "sol-util-internal.h"
#include "sol-util-file.h"
#include "sol-flow-internal.h"

struct keyboard_common_data {
    struct sol_flow_node *node;
    uint64_t last_code, last_read_code;
};

struct keyboard_boolean_data {
    struct keyboard_common_data common;
    uint64_t binary_code;
    bool toggle;
};

struct keyboard_node_type {
    struct sol_flow_node_type base;
    void (*on_code)(struct keyboard_common_data *mdata, unsigned char *buf,
        size_t len);
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
    if (tcsetattr(STDIN_FILENO, TCSANOW, &keyboard_termios) < 0) {
        SOL_WRN("could not reset termios: %s", sol_util_strerrora(errno));
    }
}

static void
keyboard_users_cleanup(void)
{
    struct keyboard_common_data *mdata;
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
    struct sol_flow_node *handle;
    uint64_t code = 0;

    memcpy(&code, buf, len > sizeof(code) ? sizeof(code) : len);

    if (code == 0)
        return;

    handle = mdata->common.node;

    if (mdata->binary_code != code)
        return;

    if (mdata->toggle) {
        if (mdata->common.last_code == code)
            mdata->common.last_code = 0;
        else
            mdata->common.last_code = code;
        sol_flow_send_bool_packet(handle, 0,
            mdata->common.last_code == mdata->binary_code);
    } else {
        mdata->common.last_code = code;
        sol_flow_send_bool_packet(handle, 0, true);
        mdata->common.last_code = 0;
        sol_flow_send_bool_packet(handle, 0, false);
    }
}

static void
keyboard_irange_on_code(struct keyboard_common_data *data,
    unsigned char *buf,
    size_t len)
{
    uint64_t code = 0;
    uint16_t i;

    for (i = 0; i < len; i++)
        code = (code << 8) | buf[i];

    if (code == 0)
        return;

    data->last_code = (int32_t)code;
    packet_irange_send(data->node, data->last_code);
}

static bool
keyboard_on_event(void *data, int fd, uint32_t cond)
{
    struct keyboard_common_data *mdata;
    uint16_t i;

    if (cond & SOL_FD_FLAGS_IN) {
        unsigned char buf[8];
        struct sol_buffer buffer = SOL_BUFFER_INIT_FLAGS(buf, sizeof(buf),
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
        int r = sol_util_fill_buffer(STDIN_FILENO, &buffer, buffer.capacity);
        if (r < 0) {
            SOL_WRN("could not read stdin: %s", sol_util_strerrora(errno));
            cond |= SOL_FD_FLAGS_ERR;
        } else if (buffer.used > 0) {
            const struct keyboard_node_type *type;

            keyboard_users_walking++;

            SOL_PTR_VECTOR_FOREACH_IDX (&keyboard_users, mdata, i) {
                type = (const struct keyboard_node_type *)
                    sol_flow_node_get_type(mdata->node);
                type->on_code(mdata, buf, buffer.used);
            }

            keyboard_users_walking--;
            keyboard_users_cleanup();
        }
    }

    if (cond & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL)) {
        SOL_WRN("error reading from stdin.");

        keyboard_users_walking++;

        SOL_PTR_VECTOR_FOREACH_IDX (&keyboard_users, mdata, i) {
            sol_flow_send_error_packet(mdata->node, EIO, NULL);
        }

        keyboard_users_walking--;
        keyboard_users_cleanup();
    }

    return true;
}

static int
keyboard_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct keyboard_common_data *mdata = data;

    if (!keyboard_done) {
        if (!isatty(STDIN_FILENO)) {
            SOL_WRN("stdin is not a TTY");
            return -errno;
        }

        if (sol_util_fd_set_flag(STDIN_FILENO, O_NONBLOCK) < 0)
            return -errno;

        if (tcgetattr(STDIN_FILENO, &keyboard_termios) != 0) {
            SOL_ERR("Unable to get keyboard settings.");
            return -errno;
        }

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

    return sol_ptr_vector_append(&keyboard_users, mdata);
}

static int
keyboard_boolean_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct keyboard_boolean_data *mdata = data;
    const struct sol_flow_node_type_keyboard_boolean_options *opts =
        (const struct sol_flow_node_type_keyboard_boolean_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_KEYBOARD_BOOLEAN_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->binary_code = opts->binary_code;
    mdata->toggle = opts->toggle;

    return keyboard_open(node, data, options);
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

#include "keyboard-gen.c"
