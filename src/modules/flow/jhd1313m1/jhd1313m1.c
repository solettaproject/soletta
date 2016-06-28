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
#include <math.h>

#include "sol-flow/jhd1313m1.h"

#include "sol-flow-internal.h"
#include "sol-flow-static.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-vector.h"
#include "sol-i2c.h"

#define COL_MIN (0)
#define COL_MAX (15)
#define COL_EXTRA (16) /* when writing RTL, the cursor must be past
                        * the screen to start in the last cell
                        * right */
#define ROW_MIN (0)
#define ROW_MAX (1)

static const uint8_t RGB_ADDR = 0xc4 >> 1; //0x62 = 98dec
static const uint8_t COLOR_ADDR[3] = { 0x04, 0x03, 0x02 };
static const uint8_t DISPLAY_ADDR = 0x7c >> 1; //0x3E = 62dec
static const uint8_t ROW_ADDR[2] = { 0x80, 0xc0 };

static const uint8_t SEND_DATA = 0x40;
static const uint8_t SEND_COMMAND = 0x80;

enum command_status {
    COMMAND_STATUS_WAITING = 0,
    COMMAND_STATUS_SENDING,
    COMMAND_STATUS_DONE
};

#define FLAG_SPECIAL_CMD (1 << 0)
#define FLAG_STRING (1 << 1)
#define FLAG_CURSOR_COL (1 << 2)
#define FLAG_CURSOR_ROW (1 << 3)

struct command {
    struct lcd_data *mdata;
    char *string;
    uint8_t chip_addr, data_addr, value;
    enum command_status status;
    uint8_t flags;
};

struct lcd_data {
    struct sol_i2c *i2c;
    struct sol_i2c_pending *i2c_pending;
    struct sol_ptr_vector cmd_queue;
    struct sol_timeout *timer;
    uint8_t row, col, display_mode, display_control;
    bool error : 1;
    bool ready : 1;
};

#define LCD_BLINK_ON (0x01)
#define LCD_CURSOR_ON (0x02)
#define LCD_MODE_SET_LTR (0x02)
#define LCD_MODE_SET_AUTO_SCROLL (0x01)

static struct command *
command_new(struct lcd_data *mdata)
{
    struct command *cmd;
    int r;

    if (mdata->error)
        return NULL;

    cmd = calloc(1, sizeof(*cmd));
    SOL_NULL_CHECK(cmd, NULL);
    cmd->mdata = mdata;
    cmd->flags = 0;

    r = sol_ptr_vector_append(&mdata->cmd_queue, cmd);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    return cmd;

err:
    free(cmd);
    return NULL;
}

static int
command_queue_append(struct lcd_data *mdata,
    uint8_t chip_addr,
    uint8_t data_addr,
    uint8_t value)
{
    struct command *cmd = command_new(mdata);

    SOL_NULL_CHECK(cmd, -ENOMEM);

    cmd->chip_addr = chip_addr;
    cmd->data_addr = data_addr;
    cmd->value = value;
    cmd->status = COMMAND_STATUS_WAITING;
    cmd->string = NULL;

    return 0;
}

static int
command_string_queue_append(struct lcd_data *mdata, char *string)
{
    struct command *cmd = command_new(mdata);

    SOL_NULL_CHECK(cmd, -ENOMEM);

    cmd->chip_addr = DISPLAY_ADDR;
    cmd->data_addr = SEND_COMMAND;
    cmd->value = 0;
    cmd->status = COMMAND_STATUS_WAITING;
    cmd->string = string;
    cmd->flags = FLAG_SPECIAL_CMD | FLAG_STRING;

    return 0;
}

/* update only one of row/col */
static int
command_cursor_position_queue_append(struct lcd_data *mdata, int row, int col)
{
    struct command *cmd = command_new(mdata);

    SOL_NULL_CHECK(cmd, -ENOMEM);

    cmd->chip_addr = DISPLAY_ADDR;
    cmd->data_addr = SEND_COMMAND;
    cmd->status = COMMAND_STATUS_WAITING;
    cmd->string = NULL;
    cmd->flags = FLAG_SPECIAL_CMD;

    if (row > -1) {
        cmd->flags |= FLAG_CURSOR_ROW;
        cmd->value = row & 0xFF;
    } else {
        cmd->flags |= FLAG_CURSOR_COL;
        cmd->value = col & 0xFF;
    }

    return 0;
}

