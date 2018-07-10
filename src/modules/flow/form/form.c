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
#include <stdio.h>

#include "sol-flow/form.h"
#include "sol-buffer.h"
#include "sol-mainloop.h"
#include "sol-flow-internal.h"
#include "sol-util-internal.h"

#include "form-common.h"

struct selector_data {
    char *title, *sel_mark, *cursor_mark, *text_mem;
    char *format, *title_tag, *value_tag, *pending_sel;
    struct sol_buffer text_grid;
    struct sol_ptr_vector items;
    uint16_t selection, cursor, n_values;
    size_t columns, rows;
    bool circular : 1;
    bool enabled : 1;
    bool n_values_done : 1;
};

static int
calculate_n_values(struct selector_data *mdata, size_t *row, size_t *col)
{
    int r;
    const char *tmp_ptr = NULL;
    size_t tmp_row = 0, tmp_col = 0, row_span;

    /* Calculate row span of post-value chunk */
    tmp_ptr = mdata->value_tag + sizeof(VALUE_TAG) - 1;
    tmp_row = *row + 1; /* + 1 so we get at least one value line */
    tmp_col = 0;
    r = format_chunk(&mdata->text_grid, mdata->rows, mdata->columns,
        tmp_ptr, mdata->format + strlen(mdata->format),
        &tmp_row, &tmp_col, CALC_ONLY, KEEP_NL);
    SOL_INT_CHECK(r, < 0, r);
    row_span = tmp_row - (*row + 1);

    mdata->n_values = mdata->rows - *row;

    if ((ssize_t)(mdata->n_values - row_span) <= 0)
        mdata->n_values = 0;
    else
        mdata->n_values -= row_span;

    mdata->n_values_done = true;

    return 0;
}

//FIXME: - autoscroll/markee effect on tags
//       - minimum formatting abilities for the value tag itself
//         (think printf "%-10.10s" "aoeiu")
static int
selector_format_do(struct sol_flow_node *node)
{
    struct selector_data *mdata = sol_flow_node_get_private_data(node);
    uint16_t len = sol_ptr_vector_get_len(&mdata->items);
    bool skip_cursor = false, no_more_space = false;
    size_t row = 0, col = 0, n_values = 0, idx = 0;
    int r, buf_size = mdata->text_grid.capacity;
    const char *it_value = NULL;

    r = format_title(&mdata->text_grid, buf_size, mdata->rows, mdata->columns,
        &row, &col, mdata->format, mdata->title, mdata->title_tag,
        mdata->value_tag, &no_more_space);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    SOL_EXP_CHECK_GOTO(no_more_space, send);

    if (!len) {
        n_values = 0;
    } else if (mdata->rows > 1) {
        r = go_to_new_line(&mdata->text_grid, mdata->rows, mdata->columns,
            &row, &col);
        SOL_INT_CHECK_GOTO(r, < 0, err);

        if (!mdata->n_values_done) {
            r = calculate_n_values(mdata, &row, &col);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        }
        n_values = mdata->n_values;
        if (!n_values)
            goto post_value;

        idx = sol_util_max((int16_t)(mdata->cursor - n_values / 2), (int16_t)0);
        if (idx + n_values > len)
            idx = sol_util_max((int16_t)(len - n_values), (int16_t)0);
    } else {
        idx = mdata->cursor;
        n_values = 1;
    }

    skip_cursor = (n_values == 1);

    /* Format values, while we can */
    while (n_values && (it_value = sol_ptr_vector_get(&mdata->items, idx))) {
        const char *value = NULL;
        size_t curr_row = row;
        bool did_cursor = false, did_sel = false;
        size_t cursor_len =
            mdata->cursor_mark ? strlen(mdata->cursor_mark) : 0,
            sel_len = mdata->sel_mark ? strlen(mdata->sel_mark) : 0,
            padding_spc = 0;

        /* Format selection/cursor markers */
        if (!skip_cursor && idx == mdata->cursor && mdata->cursor_mark) {
            value = mdata->cursor_mark;
            r = format_chunk(&mdata->text_grid, mdata->rows, mdata->columns,
                value, mdata->cursor_mark + cursor_len,
                &row, &col, DO_FORMAT, DITCH_NL);
            SOL_INT_CHECK_GOTO(r, < 0, err);
            if (r >= buf_size || row >= mdata->rows)
                goto send;
            if (row > curr_row)
                goto next;

            did_cursor = true;
        }

        if (idx == mdata->selection && mdata->sel_mark) {
            if (!skip_cursor && !did_cursor) {
                r = fill_spaces(&mdata->text_grid, mdata->columns,
                    &row, &col, cursor_len);
                SOL_INT_CHECK_GOTO(r, < 0, err);
                if (row > curr_row)
                    goto next;
            }

            value = mdata->sel_mark;
            r = format_chunk(&mdata->text_grid, mdata->rows, mdata->columns,
                value, mdata->sel_mark + sel_len, &row, &col, DO_FORMAT,
                DITCH_NL);
            SOL_INT_CHECK_GOTO(r, < 0, err);
            if (r >= buf_size || row >= mdata->rows)
                goto send;
            if (row > curr_row)
                goto next;

            did_sel = true;
        }

        if (skip_cursor) {
            if (!did_sel)
                padding_spc += did_sel * sel_len;
        } else {
            if (!did_sel) {
                padding_spc += sel_len;
                if (!did_cursor)
                    padding_spc += cursor_len;
            }
        }

        if (padding_spc) {
            r = fill_spaces(&mdata->text_grid, mdata->columns,
                &row, &col, padding_spc);
            SOL_INT_CHECK_GOTO(r, < 0, err);
            if (row > curr_row)
                goto next;
        }

        r = format_chunk(&mdata->text_grid, mdata->rows, mdata->columns,
            it_value, it_value + strlen(it_value), &row, &col, DO_FORMAT,
            DITCH_NL);
        SOL_INT_CHECK_GOTO(r, < 0, err);
        if (r >= buf_size || row >= mdata->rows)
            goto send;
        if (row > curr_row)
            goto next;

        r = fill_line(&mdata->text_grid, mdata->rows, mdata->columns,
            &row, &col, DO_FORMAT);
        SOL_INT_CHECK_GOTO(r, < 0, err);

next:
        n_values--;
        idx++;
    }

post_value:
    r = format_post_value(&mdata->text_grid, mdata->rows, mdata->columns,
        &row, &col, mdata->format, mdata->value_tag);
    SOL_INT_CHECK_GOTO(r, < 0, err);

send:
    return format_send(node, &mdata->text_grid,
        SOL_FLOW_NODE_TYPE_FORM_SELECTOR__OUT__STRING);

err:
    /* we got to re-init because of the error cases. if this function
     * fails we're no better, so don't check. */
    buffer_re_init(&mdata->text_grid, mdata->text_mem, mdata->rows,
        mdata->columns);

    return r;
}

