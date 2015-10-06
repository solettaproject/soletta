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

#include "sol-flow/form.h"
#include "sol-flow-internal.h"

#include "sol-util.h"
#include "sol-buffer.h"

#define DITCH_NL (true)
#define KEEP_NL (false)

#define DO_FORMAT (false)
#define CALC_ONLY (true)

static const char CR = '\r', NL = '\n', SPC = ' ', NUL = '\0';
static const char TITLE_TAG[] = "{title}", VALUE_TAG[] = "{value}";

struct selector_data {
    char *title, *sel_mark, *cursor_mark, *text_mem;
    char *format, *title_tag, *value_tag;
    struct sol_buffer text_grid;
    struct sol_ptr_vector items;
    uint16_t selection, cursor, n_values;
    size_t columns, rows;
    bool circular : 1;
    bool enabled : 1;
    bool n_values_done : 1;
};

static inline size_t
get_buf_size(struct selector_data *mdata)
{
    /* +1 on cols for '\n' and (final) NUL chars */
    return mdata->rows * (mdata->columns + 1);
}

static inline void
buffer_re_init(struct selector_data *mdata)
{
    /* We choose to do the ending nul byte ourselves, since we're
     * doing fixed capacity, to able to set the second to last byte on
     * the buffer without reallocation attempts. */
    sol_buffer_init_flags(&mdata->text_grid, mdata->text_mem,
        get_buf_size(mdata), SOL_BUFFER_FLAGS_FIXED_CAPACITY |
        SOL_BUFFER_FLAGS_NO_NUL_BYTE);
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

    if (opts->columns.val <= 0) {
        SOL_WRN("Selector columns number must be a positive integer, "
            "but %d was given. Fallbacking to minimum value of 1.",
            opts->columns.val);
        mdata->columns = 1;
    } else
        mdata->columns = opts->columns.val;

    if (opts->rows.val <= 0) {
        SOL_WRN("Selector rows number must be a positive integer, "
            "but %d was given. Fallbacking to minimum value of 1.",
            opts->rows.val);
        mdata->rows = 1;
    } else
        mdata->rows = opts->rows.val;

    mdata->format = strdup(opts->format);
    SOL_NULL_CHECK(mdata->format, -ENOMEM);

    mdata->title_tag = strstr(mdata->format, TITLE_TAG);
    mdata->value_tag = strstr(mdata->format, VALUE_TAG);

    if (!mdata->value_tag) {
        SOL_WRN("Bad format, no {value} tag: %s. Fallbacking to "
            "pristine one, i. e. '{value}'.", mdata->format);
        free(mdata->format);
        mdata->format = strdup("{value}");
        SOL_NULL_CHECK(mdata->format, -ENOMEM);
    }

    if (mdata->title_tag > mdata->value_tag) {
        SOL_WRN("Bad format, {title} tag placed after {value} tag: %s."
            " Fallbacking to pristine one, i. e. '{value}'.",
            mdata->format);
        free(mdata->format);
        mdata->format = strdup("{value}");
        SOL_NULL_CHECK(mdata->format, -ENOMEM);
    }

    mdata->text_mem = calloc(1, get_buf_size(mdata));
    if (!mdata->text_mem) {
        r = -ENOMEM;
        goto err_format;
    }

    if (opts->title) {
        mdata->title = strdup(opts->title);
        if (!mdata->title) {
            r = -ENOMEM;
            goto err_title;
        }
    }

    if (opts->selection_marker) {
        mdata->sel_mark = strdup(opts->selection_marker);
        if (!mdata->sel_mark) {
            r = -ENOMEM;
            goto err_sel_mark;
        }
    }

    if (opts->cursor_marker) {
        mdata->cursor_mark = strdup(opts->cursor_marker);
        if (!mdata->cursor_mark) {
            r = -ENOMEM;
            goto err_cursor_mark;
        }
    }

    buffer_re_init(mdata);

    sol_ptr_vector_init(&mdata->items);
    mdata->enabled = true;

    return 0;

err_cursor_mark:
    free(mdata->sel_mark);

err_sel_mark:
    free(mdata->title);

err_title:
    free(mdata->text_mem);

err_format:
    free(mdata->format);
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

    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->items, it, i)
        free(it);
    sol_ptr_vector_clear(&mdata->items);
}