/* update both row and col */
static int
pos_cmd_queue(struct lcd_data *mdata, uint8_t row, uint8_t col)
{
    uint8_t command = col | ROW_ADDR[row];

    return command_queue_append(mdata, DISPLAY_ADDR, SEND_COMMAND, command);
}

static int
char_cmd_queue(struct lcd_data *mdata, uint8_t value)
{
    return command_queue_append(mdata, DISPLAY_ADDR, SEND_DATA, value);
}

/* Returns the number of chars behind the current cursor position, if
 * in LTR, or after that, if in RTL. If autoscroll is on, it will
 * return 0 on success. */
static int
write_char(struct lcd_data *mdata, char value)
{
    bool right_to_left = false, autoscroll = false, newline = false;
    int r;

    right_to_left = !(mdata->display_mode & LCD_MODE_SET_LTR);

    if (value != '\n') {
        r = char_cmd_queue(mdata, value);
        SOL_INT_CHECK(r, < 0, r);
    } else {
        mdata->row++;
        if (mdata->row > ROW_MAX)
            mdata->row = ROW_MAX;
        if (right_to_left)
            mdata->col = COL_MAX;
        else
            mdata->col = COL_MIN;
        newline = true;
    }

    autoscroll = mdata->display_mode & LCD_MODE_SET_AUTO_SCROLL;
    /* When autoscrolling, don't advance in either way */
    if (autoscroll)
        return 0;

    if (newline) {
        r = pos_cmd_queue(mdata, mdata->row, mdata->col);
        if (r < 0) {
            SOL_WRN("Failed to change cursor position");
            return r;
        }
        goto end;
    }

    if (right_to_left) {
        mdata->col--;
        /* Going RTL case (checking underflow on unsigned): jump to
         * end of 1st line or keep overriding first col */
        if (mdata->col == UINT8_MAX) {
            if (mdata->row < ROW_MAX) {
                mdata->col = COL_MAX;
                mdata->row++;
                r = pos_cmd_queue(mdata, mdata->row, mdata->col);
                if (r < 0) {
                    SOL_WRN("Failed to change cursor position");
                    return r;
                }
            } else
                mdata->col = COL_MIN;
        }
    } else {
        if (mdata->col < UINT8_MAX)
            mdata->col++;
        /* Going LTR case: jump to start of next line or keep
         * overriding last col */
        if (mdata->col > COL_MAX) {
            if (mdata->row < ROW_MAX) {
                mdata->col = COL_MIN;
                mdata->row++;
                r = pos_cmd_queue(mdata, mdata->row, mdata->col);
                if (r < 0) {
                    SOL_WRN("Failed to change cursor position");
                    return r;
                }
            } else
                mdata->col = COL_MAX;
        }
    }

end:
    if (right_to_left)
        return mdata->col + (1 + COL_MAX) * mdata->row;
    else
        return (ROW_MAX - mdata->row) * (1 + COL_MAX) + (COL_MAX - mdata->col);
}

static int
write_string(struct lcd_data *mdata, char *str)
{
    int r;

    while (*str) {
        r = write_char(mdata, *str++);
        SOL_INT_CHECK(r, < 0, r);
        /* stop if the whole display was used */
        if (r >= (COL_MAX + 1) * (ROW_MAX + 1) - 1)
            return 0;
    }

    return 0;
}

#define LCD_CLEAR (0x01)
#define LCD_ENTRY_MODE_SET (0x04)
#define LCD_DISPLAY_CONTROL (0x08)
#define LCD_FUNCTION_SET (0x20)
#define LCD_DISPLAY_ON (0x04)
#define LCD_FUNCTION_SET_2_LINES (0x08)

#define LCD_CURSOR_SHIFT (0x10)
#define LCD_DISPLAY_MOVE (0x08)
#define LCD_MOVE_RIGHT (0x04)
#define LCD_MOVE_LEFT (0x00)