static void
selector_close(struct sol_flow_node *node, void *data)
{
    struct selector_data *mdata = data;
    uint16_t i;
    char *it;

    sol_buffer_fini(&mdata->text_grid);

    free(mdata->cursor_mark);
    free(mdata->sel_mark);
    free(mdata->title);
    free(mdata->format);
    free(mdata->pending_sel);

    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->items, it, i)
        free(it);
    sol_ptr_vector_clear(&mdata->items);
}

static int
selector_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct selector_data *mdata = data;
    const struct sol_flow_node_type_form_selector_options *opts =
        (const struct sol_flow_node_type_form_selector_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_FORM_SELECTOR_OPTIONS_API_VERSION, -EINVAL);

    mdata->circular = opts->circular;

    sol_ptr_vector_init(&mdata->items);
    mdata->enabled = true;

    r = common_form_init(&mdata->text_grid,
        opts->rows,
        &mdata->rows,
        opts->columns,
        &mdata->columns,
        opts->format,
        &mdata->format,
        opts->title,
        &mdata->title,
        &mdata->title_tag,
        &mdata->value_tag,
        &mdata->text_mem);
    SOL_INT_CHECK(r, < 0, r);

    if (opts->selection_marker) {
        mdata->sel_mark = strdup(opts->selection_marker);
        if (!mdata->sel_mark) {
            r = -ENOMEM;
            goto err;
        }
    }

    if (opts->cursor_marker) {
        mdata->cursor_mark = strdup(opts->cursor_marker);
        if (!mdata->cursor_mark) {
            r = -ENOMEM;
            goto err;
        }
    }

    /* we don't issue selector_format_do() until the 1st add_item()
     * call is done, there's no point in doing so */
    return 0;

err:
    selector_close(node, mdata);

    return r;
}

static int
add_item(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct selector_data *mdata = data;
    const char *value;
    char *str;
    int r;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    str = strdup(value);
    SOL_NULL_CHECK(str, -ENOMEM);

    r = sol_ptr_vector_append(&mdata->items, str);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    if (mdata->pending_sel && streq(mdata->pending_sel, str)) {
        mdata->selection = sol_ptr_vector_get_len(&mdata->items) - 1;
        free(mdata->pending_sel);
        mdata->pending_sel = NULL;
    }

    return selector_format_do(node);

error:
    free(str);
    return r;
}

static int
clear_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct selector_data *mdata = data;
    uint16_t i;
    char *it;

    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->items, it, i)
        free(it);
    sol_ptr_vector_clear(&mdata->items);

    mdata->cursor = mdata->selection = 0;
    mdata->n_values = 0;
    mdata->n_values_done = false;

    return selector_format_do(node);
}

static int
next_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct selector_data *mdata = data;
    uint16_t len = sol_ptr_vector_get_len(&mdata->items);

    if (!mdata->enabled || !len)
        return 0;

    if (mdata->circular)
        mdata->cursor = (mdata->cursor + 1) % len;
    else
        mdata->cursor = sol_util_min(mdata->cursor + 1, len - 1);

    SOL_DBG("next (len = %d): curr is now %d", len, mdata->cursor);

    return selector_format_do(node);
}

static int
previous_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct selector_data *mdata = data;
    uint16_t len = sol_ptr_vector_get_len(&mdata->items);

    if (!mdata->enabled || !len)
        return 0;

    if (mdata->circular)
        mdata->cursor = mdata->cursor ? mdata->cursor - 1 : len - 1;
    else
        mdata->cursor = (int16_t)(sol_util_max(mdata->cursor - 1, 0));

    SOL_DBG("prev (len = %d): curr is now %d", len, mdata->cursor);

    return selector_format_do(node);
}

static int
selector_select_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct selector_data *mdata = data;
    int r;

    mdata->selection = mdata->cursor;

    if (!mdata->enabled)
        return 0;

    r = selector_format_do(node);
    SOL_INT_CHECK(r, < 0, r);

    if (!sol_ptr_vector_get_len(&mdata->items))
        return 0;

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_FORM_SELECTOR__OUT__SELECTED,
        sol_ptr_vector_get(&mdata->items, mdata->selection));
}

static int
selector_selected_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct selector_data *mdata = data;
    const char *value, *it;
    bool selected = false;
    uint16_t idx;
    int r;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->items, it, idx) {
        if (streq(it, value)) {
            mdata->selection = idx;
            selected = true;
        }
    }

    if (!selected) {
        r = sol_util_replace_str_if_changed(&mdata->pending_sel, value);
        SOL_INT_CHECK(r, < 0, r);
    }

    if (!mdata->enabled || mdata->pending_sel)
        return 0;

    return selector_format_do(node);
}

static int
selector_enabled_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct selector_data *mdata = data;
    bool value;
    int r;

    r = sol_flow_packet_get_bool(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->enabled = value;

    return 0;
}

struct boolean_data {
    char *title, *text_mem, *format, *title_tag, *value_tag, *true_str,
    *false_str;
    struct sol_buffer text_grid;
    size_t columns, rows;
    bool selection : 1;
    bool enabled : 1;
};

static int
boolean_format_do(struct sol_flow_node *node)
{
    struct boolean_data *mdata = sol_flow_node_get_private_data(node);
    int r, buf_size = mdata->text_grid.capacity;
    const char *it_value = NULL;
    bool no_more_space = false;
    size_t row = 0, col = 0;

    r = format_title(&mdata->text_grid, buf_size, mdata->rows, mdata->columns,
        &row, &col, mdata->format, mdata->title, mdata->title_tag,
        mdata->value_tag, &no_more_space);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    SOL_EXP_CHECK_GOTO(no_more_space, send);

    it_value = mdata->selection ? mdata->true_str : mdata->false_str;

    r = format_chunk(&mdata->text_grid, mdata->rows, mdata->columns,
        it_value, it_value + strlen(it_value), &row, &col, DO_FORMAT,
        DITCH_NL);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    if (r >= buf_size || row >= mdata->rows)
        goto send;

    r = format_post_value(&mdata->text_grid, mdata->rows, mdata->columns,
        &row, &col, mdata->format, mdata->value_tag);
    SOL_INT_CHECK_GOTO(r, < 0, err);

send:
    return format_send(node, &mdata->text_grid,
        SOL_FLOW_NODE_TYPE_FORM_BOOLEAN__OUT__STRING);

err:
    /* we got to re-init because of the error cases. if this function
     * fails we're no better, so don't check. */
    buffer_re_init(&mdata->text_grid, mdata->text_mem, mdata->rows,
        mdata->columns);

    return r;
}