static inline size_t
coords_to_pos(struct selector_data *mdata, int32_t r, int32_t c)
{
    /* account for extra col for (implicit) trailing NLs, thus + r */
    return (r * mdata->columns) + c + r;
}

#define CUR_POS (coords_to_pos(mdata, *row, *col))
#define CUR_EXTRA_COL (coords_to_pos(mdata, *row, mdata->columns - 1) + 1)

static int
fill_spaces(struct selector_data *mdata,
    size_t *row,
    size_t *col,
    size_t length)
{
    int r;

    while (*col < mdata->columns && length) {
        r = sol_buffer_set_char_at(&mdata->text_grid, CUR_POS, SPC);
        SOL_INT_CHECK(r, < 0, r);
        (*col)++;
        length--;
    }
    /* If we did not reach the row's end, we're done, otherwise we
     * must break the line */
    if (!length)
        return CUR_POS;
    r = sol_buffer_set_char_at(&mdata->text_grid, CUR_EXTRA_COL, NL);
    SOL_INT_CHECK(r, < 0, r);
    *col = 0;
    (*row)++;

    return CUR_POS;
}

static int
fill_line(struct selector_data *mdata,
    size_t *row,
    size_t *col,
    bool calc_only)
{
    int r;

    while (*col < mdata->columns) {
        if (!calc_only) {
            r = sol_buffer_set_char_at(&mdata->text_grid, CUR_POS, SPC);
            SOL_INT_CHECK(r, < 0, r);
        }
        (*col)++;
    }
    if (*row < mdata->rows - 1) {
        if (!calc_only) {
            r = sol_buffer_set_char_at
                    (&mdata->text_grid, CUR_EXTRA_COL, NL);
            SOL_INT_CHECK(r, < 0, r);
        }
    }
    (*row)++;
    *col = 0;

    return CUR_POS;
}

static int
go_to_new_line(struct selector_data *mdata, size_t *row, size_t *col)
{
    int r = 0;

    if (*col > 0)
        r = fill_line(mdata, row, col, DO_FORMAT);

    return r;
}

static int
format_chunk(struct selector_data *mdata,
    char **ptr,
    char *end_ptr,
    size_t *row,
    size_t *col,
    bool calc_only,
    bool ditch_new_lines)
{
    size_t sz = get_buf_size(mdata);
    int r;

    while (*ptr < end_ptr && CUR_POS < sz && *row < mdata->rows) {
        /* translate middle-of-line \n's to spaces till the end +
         * \n */
        if (**ptr == CR || **ptr == NL) {
            if (ditch_new_lines) {
                if (!calc_only) {
                    r = sol_buffer_set_char_at
                            (&mdata->text_grid, CUR_POS, SPC);
                    SOL_INT_CHECK(r, < 0, r);
                }
                (*col)++;
            } else {
                r = fill_line(mdata, row, col, calc_only);
                SOL_INT_CHECK(r, < 0, r);
            }
            if (**ptr == CR)
                (*ptr)++;
            if (**ptr == NL)
                (*ptr)++;
            goto col_check;
        }
        if (!calc_only) {
            r = sol_buffer_set_char_at(&mdata->text_grid, CUR_POS, **ptr);
            SOL_INT_CHECK(r, < 0, r);
        }
        (*col)++;
        (*ptr)++;

col_check:
        /* crop lines that don't fit */
        if (*col == mdata->columns) {
            if (!calc_only) {
                if (*row < mdata->rows - 1) {
                    r = sol_buffer_set_char_at
                            (&mdata->text_grid, CUR_EXTRA_COL, NL);
                    SOL_INT_CHECK(r, < 0, r);
                }
            }
            *col = 0;
            (*row)++;
            while (**ptr && **ptr != CR && **ptr != NL) {
                (*ptr)++;
            }
            if (**ptr == CR)
                (*ptr)++;
            if (**ptr == NL)
                (*ptr)++;
            if (ditch_new_lines) /* if ditching NL's, stop at 1st crop */
                break;
        } else if (*ptr == end_ptr && !ditch_new_lines) {
            /* the source ended before the line */
            r = fill_line(mdata, row, col, calc_only);
            SOL_INT_CHECK(r, < 0, r);
        }
    }

    return CUR_POS;
#undef CUR_POS
#undef CUR_EXTRA_COL
}