#define LCD_RGB_MODE1 (0x00)
#define LCD_RGB_MODE2 (0x01)
#define LCD_RGB_OUTPUT (0x08)

#define LCD_COLOR_R (0)
#define LCD_COLOR_G (1)
#define LCD_COLOR_B (2)

#define TIME_TO_CLEAR (15)
#define I2C_STEP_TIME (1)
#define TIME_TO_TURN_ON (55)

static int command_queue_process(struct lcd_data *mdata);

static bool
timer_cb(void *data)
{
    int r;
    struct lcd_data *mdata = data;

    mdata->timer = NULL;
    r = command_queue_process(data);
    if (r < 0)
        SOL_ERR("Error processing LCD's I2C command queue: %s\n",
            sol_util_strerrora(r));

    return false;
}

static int
timer_reschedule(struct lcd_data *mdata,
    uint32_t timeout_ms,
    bool delete_prev)
{
    if (mdata->timer) {
        if (!delete_prev)
            return 0;
        else
            sol_timeout_del(mdata->timer);
    }

    mdata->timer = sol_timeout_add(timeout_ms, timer_cb, mdata);
    SOL_NULL_CHECK(mdata->timer, -ENOMEM);

    return 0;
}

static void
i2c_write_cb(void *cb_data,
    struct sol_i2c *i2c,
    uint8_t reg,
    uint8_t *data,
    ssize_t status)
{
    struct command *cmd = cb_data;
    struct lcd_data *mdata = cmd->mdata;

    mdata->i2c_pending = NULL;
    if (status < 0)
        SOL_WRN("LCD command %p failed: %s", cmd, sol_util_strerrora(-status));

    cmd->status = COMMAND_STATUS_DONE;

    if (cmd->value != LCD_CLEAR)
        command_queue_process(mdata);
}

static int
command_send(struct lcd_data *mdata, struct command *cmd)
{
    int r;

    r = sol_i2c_set_slave_address(mdata->i2c, cmd->chip_addr);

    if (r < 0) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", cmd->chip_addr);
        return -EIO;
    }

    cmd->status = COMMAND_STATUS_SENDING;
    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c, cmd->data_addr,
        &cmd->value, 1, i2c_write_cb, cmd);
    if (!mdata->i2c_pending) {
        SOL_WRN("Failed to write on I2C register 0x%02x\n", cmd->data_addr);
        cmd->status = COMMAND_STATUS_WAITING;
        return -EIO;
    }

    return 0;
}

static void
command_free(struct command *cmd)
{
    if (cmd->string)
        free(cmd->string);
    free(cmd);
}

static void
free_commands(struct sol_ptr_vector *cmd_queue, bool done_only)
{
    struct command *cmd;
    uint16_t i;

    /* Traverse backwards so deletion doesn't impact the indexes. */
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (cmd_queue, cmd, i) {
        if (done_only && cmd->status != COMMAND_STATUS_DONE)
            continue;
        command_free(cmd);
        sol_ptr_vector_del(cmd_queue, i);
    }
}