static void
boolean_close(struct sol_flow_node *node, void *data)
{
    struct boolean_data *mdata = data;

    sol_buffer_fini(&mdata->text_grid);

    free(mdata->title);
    free(mdata->format);
    free(mdata->true_str);
    free(mdata->false_str);
}

static int
boolean_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct boolean_data *mdata = data;
    const struct sol_flow_node_type_form_boolean_options *opts =
        (const struct sol_flow_node_type_form_boolean_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_FORM_BOOLEAN_OPTIONS_API_VERSION, -EINVAL);

    mdata->true_str = strdup(opts->true_str);
    SOL_NULL_CHECK(mdata->true_str, -ENOMEM);

    mdata->selection = true;

    mdata->false_str = strdup(opts->false_str);
    if (!mdata->false_str) {
        free(mdata->true_str);
        return -ENOMEM;
    }

    r = common_form_init(&mdata->text_grid,
        opts->rows,
        &mdata->rows,
        opts->columns,
        &mdata->columns,
        opts->format,
        &mdata->format,
        opts->title,
        &mdata->title,
        &mdata->title_tag,
        &mdata->value_tag,
        &mdata->text_mem);
    SOL_INT_CHECK(r, < 0, r);

    mdata->enabled = true;

    return boolean_format_do(node);
}

static int
toggle_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct boolean_data *mdata = data;

    if (!mdata->enabled)
        return 0;

    mdata->selection = !mdata->selection;

    return boolean_format_do(node);
}

static int
boolean_selected_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct boolean_data *mdata = data;
    bool value;
    int r;

    r = sol_flow_packet_get_bool(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->selection = value;

    if (!mdata->enabled)
        return 0;

    return boolean_format_do(node);
}

static int
boolean_select_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct boolean_data *mdata = data;
    int r;

    if (!mdata->enabled)
        return 0;

    r = boolean_format_do(node);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_FORM_BOOLEAN__OUT__SELECTED,
        mdata->selection);
}

static int
boolean_enabled_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct boolean_data *mdata = data;
    bool value;
    int r;

    r = sol_flow_packet_get_bool(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->enabled = value;

    return 0;
}

struct integer_data {
    char *title, *text_mem, *format, *title_tag, *value_tag;
    struct sol_buffer text_grid;
    struct sol_irange state;
    size_t columns, rows;
    bool circular : 1;
    bool enabled : 1;
};

static int
integer_format_do(struct sol_flow_node *node)
{
    struct integer_data *mdata = sol_flow_node_get_private_data(node);
    int r, buf_size = mdata->text_grid.capacity;
    bool no_more_space = false;
    size_t row = 0, col = 0;
    char *it_value = NULL;

    r = format_title(&mdata->text_grid, buf_size, mdata->rows, mdata->columns,
        &row, &col, mdata->format, mdata->title, mdata->title_tag,
        mdata->value_tag, &no_more_space);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    SOL_EXP_CHECK_GOTO(no_more_space, send);

    r = asprintf(&it_value, "%" PRId32 "", mdata->state.val);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    r = format_chunk(&mdata->text_grid, mdata->rows, mdata->columns,
        it_value, it_value + strlen(it_value), &row, &col, DO_FORMAT,
        DITCH_NL);
    free(it_value);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    if (r >= buf_size || row >= mdata->rows)
        goto send;

    r = format_post_value(&mdata->text_grid, mdata->rows, mdata->columns,
        &row, &col, mdata->format, mdata->value_tag);
    SOL_INT_CHECK_GOTO(r, < 0, err);

send:
    return format_send(node, &mdata->text_grid,
        SOL_FLOW_NODE_TYPE_FORM_INT__OUT__STRING);

err:
    /* we got to re-init because of the error cases. if this function
     * fails we're no better, so don't check. */
    buffer_re_init(&mdata->text_grid, mdata->text_mem, mdata->rows,
        mdata->columns);

    return r;
}

static void
integer_close(struct sol_flow_node *node, void *data)
{
    struct integer_data *mdata = data;

    sol_buffer_fini(&mdata->text_grid);

    free(mdata->title);
    free(mdata->format);
}

static int
integer_common_open(struct sol_buffer *buf,
    struct sol_irange_spec range,
    int32_t start_value,
    int32_t rows,
    int32_t columns,
    const char *format,
    const char *title,
    struct integer_data *out)
{
    int64_t total_range;
    int r;

    if (range.min > range.max) {
        SOL_WRN("Maximum range value shouldn't be less than min."
            " Swapping values.");
        out->state.max = range.min;
        out->state.min = range.max;
    } else {
        out->state.max = range.max;
        out->state.min = range.min;
    }

    out->state.step = range.step;
    if (out->state.step == 0) {
        SOL_WRN("Step value must be non-zero. Assuming 1 for it.");
        out->state.step = 1;
    }

    total_range = (int64_t)out->state.max - (int64_t)out->state.min;
    if ((out->state.step > 0 && out->state.step > total_range)
        || (out->state.step < 0 && out->state.step < -total_range)) {
        SOL_WRN("Step value must fit the given range. Assuming 1 for it.");
        out->state.step = 1;
    }

    out->state.val = start_value;
    if (out->state.val < out->state.min) {
        SOL_INF("Start value must be in the given range"
            " (%" PRId32 "-%" PRId32 "). Assuming the minimum for it.",
            out->state.min, out->state.max);
        out->state.val = out->state.min;
    }
    if (out->state.val > out->state.max) {
        SOL_INF("Start value must be in the given range"
            " (%" PRId32 "-%" PRId32 "). Assuming the maximum for it.",
            out->state.min, out->state.max);
        out->state.val = out->state.max;
    }

    r = common_form_init(buf,
        rows,
        &out->rows,
        columns,
        &out->columns,
        format,
        &out->format,
        title,
        &out->title,
        &out->title_tag,
        &out->value_tag,
        &out->text_mem);
    SOL_INT_CHECK(r, < 0, r);

    out->enabled = true;

    return 0;
}

static int
integer_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct integer_data *mdata = data;
    const struct sol_flow_node_type_form_int_options *opts =
        (const struct sol_flow_node_type_form_int_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_FORM_INT_OPTIONS_API_VERSION, -EINVAL);

    r = integer_common_open(&mdata->text_grid, opts->range, opts->start_value,
        opts->rows, opts->columns, opts->format, opts->title, mdata);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    mdata->circular = opts->circular;

    return integer_format_do(node);

err:
    integer_close(node, mdata);

    return r;
}

/* Either step > 0 && step < max-min or step < 0 && step > min-max */