static int
calculate_n_values(struct selector_data *mdata, size_t *row, size_t *col)
{
    int r;
    char *tmp_ptr = NULL;
    size_t tmp_row = 0, tmp_col = 0, row_span;

    /* Calculate row span of post-value chunk */
    tmp_ptr = mdata->value_tag + sizeof(VALUE_TAG) - 1;
    tmp_row = *row + 1; /* + 1 so we get at least one value line */
    tmp_col = 0;
    r = format_chunk(mdata, &tmp_ptr, mdata->format + strlen(mdata->format),
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
format_do(struct sol_flow_node *node)
{
    struct selector_data *mdata = sol_flow_node_get_private_data(node);
    uint16_t len = sol_ptr_vector_get_len(&mdata->items);
    int r, buf_size = get_buf_size(mdata);
    size_t row = 0, col = 0, n_values = 0, idx = 0,
        format_len = strlen(mdata->format);
    char *ptr = mdata->format, *it_value = NULL;
    bool skip_cursor = false;

    /* Format pre-title/value chunk */
    r = format_chunk(mdata, &ptr,
        mdata->title_tag ? mdata->title_tag : mdata->value_tag,
        &row, &col, DO_FORMAT, KEEP_NL);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    if (r >= buf_size || row >= mdata->rows)
        goto send;

    /* Format title */
    if (mdata->title && mdata->title_tag) {
        char *value = mdata->title;
        r = format_chunk(mdata, &value, mdata->title + strlen(mdata->title),
            &row, &col, DO_FORMAT, DITCH_NL);
        SOL_INT_CHECK_GOTO(r, < 0, err);
        if (r >= buf_size || row >= mdata->rows)
            goto send;

        if (mdata->rows > 1) {
            r = go_to_new_line(mdata, &row, &col);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        } else {
            r = fill_spaces(mdata, &row, &col, 1);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        }

        /* Format post-title, pre-value chunk. If we got only one
         * line, ditch new lines in an attempt to have both title and
         * value in it */
        ptr = mdata->title_tag + strlen(TITLE_TAG);
        r = format_chunk(mdata, &ptr, mdata->value_tag, &row, &col,
            DO_FORMAT, mdata->rows > 1 ? KEEP_NL : DITCH_NL);
        SOL_INT_CHECK_GOTO(r, < 0, err);
        if (r >= buf_size || row >= mdata->rows)
            goto send;
    }

    if (!len) {
        n_values = 0;
    } else if (mdata->rows > 1) {
        r = go_to_new_line(mdata, &row, &col);
        SOL_INT_CHECK_GOTO(r, < 0, err);

        if (!mdata->n_values_done) {
            r = calculate_n_values(mdata, &row, &col);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        }
        n_values = mdata->n_values;
        if (!n_values)
            goto post_value;

        idx = sol_max((int16_t)(mdata->cursor - n_values / 2), (int16_t)0);
        if (idx + n_values > len)
            idx = sol_max((int16_t)(len - n_values), (int16_t)0);
    } else {
        idx = mdata->cursor;
        n_values = 1;
    }

    skip_cursor = (n_values == 1);

    /* Format values, while we can */
    while (n_values && (it_value = sol_ptr_vector_get(&mdata->items, idx))) {
        char *value = NULL;
        size_t curr_row = row;
        bool did_cursor = false, did_sel = false;
        size_t cursor_len =
            mdata->cursor_mark ? strlen(mdata->cursor_mark) : 0,
            sel_len = mdata->sel_mark ? strlen(mdata->sel_mark) : 0,
            padding_spc = 0;

        /* Format selection/cursor markers */
        if (!skip_cursor && idx == mdata->cursor && mdata->cursor_mark) {
            value = mdata->cursor_mark;
            r = format_chunk(mdata, &value, mdata->cursor_mark + cursor_len,
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
                r = fill_spaces(mdata, &row, &col, cursor_len);
                SOL_INT_CHECK_GOTO(r, < 0, err);
                if (row > curr_row)
                    goto next;
            }

            value = mdata->sel_mark;
            r = format_chunk(mdata, &value,
                mdata->sel_mark + sel_len, &row, &col, DO_FORMAT, DITCH_NL);
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
            r = fill_spaces(mdata, &row, &col, padding_spc);
            SOL_INT_CHECK_GOTO(r, < 0, err);
            if (row > curr_row)
                goto next;
        }

        r = format_chunk(mdata, &it_value,
            it_value + strlen(it_value), &row, &col, DO_FORMAT, DITCH_NL);
        SOL_INT_CHECK_GOTO(r, < 0, err);
        if (r >= buf_size || row >= mdata->rows)
            goto send;
        if (row > curr_row)
            goto next;

        r = fill_line(mdata, &row, &col, DO_FORMAT);
        SOL_INT_CHECK_GOTO(r, < 0, err);

next:
        n_values--;
        idx++;
    }

post_value:
    r = go_to_new_line(mdata, &row, &col);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    /* Format post-value chunk */
    ptr = mdata->value_tag + sizeof(VALUE_TAG) - 1;
    r = format_chunk(mdata, &ptr,
        mdata->format + format_len, &row, &col, DO_FORMAT, KEEP_NL);
    SOL_INT_CHECK_GOTO(r, < 0, err);

send:
    /* Don't ever end with NL and guarantee ending NUL byte */
    if (mdata->text_grid.used) {
        char *value = (char *)sol_buffer_at_end(&mdata->text_grid);
        *value = NUL;
        value = (char *)sol_buffer_at_end(&mdata->text_grid) - 1;
        if (*value == NL)
            *value = NUL;
    } else {
        r = sol_buffer_set_char_at(&mdata->text_grid, 0, NUL);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    r = sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_FORM_SELECTOR__OUT__STRING,
        sol_buffer_steal(&mdata->text_grid, NULL));

err:
    /* we got to re-init because of the error cases */
    buffer_re_init(mdata);

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

    return format_do(node);

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

    return format_do(node);
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
        mdata->cursor = sol_min(mdata->cursor + 1, len - 1);

    SOL_DBG("next (len = %d): curr is now %d", len, mdata->cursor);

    return format_do(node);
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
        mdata->cursor = (int16_t)(sol_max(mdata->cursor - 1, 0));

    SOL_DBG("prev (len = %d): curr is now %d", len, mdata->cursor);

    return format_do(node);
}

static int
select_set(struct sol_flow_node *node,
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

    r = format_do(node);
    SOL_INT_CHECK(r, < 0, r);

    if (!sol_ptr_vector_get_len(&mdata->items))
        return 0;

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_FORM_SELECTOR__OUT__SELECTED,
        sol_ptr_vector_get(&mdata->items, mdata->selection));
}

static int
selected_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct selector_data *mdata = data;
    const char *value, *it;
    uint16_t idx;
    int r;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->items, it, idx) {
        if (streq(it, value))
            mdata->selection = idx;
    }

    if (!mdata->enabled)
        return 0;

    return format_do(node);
}

static int
enabled_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct selector_data *mdata = data;
    bool value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->enabled = value;

    return 0;
}

#include "form-gen.c"