static int
lcd_string_write_process(struct lcd_data *mdata, char *string, uint16_t i)
{
    struct sol_ptr_vector old_vector;
    struct sol_ptr_vector final_vector;
    struct command *cmd;
    uint16_t j;
    int r;

    /*
     * The commands that will be queued by write_string() must be
     * right after the command that triggered this function, to
     * accomplish this with vector:
     * - Copy mdata->cmd_queue vector to old_vector
     * - Initialize mdata->cmd_queue
     * - Call write_string() to queue the commands to mdata->cmd_queue
     * - Initialize the final_vector
     * - Append all processed(0..i) commands on old_vector to final_vector
     * - Append all commands queued by write_string() to final_vector
     * - Append all non-processed(i+1..len) commands on old_vector to
     *   final_vector
     * - Set final_vector as mdata->cmd_queue
     */

    old_vector = mdata->cmd_queue;
    sol_ptr_vector_init(&mdata->cmd_queue);

    /* mdata->cmd_queue now contains the expanded string commands */
    r = write_string(mdata, string);
    if (r < 0) {
        free_commands(&mdata->cmd_queue, true);
        sol_ptr_vector_clear(&mdata->cmd_queue);
        goto err;
    }

    /* copy original commands up to 'i' */
    sol_ptr_vector_init(&final_vector);
    SOL_PTR_VECTOR_FOREACH_IDX (&old_vector, cmd, j) {
        /* 'i' is included back because it is marked as done before
         * this function is called
         */
        if (j > i) break;

        r = sol_ptr_vector_append(&final_vector, cmd);
        SOL_INT_CHECK_GOTO(r, < 0, err_cmds);
    }

    /* copy commands of the string command expansion */
    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->cmd_queue, cmd, j) {
        r = sol_ptr_vector_append(&final_vector, cmd);
        SOL_INT_CHECK_GOTO(r, < 0, err_cmds);
    }

    /* only clear, since we moved them to final_vector */
    sol_ptr_vector_clear(&mdata->cmd_queue);

    /* copy original commands past 'i' */
    SOL_PTR_VECTOR_FOREACH_IDX (&old_vector, cmd, j) {
        if (j <= i) continue;

        r = sol_ptr_vector_append(&final_vector, cmd);
        SOL_INT_CHECK_GOTO(r, < 0, err_final);
    }
    /* only clear, since we moved them to final_vector */
    sol_ptr_vector_clear(&old_vector);

    mdata->cmd_queue = final_vector;

    return 0;

err_cmds:
    free_commands(&mdata->cmd_queue, false);
    sol_ptr_vector_clear(&mdata->cmd_queue);

err_final:
    free_commands(&final_vector, false);
    sol_ptr_vector_clear(&final_vector);

err:
    mdata->cmd_queue = old_vector;
    return r;
}

static inline bool
is_processing(struct lcd_data *mdata)
{
    return mdata->i2c_pending != NULL || !mdata->ready;
}

static int
command_queue_start(struct lcd_data *mdata)
{
    if (is_processing(mdata))
        return 0;

    return command_queue_process(mdata);
}

/* commit buffered changes */
static int
command_queue_process(struct lcd_data *mdata)
{
    struct command *cmd;
    uint16_t i;
    int r;

    if (mdata->i2c_pending) {
        r = timer_reschedule(mdata, I2C_STEP_TIME, false);
        SOL_INT_CHECK_GOTO(r, < 0, sched_err);
        goto end;
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->cmd_queue, cmd, i) {
        /* done, left to be cleaned after the loop */
        if (cmd->status == COMMAND_STATUS_DONE) break;

        /* COMMAND_STATUS_WAITING cases, since COMMAND_STATUS_SENDING
         * can't happen at this point */
        if (!(cmd->flags & FLAG_SPECIAL_CMD)) {
            r = command_send(mdata, cmd);
            SOL_INT_CHECK_GOTO(r, < 0, err);

            if (cmd->chip_addr == DISPLAY_ADDR && cmd->value == LCD_CLEAR) {
                /* command_send() on a clear one will not link back to
                 * command_queue_process(), we do this by a timer */
                mdata->row = ROW_MIN;
                mdata->col = COL_MIN;

                r = timer_reschedule(mdata, TIME_TO_CLEAR, true);
                SOL_INT_CHECK_GOTO(r, < 0, sched_err);
                cmd->status = COMMAND_STATUS_DONE;
            }
            return 0;
        }

        /* FLAG_SPECIAL_CMD cases */
        if (cmd->flags & FLAG_STRING) {
            /* Being a fake command, we expand it to real,
             * intermediate commands (and mark this one as done). We
             * *have* to expand string commands JIT-like, because we
             * can't know beforehand the state of row/col that each
             * write_char() call will find while traversing the
             * string's individual char commands. */
            cmd->status = COMMAND_STATUS_DONE;

            r = lcd_string_write_process(mdata, cmd->string, i);
            SOL_INT_CHECK_GOTO(r, < 0, err);
            /* proceed to the 1st expanded command */
            continue;
        }

        /* Just col/row as special commands left. Since we do the
         * trick of passing pure row/col on value, store it on mdata
         * and fix the expected value for the I2C wire. */
        if (cmd->flags & FLAG_CURSOR_COL) {
            mdata->col = cmd->value;
        } else {
            mdata->row = cmd->value;
        }
        cmd->value = mdata->col | ROW_ADDR[mdata->row];
        r = command_send(mdata, cmd);
        SOL_INT_CHECK_GOTO(r, < 0, err);
        return 0;
    }

    free_commands(&mdata->cmd_queue, true);

    if (mdata->cmd_queue.base.len)
        return command_queue_process(mdata);

end:
    return 0;

err:
    SOL_ERR("Failed to process LCD command,"
        " no new commands will be executed.");
    mdata->error = true;
    return r;

sched_err:
    SOL_WRN("Fail to reschedule LCD command queue,"
        " no new commands will be executed");
    mdata->error = true;
    return r;
}