static int
integer_up_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct integer_data *mdata = data;

    if (!mdata->enabled)
        return 0;

    if (mdata->state.step > 0) {
        /* step > 0 && max - step > min, so no overflow */
        if (mdata->state.val <= mdata->state.max - mdata->state.step) {
            mdata->state.val += mdata->state.step;
        } else {
            if (mdata->circular)
                mdata->state.val = mdata->state.min;
        }
    } else {
        /* step < 0 && min - step > max, so no overflow */
        if (mdata->state.val >= mdata->state.min - mdata->state.step) {
            mdata->state.val += mdata->state.step;
        } else {
            if (mdata->circular)
                mdata->state.val = mdata->state.max;
        }
    }

    return integer_format_do(node);
}

static int
integer_down_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct integer_data *mdata = data;

    if (!mdata->enabled)
        return 0;

    if (mdata->state.step > 0) {
        /* step > 0 && min + step < max, so no overflow */
        if (mdata->state.val >= mdata->state.min + mdata->state.step) {
            mdata->state.val -= mdata->state.step;
        } else {
            if (mdata->circular)
                mdata->state.val = mdata->state.max;
        }
    } else {
        /* step < 0 && max + step < min, so no overflow */
        if (mdata->state.val <= mdata->state.max + mdata->state.step) {
            mdata->state.val -= mdata->state.step;
        } else {
            if (mdata->circular)
                mdata->state.val = mdata->state.min;
        }
    }

    return integer_format_do(node);
}

static int
integer_selected_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct integer_data *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->state.val = value;
    if (mdata->state.val > mdata->state.max)
        mdata->state.val = mdata->state.max;
    if (mdata->state.val < mdata->state.min)
        mdata->state.val = mdata->state.min;

    if (!mdata->enabled)
        return 0;

    return integer_format_do(node);
}

static int
integer_select_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct integer_data *mdata = data;
    int r;

    if (!mdata->enabled)
        return 0;

    r = integer_format_do(node);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_FORM_INT__OUT__SELECTED,
        &mdata->state);
}

static int
integer_enabled_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct integer_data *mdata = data;
    bool value;
    int r;

    r = sol_flow_packet_get_bool(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->enabled = value;

    return 0;
}

struct integer_custom_data {
    struct integer_data base;
    struct sol_timeout *timer;
    char *chars;
    size_t cursor_row, cursor_col, value_prefix_len;
    uint32_t blink_time;
    uint8_t n_digits;
    bool blink_on : 1;
    bool state_changed : 1;
    bool cursor_initialized : 1;
};

static int
integer_custom_format_do(struct sol_flow_node *node)
{
    struct integer_custom_data *mdata = sol_flow_node_get_private_data(node);
    int r, buf_size = mdata->base.text_grid.capacity;
    bool no_more_space = false;
    size_t row = 0, col = 0, tmp;
    char *it_value = NULL;

    if (!mdata->state_changed) {
        char *value;
        size_t pos;

        if (mdata->cursor_col > mdata->base.columns - 1)
            goto send;

        pos = coords_to_pos
                (mdata->base.columns, mdata->cursor_row, mdata->cursor_col);

        value = (char *)sol_buffer_at(&mdata->base.text_grid, pos);

        if (mdata->blink_on) {
            mdata->blink_on = false;
            *value = SPC;
        } else {
            mdata->blink_on = true;
            pos -= coords_to_pos(mdata->base.columns, mdata->cursor_row, 0);
            pos -= mdata->value_prefix_len;
            *value = mdata->chars[pos];
        }

        goto send;
    }

    r = format_title(&mdata->base.text_grid, buf_size, mdata->base.rows,
        mdata->base.columns, &row, &col, mdata->base.format, mdata->base.title,
        mdata->base.title_tag, mdata->base.value_tag, &no_more_space);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    SOL_EXP_CHECK_GOTO(no_more_space, send);

    mdata->value_prefix_len = col;
    if (mdata->base.state.val < 0)
        mdata->value_prefix_len++;

    if (mdata->base.state.val >= 0) {
        r = asprintf(&it_value, "%.*" PRId32 "",
            mdata->n_digits, mdata->base.state.val);
    } else {
        r = asprintf(&it_value, "% .*" PRId32 "",
            mdata->n_digits, mdata->base.state.val);
    }
    SOL_INT_CHECK_GOTO(r, < 0, err);

    if (mdata->base.state.val >= 0)
        memcpy(mdata->chars, it_value, mdata->n_digits);
    else
        memcpy(mdata->chars, it_value + 1, mdata->n_digits);

    tmp = strlen(it_value);
    if (!mdata->cursor_initialized) {
        mdata->cursor_row = row;
        mdata->cursor_col = mdata->base.text_grid.used
            - coords_to_pos(mdata->base.columns, mdata->cursor_row, 0)
            + tmp - 1;
        mdata->cursor_initialized = true;
    }
    r = format_chunk(&mdata->base.text_grid, mdata->base.rows,
        mdata->base.columns, it_value, it_value + tmp, &row, &col,
        DO_FORMAT, DITCH_NL);
    free(it_value);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    if (r >= buf_size || row >= mdata->base.rows)
        goto send;

    r = format_post_value(&mdata->base.text_grid, mdata->base.rows,
        mdata->base.columns, &row, &col, mdata->base.format,
        mdata->base.value_tag);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    mdata->state_changed = false;

send:
    r = format_send(node, &mdata->base.text_grid,
        SOL_FLOW_NODE_TYPE_FORM_INT__OUT__STRING);

    return r;

err:
    /* we got to re-init because of the error cases. if this function
     * fails we're no better, so don't check. */
    buffer_re_init(&mdata->base.text_grid, mdata->base.text_mem,
        mdata->base.rows, mdata->base.columns);

    return r;
}

static bool
integer_custom_timeout(void *data)
{
    return integer_custom_format_do(data) == 0;
}

static void
integer_custom_force_imediate_format(struct integer_custom_data *mdata,
    bool re_init)
{
    if (re_init)
        buffer_re_init(&mdata->base.text_grid, mdata->base.text_mem,
            mdata->base.rows, mdata->base.columns);
    if (mdata->timer) {
        sol_timeout_del(mdata->timer);
        mdata->timer = NULL;
    }
}

static int
integer_custom_format(struct sol_flow_node *node)
{
    struct integer_custom_data *mdata = sol_flow_node_get_private_data(node);

    if (!mdata->timer) {
        mdata->timer = sol_timeout_add
                (mdata->blink_time, integer_custom_timeout, node);
        SOL_NULL_CHECK(mdata->timer, -ENOMEM);
        return integer_custom_format_do(node);
    }

    return 0;
}

static void
integer_custom_close(struct sol_flow_node *node, void *data)
{
    struct integer_custom_data *mdata = data;

    sol_buffer_fini(&mdata->base.text_grid);
    free(mdata->chars);

    if (mdata->timer)
        sol_timeout_del(mdata->timer);

    free(mdata->base.title);
    free(mdata->base.format);
}

static int
integer_custom_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    char sbuf[2];
    int n_max, n_min;
    struct integer_custom_data *mdata = data;
    const struct sol_flow_node_type_form_int_custom_options *opts =
        (const struct sol_flow_node_type_form_int_custom_options *)options,
    *def_opts;

    def_opts = node->type->default_options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_FORM_INT_OPTIONS_API_VERSION, -EINVAL);

    r = integer_common_open(&mdata->base.text_grid, opts->range,
        opts->start_value, opts->rows, opts->columns, opts->format,
        opts->title, &mdata->base);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    mdata->blink_time = opts->blink_time;
    if (opts->blink_time < 0) {
        SOL_WRN("Invalid blink_time (%" PRId32 "), that must be positive. "
            "Setting to %" PRId32 ".", opts->blink_time, def_opts->blink_time);
        mdata->blink_time = def_opts->blink_time;
    }

    mdata->blink_on = true;

    /* outputs truncated on purpose -- only the return value matters */
#pragma GCC diagnostic ignored "-Wformat-truncation"
    n_max = snprintf(sbuf, 1, "%+" PRId32 "", mdata->base.state.max);
    SOL_INT_CHECK_GOTO(n_max, < 0, err);

    n_min = snprintf(sbuf, 1, "%+" PRId32 "", mdata->base.state.min);
    SOL_INT_CHECK_GOTO(n_min, < 0, err);
#pragma GCC diagnostic pop

    /* -1 to take away sign */
    mdata->n_digits = sol_util_max(n_min, n_max) - 1;

    mdata->chars = calloc(1, mdata->n_digits);
    SOL_NULL_CHECK_GOTO(mdata->chars, err);

    mdata->state_changed = true;

    return integer_custom_format(node);

err:
    integer_custom_close(node, mdata);

    return r;
}

static int64_t
ten_pow(unsigned int exp)
{
    int64_t result = 1;

    while (exp) {
        result *= 10;
        exp--;
    }

    return result;
}

static int
char_remove(struct integer_custom_data *mdata,
    size_t cursor_pos,
    char *out)
{
    int64_t tmp;
    bool negative = mdata->base.state.val < 0;

    *out = mdata->chars[cursor_pos];
    tmp = ten_pow(mdata->n_digits - cursor_pos - 1);
    if (tmp > INT32_MAX)
        return -EDOM;

    tmp = (int64_t)(*out - '0') * tmp;
    if ((negative
        && tmp > (int64_t)mdata->base.state.max - mdata->base.state.val)
        || (!negative
        && tmp > (int64_t)mdata->base.state.val - mdata->base.state.min))
        return -EDOM;

    if (!negative)
        mdata->base.state.val -= (int32_t)tmp;
    else
        mdata->base.state.val += (int32_t)tmp;

    return 0;
}

static int
char_re_insert(struct integer_custom_data *mdata,
    size_t cursor_pos,
    char c,
    bool negative)
{
    int64_t tmp;

    tmp = (int64_t)(c - '0') * ten_pow(mdata->n_digits - cursor_pos - 1);
    if ((!negative && tmp > mdata->base.state.max - mdata->base.state.val)
        || (negative
        && tmp > (int64_t)mdata->base.state.val - mdata->base.state.min))
        return -EDOM;

    if (!negative)
        mdata->base.state.val += (int32_t)tmp;
    else
        mdata->base.state.val -= (int32_t)tmp;

    return 0;
}

static void
cursor_pos_calc(size_t columns,
    size_t cursor_row,
    size_t cursor_col,
    size_t prefix_len,
    size_t *cursor_pos)
{
    *cursor_pos = coords_to_pos(columns, cursor_row, cursor_col);
    *cursor_pos -= coords_to_pos(columns, cursor_row, 0);
    *cursor_pos -= prefix_len;
}

static int
digit_flip_post(struct sol_flow_node *node,
    size_t cursor_pos,
    char c,
    char orig_c,
    bool negative)
{
    int r;
    struct integer_custom_data *mdata = sol_flow_node_get_private_data(node);

    mdata->chars[cursor_pos] = c;

    r = char_re_insert(mdata, cursor_pos, c, negative);
    if (r < 0) {
        char_re_insert(mdata, cursor_pos, orig_c, negative);
        c = orig_c;
        mdata->chars[cursor_pos] = orig_c;

        return sol_flow_send_empty_packet(node,
            SOL_FLOW_NODE_TYPE_FORM_INT__OUT__OUT_OF_RANGE);
    }

    mdata->state_changed = true;
    mdata->blink_on = true;

    integer_custom_force_imediate_format(mdata, true);
    return integer_custom_format(node);
}

static int
integer_custom_up_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    char c, old_c;
    size_t cursor_pos = 0;
    bool negative, sign_change = false;
    struct integer_custom_data *mdata = data;

    if (!mdata->base.enabled)
        return 0;

    negative = mdata->base.state.val < 0;

    cursor_pos_calc(mdata->base.columns, mdata->cursor_row, mdata->cursor_col,
        mdata->value_prefix_len, &cursor_pos);

    if ((mdata->base.state.val == -1 || mdata->base.state.val == -9)
        && cursor_pos == (size_t)(mdata->n_digits - 1))
        sign_change = true;

    r = char_remove(mdata, cursor_pos, &c);
    if (r < 0)
        return sol_flow_send_empty_packet(node,
            SOL_FLOW_NODE_TYPE_FORM_INT__OUT__OUT_OF_RANGE);

    old_c = c;

    c++;
    if (c > '9')
        c = '0';

    if (negative && sign_change)
        mdata->cursor_initialized = false;

    return digit_flip_post(node, cursor_pos, c, old_c, negative);
}

static int
integer_custom_down_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    char c, old_c;
    size_t cursor_pos;
    bool negative, sign_change = false;
    struct integer_custom_data *mdata = data;

    if (!mdata->base.enabled)
        return 0;

    negative = mdata->base.state.val < 0;

    cursor_pos_calc(mdata->base.columns, mdata->cursor_row, mdata->cursor_col,
        mdata->value_prefix_len, &cursor_pos);

    if ((mdata->base.state.val == 0 || mdata->base.state.val == -1)
        && cursor_pos == (size_t)(mdata->n_digits - 1))
        sign_change = true;

    r = char_remove(mdata, cursor_pos, &c);
    if (r < 0)
        return sol_flow_send_empty_packet(node,
            SOL_FLOW_NODE_TYPE_FORM_INT__OUT__OUT_OF_RANGE);

    old_c = c;

    c--;
    if (c < '0')
        c = '9';

    if (sign_change) {
        /* We're going from -1 to 0. Since the least significant digit
         * has already been taken, stay on zero. */
        if (negative)
            c = '0';
        /* We're going from 0 to -1. We want to add (minus) 1, then. */
        else
            c = '1';
        negative = !negative;
        mdata->cursor_initialized = false;
    }

    return digit_flip_post(node, cursor_pos, c, old_c, negative);
}