static int
clear_cmd_queue(struct lcd_data *mdata)
{
    return command_queue_append(mdata, DISPLAY_ADDR, SEND_COMMAND, LCD_CLEAR);
}

static bool
start(void *data)
{
    struct lcd_data *mdata = data;
    int r;

    mdata->ready = true;

    r = command_queue_start(mdata);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    return false;

fail:
    SOL_WRN("Unable to start LCD command queue");
    return false;
}

static int
append_setup_commands(struct lcd_data *mdata)
{
    int r;
    struct sol_timeout *timer;

    SOL_DBG("About to append 8 initial cmds");

    /* set display to 2 lines */
    r = command_queue_append(mdata, DISPLAY_ADDR, SEND_COMMAND,
        LCD_FUNCTION_SET | LCD_FUNCTION_SET_2_LINES);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    /* turn on display */
    r = command_queue_append(mdata, DISPLAY_ADDR, SEND_COMMAND,
        LCD_DISPLAY_CONTROL | LCD_DISPLAY_ON);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = command_queue_append(mdata, DISPLAY_ADDR, SEND_COMMAND,
        mdata->display_mode);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = command_queue_append(mdata, DISPLAY_ADDR, SEND_COMMAND,
        mdata->display_control);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = command_queue_append(mdata, RGB_ADDR, LCD_RGB_MODE1, 0);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = command_queue_append(mdata, RGB_ADDR, LCD_RGB_MODE2, 0);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = command_queue_append(mdata, RGB_ADDR, LCD_RGB_OUTPUT, 0xAA);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    /* clear display */
    r = clear_cmd_queue(mdata);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    timer = sol_timeout_add(TIME_TO_TURN_ON, start, mdata);
    SOL_NULL_CHECK(timer, -ENOMEM);

    return 0;

fail:
    SOL_WRN("Unable to queue initial LCD commands");
    return r;
}

#undef LCD_FUNCTION_SET
#undef LCD_FUNCTION_SET_2_LINES
#undef LCD_RGB_MODE1
#undef LCD_RGB_MODE2
#undef LCD_RGB_OUTPUT
#undef LCD_COLOR_R
#undef LCD_COLOR_G
#undef LCD_COLOR_B

static int
lcd_open(struct lcd_data *mdata, uint8_t bus)
{
    mdata->i2c = sol_i2c_open(bus, SOL_I2C_SPEED_10KBIT);
    SOL_NULL_CHECK_MSG(mdata->i2c, -EIO, "Failed to open i2c bus %d", bus);

    sol_ptr_vector_init(&mdata->cmd_queue);

    return append_setup_commands(mdata);
}

static void
lcd_close(struct sol_flow_node *node, void *data)
{
    struct lcd_data *mdata = data;

    if (mdata->timer)
        sol_timeout_del(mdata->timer);

    if (mdata->i2c_pending)
        sol_i2c_pending_cancel(mdata->i2c, mdata->i2c_pending);
    if (mdata->i2c)
        sol_i2c_close(mdata->i2c);

    free_commands(&mdata->cmd_queue, false);
    sol_ptr_vector_clear(&mdata->cmd_queue);
}

/* LCD API */
static int
set_row(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    struct sol_irange in_value;
    int r;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value.val < ROW_MIN || in_value.val > ROW_MAX) {
        SOL_WRN("Row range for this lcd display is %d-%d", ROW_MIN, ROW_MAX);
        return -EINVAL;
    }

    r = command_cursor_position_queue_append(mdata, in_value.val, -1);
    SOL_INT_CHECK(r, < 0, r);

    return command_queue_start(mdata);
}