static int
integer_custom_next_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct integer_custom_data *mdata = data;

    if (!mdata->base.enabled)
        return 0;

    mdata->cursor_col++;
    if (mdata->cursor_col >
        (size_t)(mdata->n_digits + mdata->value_prefix_len - 1)) {
        mdata->cursor_col--;
        return 0;
    }

    mdata->state_changed = true;
    mdata->blink_on = true;

    integer_custom_force_imediate_format(mdata, true);
    return integer_custom_format(node);
}

static int
integer_custom_previous_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct integer_custom_data *mdata = data;

    if (!mdata->base.enabled)
        return 0;

    if (mdata->cursor_col > (mdata->base.state.val > 0 ? 0 : 1))
        mdata->cursor_col--;
    else
        return 0;

    mdata->state_changed = true;
    mdata->blink_on = true;

    integer_custom_force_imediate_format(mdata, true);
    return integer_custom_format(node);
}

static int
sign_toggle(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct integer_custom_data *mdata = data;
    int64_t tmp;

    if (!mdata->base.enabled)
        return 0;

    tmp = mdata->base.state.val * -1;

    if (tmp < 0) {
        if (tmp < mdata->base.state.min)
            mdata->base.state.val = mdata->base.state.min;
        else
            mdata->base.state.val = tmp;
    } else {
        if (tmp > mdata->base.state.max)
            mdata->base.state.val = mdata->base.state.max;
        else
            mdata->base.state.val = tmp;
    }

    mdata->state_changed = true;
    mdata->blink_on = true;
    /* calculate it again, now accounting for sign char */
    mdata->cursor_initialized = false;

    integer_custom_force_imediate_format(mdata, true);
    return integer_custom_format(node);
}

static int
integer_custom_selected_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct integer_custom_data *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->base.state.val = value;
    if (mdata->base.state.val > mdata->base.state.max)
        mdata->base.state.val = mdata->base.state.max;
    if (mdata->base.state.val < mdata->base.state.min)
        mdata->base.state.val = mdata->base.state.min;

    if (!mdata->base.enabled)
        return 0;

    /* force from scratch because sign may change */
    integer_custom_force_imediate_format(mdata, true);
    mdata->state_changed = true;
    mdata->blink_on = true;

    return integer_custom_format(node);
}

static int
integer_custom_select_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct integer_custom_data *mdata = data;
    int r;

    if (!mdata->base.enabled)
        return 0;

    /* force new format with state changed and blink state on, so we
     * always get the full output here */
    integer_custom_force_imediate_format(mdata, false);
    mdata->state_changed = true;
    mdata->blink_on = true;

    r = integer_custom_format(node);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_FORM_INT__OUT__SELECTED,
        &mdata->base.state);
}

static int
integer_custom_enabled_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct integer_custom_data *mdata = data;
    bool value;
    int r;

    r = sol_flow_packet_get_bool(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->base.enabled = value;

    return 0;
}

/* let's use a custom sentinel ptr value, instead of NULL, to avoid
 * hiding bugs on sol_ptr_vector_get() code paths */
#define EMPTY_SENTINEL ((char *)0xdeadbeef)

struct string_data {
    size_t columns, rows, cursor_row, cursor_col, value_prefix_len,
        hidden_prefix_len, min_length, max_length;
    char *title, *text_mem, *format, *title_tag, *value_tag;
    struct sol_buffer text_grid;
    struct sol_ptr_vector chars; /* ptrs to charset or void sentinel */
    struct sol_timeout *timer;
    int blink_time;
    char *charset;
    bool enabled : 1;
    bool blink_on : 1;
    bool state_changed : 1;
    bool cursor_initialized : 1;
};

static int
string_format_do(struct sol_flow_node *node)
{
    struct string_data *mdata = sol_flow_node_get_private_data(node);
    int r, buf_size = mdata->text_grid.capacity;
    char *it_value = NULL, *tmp = NULL;
    bool no_more_space = false;
    size_t row = 0, col = 0;
    bool empty = false;
    uint16_t len;

    if (!mdata->state_changed) {
        char *value;
        size_t pos;

        if (mdata->cursor_col - mdata->hidden_prefix_len > mdata->columns - 1)
            goto send;

        pos = coords_to_pos
                (mdata->columns, mdata->cursor_row, mdata->cursor_col);

        value = (char *)sol_buffer_at(&mdata->text_grid, pos);

        if (mdata->blink_on) {
            mdata->blink_on = false;
            *value = UNDERSCORE;
        } else {
            char *v;

            mdata->blink_on = true;
            pos -= coords_to_pos(mdata->columns, mdata->cursor_row, 0);
            pos -= mdata->value_prefix_len;
            pos += mdata->hidden_prefix_len;

            v = sol_ptr_vector_get(&mdata->chars, pos);
            *value = v != EMPTY_SENTINEL ? *v : SPC;
        }

        goto send;
    }

    r = format_title(&mdata->text_grid, buf_size, mdata->rows,
        mdata->columns, &row, &col, mdata->format, mdata->title,
        mdata->title_tag, mdata->value_tag, &no_more_space);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    SOL_EXP_CHECK_GOTO(no_more_space, send);

    mdata->value_prefix_len = col;

    empty = (sol_ptr_vector_get(&mdata->chars, 0) == EMPTY_SENTINEL);
    len = sol_ptr_vector_get_len(&mdata->chars);
    it_value = malloc(len + 1);
    SOL_NULL_CHECK_GOTO(it_value, err);

    if (empty)
        it_value[0] = SPC;
    else
        for (int i = 0; i < len; i++) {
            char *v = sol_ptr_vector_get(&mdata->chars, i);
            it_value[i] = *v;
        }

    it_value[len] = NUL;

    if (!mdata->cursor_initialized) {
        mdata->cursor_row = row;
        mdata->cursor_col = mdata->text_grid.used
            - coords_to_pos(mdata->columns, mdata->cursor_row, 0) + len - 1;
        mdata->cursor_initialized = true;
    }

    tmp = it_value;
    len -= mdata->hidden_prefix_len;
    tmp += mdata->hidden_prefix_len;
    r = format_chunk(&mdata->text_grid, mdata->rows,
        mdata->columns, tmp, tmp + len, &row, &col, DO_FORMAT, DITCH_NL);
    free(it_value);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    if (r >= buf_size || row >= mdata->rows)
        goto send;

    r = format_post_value(&mdata->text_grid, mdata->rows,
        mdata->columns, &row, &col, mdata->format,
        mdata->value_tag);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    mdata->state_changed = false;

send:
    r = format_send(node, &mdata->text_grid,
        SOL_FLOW_NODE_TYPE_FORM_INT__OUT__STRING);

    return r;

err:
    /* we got to re-init because of the error cases. if this function
     * fails we're no better, so don't check. */
    buffer_re_init(&mdata->text_grid, mdata->text_mem,
        mdata->rows, mdata->columns);

    return r;
}