/* LCD API */
static int
set_col(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    struct sol_irange in_value;
    int r;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value.val < COL_MIN || in_value.val > COL_EXTRA) {
        SOL_WRN("Column range for this lcd display is %d-%d",
            COL_MIN, COL_EXTRA);
        return -EINVAL;
    }

    r = command_cursor_position_queue_append(mdata, -1, in_value.val);
    SOL_INT_CHECK(r, < 0, r);

    return command_queue_start(mdata);
}

/* serves cursor blink/underline and display on/off cmds */
static int
char_display_cmd_queue(struct lcd_data *mdata)
{
    return command_queue_append(mdata, DISPLAY_ADDR, SEND_COMMAND,
        mdata->display_control);
}

/* LCD API */
static int
set_display_on(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    bool in_value;
    int r;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value)
        mdata->display_control |= LCD_DISPLAY_ON;
    else
        mdata->display_control &= ~LCD_DISPLAY_ON;

    r = char_display_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);

    return command_queue_start(mdata);
}

/* LCD API */
static int
set_underline_cursor(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    bool in_value;
    int r;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value)
        mdata->display_control |= LCD_CURSOR_ON;
    else
        mdata->display_control &= ~LCD_CURSOR_ON;

    r = char_display_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);

    return command_queue_start(mdata);
}

/* LCD API */
static int
set_blinking_cursor(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    bool in_value;
    int r;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value)
        mdata->display_control |= LCD_BLINK_ON;
    else
        mdata->display_control &= ~LCD_BLINK_ON;

    r = char_display_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);

    return command_queue_start(mdata);
}

/* serves both set_ltr() and set_autoscroll() */
static int
char_entry_cmd_queue(struct lcd_data *mdata)
{
    return command_queue_append(mdata, DISPLAY_ADDR, SEND_COMMAND,
        mdata->display_mode);
}

/* LCD API */
static int
set_ltr(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    bool in_value;
    int r;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value)
        mdata->display_mode |= LCD_MODE_SET_LTR;
    else
        mdata->display_mode &= ~LCD_MODE_SET_LTR;

    r = char_entry_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);

    return command_queue_start(mdata);
}

/* LCD API */
static int
set_autoscroll(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    bool in_value;
    int r;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value)
        mdata->display_mode |= LCD_MODE_SET_AUTO_SCROLL;
    else
        mdata->display_mode &= ~LCD_MODE_SET_AUTO_SCROLL;

    r = char_entry_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);

    return command_queue_start(mdata);
}

/* LCD API */
static int
put_char(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    unsigned char in_value;
    int r;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = char_cmd_queue(mdata, in_value);
    SOL_INT_CHECK(r, < 0, r);

    return command_queue_start(mdata);
}

#undef LCD_CLEAR

/* LCD API */
static int
display_clear(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    int r;

    r = clear_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);

    return command_queue_start(mdata);
}

/* LCD API */
/* insert a sequence of chars where the cursor is at */
static int
put_string(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    const char *in_value;
    char *string;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    string = strdup(in_value);
    SOL_NULL_CHECK(string, -ENOMEM);

    r = command_string_queue_append(mdata, string);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    return command_queue_start(mdata);

fail:
    free(string);
    return r;
}

/* LCD API */
/* clear screen and write sequence of chars from (0, 0) position */
static int
set_string(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    const char *in_value;
    char *string;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    string = strdup(in_value);
    SOL_NULL_CHECK(string, -ENOMEM);

    r = clear_cmd_queue(mdata);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = command_string_queue_append(mdata, string);
    SOL_INT_CHECK_GOTO(r, < 0, fail);
    command_queue_start(mdata);

    return 0;

fail:
    free(string);
    return r;
}

static int
color_cmd_queue(struct lcd_data *mdata,
    uint32_t red,
    uint32_t green,
    uint32_t blue)
{
    uint32_t colors[] = { red, green, blue };
    uint32_t *color = colors;
    unsigned i;

    for (i = 0; i < sol_util_array_size(colors); i++) {
        int r = command_queue_append(mdata, RGB_ADDR, COLOR_ADDR[i], *color++);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

/* LCD API */
static int
set_color(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    struct sol_rgb in_value;
    int r;

    r = sol_flow_packet_get_rgb(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (sol_rgb_set_max(&in_value, 255) < 0) {
        SOL_WRN("Invalid color");
        return -EINVAL;
    }

    r = color_cmd_queue(mdata, in_value.red, in_value.green, in_value.blue);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

static int
display_scroll_cmd_queue(struct lcd_data *mdata, uint8_t value)
{
    return command_queue_append(mdata, DISPLAY_ADDR, SEND_COMMAND, value);
}

/* LCD API */
static int
scroll_display(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    uint8_t value = LCD_CURSOR_SHIFT | LCD_DISPLAY_MOVE;
    struct lcd_data *mdata = data;
    int r;

    value |= (port == SOL_FLOW_NODE_TYPE_JHD1313M1_CHAR__IN__SCROLL_RIGHT ?
        LCD_MOVE_RIGHT : LCD_MOVE_LEFT);

    r = display_scroll_cmd_queue(mdata, value);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

static int
lcd_string_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct lcd_data *mdata = data;
    const struct sol_flow_node_type_jhd1313m1_string_options *opts =
        (const struct sol_flow_node_type_jhd1313m1_string_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (opts, SOL_FLOW_NODE_TYPE_JHD1313M1_STRING_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->display_mode = LCD_ENTRY_MODE_SET | LCD_MODE_SET_LTR;
    mdata->display_control = (LCD_DISPLAY_CONTROL | LCD_DISPLAY_ON)
        & (~LCD_BLINK_ON | ~LCD_CURSOR_ON);

    r = lcd_open(mdata, (uint8_t)opts->bus);
    SOL_INT_CHECK(r, < 0, r);

    return color_cmd_queue(mdata,
        opts->color.red, opts->color.green, opts->color.blue);
}

static int
lcd_char_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct lcd_data *mdata = data;
    const struct sol_flow_node_type_jhd1313m1_char_options *opts =
        (const struct sol_flow_node_type_jhd1313m1_char_options *)options;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (opts, SOL_FLOW_NODE_TYPE_JHD1313M1_CHAR_OPTIONS_API_VERSION, -EINVAL);

    mdata->display_mode = LCD_ENTRY_MODE_SET | LCD_MODE_SET_LTR;
    mdata->display_control = (LCD_DISPLAY_CONTROL | LCD_DISPLAY_ON)
        & (~LCD_BLINK_ON | ~LCD_CURSOR_ON);

    if (!opts->ltr)
        mdata->display_mode &= ~LCD_MODE_SET_LTR;

    if (opts->auto_scroll)
        mdata->display_mode |= LCD_MODE_SET_AUTO_SCROLL;
    else
        mdata->display_mode &= ~LCD_MODE_SET_AUTO_SCROLL;

    if (opts->blink_cursor)
        mdata->display_control |= LCD_BLINK_ON;
    else
        mdata->display_control &= ~LCD_BLINK_ON;

    if (opts->underline_cursor)
        mdata->display_control |= LCD_CURSOR_ON;
    else
        mdata->display_control &= ~LCD_CURSOR_ON;

    r = lcd_open(mdata, (uint8_t)opts->bus);
    SOL_INT_CHECK(r, < 0, r);

    r = command_cursor_position_queue_append(mdata, -1, opts->init_col);
    SOL_INT_CHECK(r, < 0, r);

    r = command_cursor_position_queue_append(mdata, opts->init_row, -1);
    SOL_INT_CHECK(r, < 0, r);

    r = char_entry_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);

    r = char_display_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);

    return color_cmd_queue(mdata,
        opts->color.red, opts->color.green, opts->color.blue);
}
#undef LCD_DISPLAY_CONTROL
#undef LCD_ENTRY_MODE_SET
#undef LCD_BLINK_ON
#undef LCD_CURSOR_ON
#undef LCD_DISPLAY_ON
#undef LCD_MODE_SET_LTR
#undef LCD_MODE_SET_AUTO_SCROLL

#include "jhd1313m1-gen.c"