static bool
string_timeout(void *data)
{
    return string_format_do(data) == 0;
}

static void
string_force_imediate_format(struct string_data *mdata, bool re_init)
{
    if (re_init)
        buffer_re_init(&mdata->text_grid, mdata->text_mem,
            mdata->rows, mdata->columns);
    if (mdata->timer) {
        sol_timeout_del(mdata->timer);
        mdata->timer = NULL;
    }
}

static int
string_format(struct sol_flow_node *node)
{
    struct string_data *mdata = sol_flow_node_get_private_data(node);

    if (!mdata->timer) {
        mdata->timer = sol_timeout_add
                (mdata->blink_time, string_timeout, node);
        SOL_NULL_CHECK(mdata->timer, -ENOMEM);
        return string_format_do(node);
    }

    return 0;
}

static void
string_close(struct sol_flow_node *node, void *data)
{
    struct string_data *mdata = data;

    sol_buffer_fini(&mdata->text_grid);
    sol_ptr_vector_clear(&mdata->chars);

    if (mdata->timer)
        sol_timeout_del(mdata->timer);

    free(mdata->charset);
    free(mdata->title);
    free(mdata->format);
}

static int
string_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    size_t start_len;
    struct string_data *mdata = data;
    const struct sol_flow_node_type_form_string_options *opts =
        (const struct sol_flow_node_type_form_string_options *)options,
    *def_opts;

    def_opts = node->type->default_options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_FORM_INT_OPTIONS_API_VERSION, -EINVAL);

    r = common_form_init(&mdata->text_grid,
        opts->rows,
        &mdata->rows,
        opts->columns,
        &mdata->columns,
        opts->format,
        &mdata->format,
        opts->title,
        &mdata->title,
        &mdata->title_tag,
        &mdata->value_tag,
        &mdata->text_mem);
    SOL_INT_CHECK(r, < 0, r);

    mdata->enabled = true;

    mdata->blink_time = opts->blink_time;
    if (opts->blink_time < 0) {
        SOL_WRN("Invalid blink_time (%" PRId32 "), that must be positive. "
            "Setting to %" PRId32 "", opts->blink_time, def_opts->blink_time);
        mdata->blink_time = def_opts->blink_time;
    }

    mdata->min_length = opts->min_length;
    if (opts->min_length < 0) {
        SOL_WRN("Invalid minimum output size (%" PRId32 "), "
            "that must be positive. Setting to %" PRId32 ".",
            opts->min_length, def_opts->min_length);
        mdata->min_length = def_opts->min_length;
    }

    mdata->max_length = opts->max_length;
    if (opts->max_length < 0) {
        SOL_WRN("Invalid maximum output size (%" PRId32 "), "
            "that must be positive. Setting to %" PRId32 ".",
            opts->max_length, def_opts->max_length);
        mdata->max_length = def_opts->max_length;
    }

    if (mdata->max_length && mdata->max_length < mdata->min_length) {
        SOL_WRN("Invalid maximum output size (%" PRId32 "), "
            "that must be greater than the minimum (%" PRId32 ")."
            " Setting both of them to that minimum value.",
            opts->max_length, def_opts->max_length);
        mdata->max_length = mdata->min_length;
    }

    mdata->blink_on = true;
    mdata->state_changed = true;

    if (!strlen(opts->charset)) {
        SOL_WRN("The char set must not be empty, fallbacking to default one");
        mdata->charset = strdup(def_opts->charset);
    } else
        mdata->charset = strdup(opts->charset);
    if (!mdata->charset) {
        r = -ENOMEM;
        goto err;
    }

    sol_ptr_vector_init(&mdata->chars);

    start_len = opts->start_value ? strlen(opts->start_value) : 0;

    if (!start_len && !mdata->min_length) {
        /* start at empty state */
        r = sol_ptr_vector_append(&mdata->chars, EMPTY_SENTINEL);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    } else {
        size_t charset_len = strlen(mdata->charset);
        size_t start_cnt = 0;

        size_t charset_cnt = 0;

        for (; start_cnt < start_len; start_cnt++) {
            bool found = false;

            if (mdata->max_length && start_cnt >= mdata->max_length)
                goto end;

            for (charset_cnt = 0; charset_cnt < charset_len;
                charset_cnt++) {
                if (mdata->charset[charset_cnt]
                    == opts->start_value[start_cnt]) {
                    found = true;
                    break;
                }
            }
            r = sol_ptr_vector_append(&mdata->chars,
                found ? &mdata->charset[charset_cnt] : mdata->charset);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        }

        for (size_t i = start_cnt; i < mdata->min_length; i++) {
            if (mdata->max_length && start_cnt >= mdata->max_length)
                goto end;

            r = sol_ptr_vector_append(&mdata->chars, mdata->charset);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        }
    }

end:
    return string_format(node);

err:
    string_close(node, mdata);

    return r;
}

static int
string_up_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    char *value;
    size_t cursor_pos = 0;
    struct string_data *mdata = data;

    if (!mdata->enabled)
        return 0;

    cursor_pos_calc(mdata->columns, mdata->cursor_row, mdata->cursor_col,
        mdata->value_prefix_len, &cursor_pos);

    cursor_pos += mdata->hidden_prefix_len;

    value = sol_ptr_vector_get(&mdata->chars, cursor_pos);
    if (value == EMPTY_SENTINEL) {
        value = mdata->charset;
        goto set;
    }

    value++;
    if ((size_t)(value - mdata->charset) >= strlen(mdata->charset))
        value = mdata->charset;

set:
    r = sol_ptr_vector_set(&mdata->chars, cursor_pos, value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->state_changed = true;
    mdata->blink_on = true;

    string_force_imediate_format(mdata, true);
    return string_format(node);
}

static int
string_down_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    char *value;
    size_t cursor_pos = 0;
    struct string_data *mdata = data;

    if (!mdata->enabled)
        return 0;

    cursor_pos_calc(mdata->columns, mdata->cursor_row, mdata->cursor_col,
        mdata->value_prefix_len, &cursor_pos);

    cursor_pos += mdata->hidden_prefix_len;

    value = sol_ptr_vector_get(&mdata->chars, cursor_pos);
    if (value == EMPTY_SENTINEL)
        goto last;

    value--;
    if (value >= mdata->charset)
        goto set;

last:
    value = mdata->charset + strlen(mdata->charset) - 1;

set:
    r = sol_ptr_vector_set(&mdata->chars, cursor_pos, value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->state_changed = true;
    mdata->blink_on = true;

    string_force_imediate_format(mdata, true);
    return string_format(node);
}

static int
string_next_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_data *mdata = data;
    size_t cursor_pos = 0, len = sol_ptr_vector_get_len(&mdata->chars);
    size_t cursor_col, hidden_prefix_len;
    char *v;

    if (!mdata->enabled)
        return 0;

    v = sol_ptr_vector_get(&mdata->chars, 0);
    if (v == EMPTY_SENTINEL)
        return 0;

    /* backups -- only commit changes if we can advance */
    cursor_col = mdata->cursor_col;
    hidden_prefix_len = mdata->hidden_prefix_len;

    if (mdata->cursor_col >= mdata->columns - 1)
        hidden_prefix_len++;
    else
        cursor_col++;

    cursor_pos_calc(mdata->columns, mdata->cursor_row, cursor_col,
        mdata->value_prefix_len, &cursor_pos);

    cursor_pos += hidden_prefix_len;

    if (cursor_pos > len - 1) {
        int r;

        if (mdata->max_length && cursor_pos >= mdata->max_length)
            return 0;

        r = sol_ptr_vector_append(&mdata->chars, mdata->charset);
        SOL_INT_CHECK(r, < 0, r);
    }

    mdata->hidden_prefix_len = hidden_prefix_len;
    mdata->cursor_col = cursor_col;

    mdata->state_changed = true;
    mdata->blink_on = true;

    string_force_imediate_format(mdata, true);
    return string_format(node);
}

static int
string_previous_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_data *mdata = data;

    if (!mdata->enabled)
        return 0;

    if (mdata->cursor_col > 0) {
        if (mdata->hidden_prefix_len)
            mdata->hidden_prefix_len--;
        else
            mdata->cursor_col--;
    } else {
        char *v = sol_ptr_vector_get(&mdata->chars, 0);
        if (v != EMPTY_SENTINEL && sol_ptr_vector_get_len(&mdata->chars) == 1) {
            int r = sol_ptr_vector_set(&mdata->chars, 0, EMPTY_SENTINEL);
            SOL_INT_CHECK(r, < 0, r);
        } else
            return 0;
    }

    mdata->state_changed = true;
    mdata->blink_on = true;

    string_force_imediate_format(mdata, true);
    return string_format(node);
}

static int
string_selected_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_data *mdata = data;
    size_t len, set_len;
    const char *value;
    int r;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    sol_ptr_vector_clear(&mdata->chars);

    set_len = strlen(mdata->charset);
    len = strlen(value);
    for (size_t i = 0; i < len; i++) {
        bool found = false;
        for (size_t j = 0; j < set_len; j++) {
            if (value[i] == mdata->charset[j]) {
                r = sol_ptr_vector_append(&mdata->chars, mdata->charset + j);
                SOL_INT_CHECK_GOTO(r, < 0, error);
                found = true;
                continue;
            }
        }
        if (!found) {
            /* a character not in the charset occurred, arbitrate
             * charset[0] */
            r = sol_ptr_vector_append(&mdata->chars, mdata->charset);
            SOL_INT_CHECK_GOTO(r, < 0, error);
        }
    }

    if (!mdata->enabled)
        return 0;

    string_force_imediate_format(mdata, true);
    mdata->state_changed = true;
    mdata->blink_on = true;

    return string_format(node);

error:
    sol_ptr_vector_clear(&mdata->chars);
    return r;
}

static int
string_select_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_data *mdata = data;
    char *value;
    size_t len;
    int r;

    if (!mdata->enabled)
        return 0;

    /* force new format with state changed and blink state on, so we
     * always get the full output here */
    string_force_imediate_format(mdata, false);
    mdata->state_changed = true;
    mdata->blink_on = true;

    r = string_format(node);
    SOL_INT_CHECK(r, < 0, r);

    if (sol_ptr_vector_get(&mdata->chars, 0) == EMPTY_SENTINEL) {
        value = (char *)EMPTY_STR;
        return sol_flow_send_string_packet(node,
            SOL_FLOW_NODE_TYPE_FORM_INT__OUT__SELECTED, value);
    }

    len = sol_ptr_vector_get_len(&mdata->chars);
    value = malloc(len + 1);
    SOL_NULL_CHECK(value, -ENOMEM);

    for (size_t i = 0; i < len; i++) {
        char *v = sol_ptr_vector_get(&mdata->chars, i);
        value[i] = *v;
    }
    value[len] = NUL;

    return sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_FORM_INT__OUT__SELECTED, value);
}

static int
string_enabled_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_data *mdata = data;
    bool value;
    int r;

    r = sol_flow_packet_get_bool(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->enabled = value;

    return 0;
}

static int
string_delete(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_data *mdata = data;
    size_t cursor_pos = 0, len = sol_ptr_vector_get_len(&mdata->chars);
    int r;

    if (!mdata->enabled || len <= mdata->min_length)
        return 0;

    cursor_pos_calc(mdata->columns, mdata->cursor_row, mdata->cursor_col,
        mdata->value_prefix_len, &cursor_pos);

    if (!cursor_pos) {
        char *v = sol_ptr_vector_get(&mdata->chars, cursor_pos);
        if (v == EMPTY_SENTINEL)
            return 0;
        else if (len == 1) {
            r = sol_ptr_vector_set(&mdata->chars, cursor_pos, EMPTY_SENTINEL);
            SOL_INT_CHECK(r, < 0, r);
            goto send;
        } else {
            r = sol_ptr_vector_del(&mdata->chars, cursor_pos);
            SOL_INT_CHECK(r, < 0, r);
            if (mdata->hidden_prefix_len)
                mdata->hidden_prefix_len--;
            goto send;
        }
    }

    cursor_pos += mdata->hidden_prefix_len;

    r = sol_ptr_vector_del(&mdata->chars, cursor_pos);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->hidden_prefix_len)
        mdata->hidden_prefix_len--;
    else
        mdata->cursor_col--;

send:
    mdata->state_changed = true;
    mdata->blink_on = true;

    string_force_imediate_format(mdata, true);
    return string_format(node);
}

#include "form-gen.c"
