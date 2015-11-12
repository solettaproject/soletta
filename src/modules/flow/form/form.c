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
#include <stdio.h>

#include "sol-buffer.h"
#include "sol-mainloop.h"
#include "sol-util.h"

#include "sol-flow-internal.h"
SOL_LOG_INTERNAL_DECLARE(_converter_log_domain, "flow-converter");

#include "../converter/string-format.h"
//#undef SOL_LOG_DOMAIN

//SOL_LOG_INTERNAL_DECLARE(_converter_log_domain, "flow-converter");

#include "sol-flow/form.h"

#define DITCH_NL (true)
#define KEEP_NL (false)

#define DO_FORMAT (false)
#define CALC_ONLY (true)

static const char CR = '\r', NL = '\n', SPC = ' ', UNDERSCORE = '_',
    NUL = '\0', CURL_BRACKET_OPEN = '{', CURL_BRACKET_CLOSE = '}', COMMA = ',';
static const char TITLE_TAG[] = "{title}", VALUE_TAG[] = "{value}",
    EMPTY_STR[] = "";

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

static inline int
get_buf_size(size_t rows, size_t columns, size_t *out)
{
    size_t n_cols;
    int r;

    /* +1 on cols for '\n' and (final) NUL chars */
    r = sol_util_size_add(columns, 1, &n_cols);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_size_mul(rows, n_cols, out);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static inline int
buffer_re_init(struct sol_buffer *buf,
    char *mem,
    size_t rows,
    size_t columns)
{
    size_t size;

    int r = get_buf_size(rows, columns, &size);

    SOL_INT_CHECK(r, < 0, r);

    /* We choose to do the ending nul byte ourselves, since we're
     * doing fixed capacity, to be able to set the second to last byte
     * on the buffer without reallocation attempts. */
    sol_buffer_init_flags(buf, mem, size,
        SOL_BUFFER_FLAGS_FIXED_CAPACITY | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

    return 0;
}

static int
common_form_init(struct sol_buffer *buf,
    int32_t in_rows,
    size_t *out_rows,
    int32_t in_cols,
    size_t *out_cols,
    const char *in_format,
    char **out_format,
    const char *in_title,
    char **out_title,
    char **out_title_tag,
    char **out_value_tag,
    char **out_text_mem)
{
    int r = 0;
    size_t size;

    SOL_NULL_CHECK(in_format, -EINVAL);

    if (in_rows <= 0) {
        SOL_WRN("Form rows number must be a positive integer, "
            "but %" PRId32 " was given. Fallbacking to minimum value of 1.",
            in_rows);
        *out_rows = 1;
    } else
        *out_rows = in_rows;

    if (in_cols <= 0) {
        SOL_WRN("Boolean columns number must be a positive integer, "
            "but %" PRId32 " was given. Fallbacking to minimum value of 1.",
            in_cols);
        *out_cols = 1;
    } else
        *out_cols = in_cols;

    r = get_buf_size(*out_rows, *out_cols, &size);
    SOL_INT_CHECK(r, < 0, r);

    *out_text_mem = calloc(1, size);
    SOL_NULL_CHECK(*out_text_mem, -ENOMEM);

    *out_format = strdup(in_format);
    if (!*out_format) {
        r = -ENOMEM;
        goto err_format;
    }

    *out_title_tag = strstr(*out_format, TITLE_TAG);
    *out_value_tag = strstr(*out_format, VALUE_TAG);

    if (!*out_value_tag) {
        SOL_WRN("Bad format, no {value} tag: %s. Fallbacking to "
            "pristine one, i. e. '{value}'.", *out_format);
        r = sol_util_replace_str_if_changed(out_format, "{value}");
        SOL_INT_CHECK_GOTO(r, < 0, err_tags);
    }

    if (*out_title_tag > *out_value_tag) {
        SOL_WRN("Bad format, {title} tag placed after {value} tag: %s."
            " Fallbacking to pristine one, i. e. '{value}'.",
            *out_format);
        r = sol_util_replace_str_if_changed(out_format, "{value}");
        SOL_INT_CHECK_GOTO(r, < 0, err_tags);
    }

    if (in_title) {
        *out_title = strdup(in_title);
        if (!*out_title) {
            r = -ENOMEM;
            goto err_title;
        }
    }

    r = buffer_re_init(buf, *out_text_mem, *out_rows, *out_cols);
    if (r < 0) {
        sol_buffer_fini(buf);
        goto err_tags;
    }

    return 0;

err_tags:
err_title:
    free(*out_format);

err_format:
    free(*out_text_mem);

    return r;
}

static inline size_t
coords_to_pos(size_t n_cols, size_t r, size_t c)
{
    /* account for extra col for (implicit) trailing NLs, thus + r */
    return (r * n_cols) + c + r;
}

#define CUR_POS (coords_to_pos(n_cols, *row, *col))
#define CUR_EXTRA_COL (coords_to_pos(n_cols, *row, n_cols - 1) + 1)

static int
fill_spaces(struct sol_buffer *buf,
    size_t n_cols,
    size_t *row,
    size_t *col,
    size_t length)
{
    int r;

    while (*col < n_cols && length) {
        r = sol_buffer_set_char_at(buf, CUR_POS, SPC);
        SOL_INT_CHECK(r, < 0, r);
        (*col)++;
        length--;
    }
    /* If we did not reach the row's end, we're done, otherwise we
     * must break the line */
    if (!length)
        return CUR_POS;
    r = sol_buffer_set_char_at(buf, CUR_EXTRA_COL, NL);
    SOL_INT_CHECK(r, < 0, r);
    *col = 0;
    (*row)++;

    return CUR_POS;
}

static int
fill_line(struct sol_buffer *buf,
    size_t n_rows,
    size_t n_cols,
    size_t *row,
    size_t *col,
    bool calc_only)
{
    int r;

    while (*col < n_cols) {
        if (!calc_only) {
            r = sol_buffer_set_char_at(buf, CUR_POS, SPC);
            SOL_INT_CHECK(r, < 0, r);
        }
        (*col)++;
    }
    if (*row < n_rows - 1) {
        if (!calc_only) {
            r = sol_buffer_set_char_at
                    (buf, CUR_EXTRA_COL, NL);
            SOL_INT_CHECK(r, < 0, r);
        }
    }
    (*row)++;
    *col = 0;

    return CUR_POS;
}

static int
go_to_new_line(struct sol_buffer *buf,
    size_t n_rows,
    size_t n_cols,
    size_t *row,
    size_t *col)
{
    int r = 0;

    if (*col > 0)
        r = fill_line(buf, n_rows, n_cols, row, col, DO_FORMAT);

    return r;
}

static int
format_chunk(struct sol_buffer *buf,
    size_t n_rows,
    size_t n_cols,
    const char *ptr,
    const char *end_ptr,
    size_t *row,
    size_t *col,
    bool calc_only,
    bool ditch_new_lines)
{
    size_t sz = buf->capacity;
    int r;

    while (ptr < end_ptr && CUR_POS < sz && *row < n_rows) {
        /* translate middle-of-line \n's to spaces till the end +
         * \n */
        if (*ptr == CR || *ptr == NL) {
            if (ditch_new_lines) {
                if (!calc_only) {
                    r = sol_buffer_set_char_at(buf, CUR_POS, SPC);
                    SOL_INT_CHECK(r, < 0, r);
                }
                (*col)++;
            } else {
                r = fill_line(buf, n_rows, n_cols, row, col, calc_only);
                SOL_INT_CHECK(r, < 0, r);
            }
            if (*ptr == CR)
                ptr++;
            if (*ptr == NL)
                ptr++;
            goto col_check;
        }
        if (!calc_only) {
            r = sol_buffer_set_char_at(buf, CUR_POS, *ptr);
            SOL_INT_CHECK(r, < 0, r);
        }
        (*col)++;
        ptr++;

col_check:
        /* crop lines that don't fit */
        if (*col == n_cols) {
            if (!calc_only) {
                if (*row < n_rows - 1) {
                    r = sol_buffer_set_char_at(buf, CUR_EXTRA_COL, NL);
                    SOL_INT_CHECK(r, < 0, r);
                }
            }
            *col = 0;
            (*row)++;
            while (*ptr && *ptr != CR && *ptr != NL) {
                ptr++;
            }
            if (*ptr == CR)
                ptr++;
            if (*ptr == NL)
                ptr++;
            if (ditch_new_lines) /* if ditching NL's, stop at 1st crop */
                break;
        } else if (ptr == end_ptr && !ditch_new_lines) {
            /* the source ended before the line */
            r = fill_line(buf, n_rows, n_cols, row, col, calc_only);
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

static int
format_title(struct sol_buffer *buf,
    size_t buf_size,
    size_t n_rows,
    size_t n_cols,
    size_t *row_ptr,
    size_t *col_ptr,
    const char *format,
    const char *title,
    const char *title_tag,
    const char *value_tag,
    bool *no_more_space)
{
    int r;
    const char *ptr = format;

    *no_more_space = false;

    /* Format pre-title/value chunk */
    r = format_chunk(buf, n_rows, n_cols, ptr,
        title_tag ? title_tag : value_tag,
        row_ptr, col_ptr, DO_FORMAT, KEEP_NL);
    SOL_INT_CHECK(r, < 0, r);
    if (r >= (int)buf_size || *row_ptr >= n_rows) {
        *no_more_space = true;
        return 0;
    }

    if (!title || !title_tag)
        return 0;

    /* Format title */
    r = format_chunk(buf, n_rows, n_cols, title, title + strlen(title),
        row_ptr, col_ptr, DO_FORMAT, DITCH_NL);
    SOL_INT_CHECK(r, < 0, r);
    if (r >= (int)buf_size || *row_ptr >= n_rows) {
        *no_more_space = true;
        return 0;
    }

    if (n_rows > 1) {
        r = go_to_new_line(buf, n_rows, n_cols, row_ptr, col_ptr);
        SOL_INT_CHECK(r, < 0, r);
    } else {
        r = fill_spaces(buf, n_cols, row_ptr, col_ptr, 1);
        SOL_INT_CHECK(r, < 0, r);
    }

    /* Format post-title, pre-value chunk. If we got only one
     * line, ditch new lines in an attempt to have both title and
     * value in it */
    ptr = title_tag + strlen(TITLE_TAG);
    r = format_chunk(buf, n_rows, n_cols, ptr, value_tag, row_ptr,
        col_ptr, DO_FORMAT, n_rows > 1 ? KEEP_NL : DITCH_NL);
    SOL_INT_CHECK(r, < 0, r);
    if (r >= (int)buf_size || *row_ptr >= n_rows)
        *no_more_space = true;

    return 0;
}

static int
format_post_value(struct sol_buffer *buf,
    size_t n_rows,
    size_t n_cols,
    size_t *row_ptr,
    size_t *col_ptr,
    const char *format,
    const char *value_tag)
{
    const char *ptr = value_tag + sizeof(VALUE_TAG) - 1;
    int r;

    r = go_to_new_line(buf, n_rows, n_cols, row_ptr, col_ptr);
    SOL_INT_CHECK(r, < 0, r);

    /* Format post-value chunk */
    return format_chunk(buf, n_rows, n_cols, ptr,
        format + strlen(format), row_ptr, col_ptr, DO_FORMAT, KEEP_NL);
}

static int
format_send(struct sol_flow_node *node,
    struct sol_buffer *buf,
    uint16_t out_port)
{
    int r;

    /* Don't ever end with NL and guarantee ending NUL byte */
    if (buf->used) {
        char *value = (char *)sol_buffer_at_end(buf);
        *value = NUL;
        value = (char *)sol_buffer_at_end(buf) - 1;
        if (*value == NL)
            *value = NUL;
    } else {
        r = sol_buffer_set_char_at(buf, 0, NUL);
        SOL_INT_CHECK(r, < 0, r);
    }

    return sol_flow_send_string_slice_packet(node,
        out_port, sol_buffer_get_slice(buf));
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
        mdata->cursor = sol_min(mdata->cursor + 1, len - 1);

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
        mdata->cursor = (int16_t)(sol_max(mdata->cursor - 1, 0));

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

    r = sol_flow_packet_get_boolean(packet, &value);
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

    r = sol_flow_packet_get_boolean(packet, &value);
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

    return sol_flow_send_boolean_packet(node,
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

    r = sol_flow_packet_get_boolean(packet, &value);
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

    r = sol_flow_packet_get_boolean(packet, &value);
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

    n_max = snprintf(sbuf, 1, "%+" PRId32 "", mdata->base.state.max);
    SOL_INT_CHECK_GOTO(n_max, < 0, err);

    n_min = snprintf(sbuf, 1, "%+" PRId32 "", mdata->base.state.min);
    SOL_INT_CHECK_GOTO(n_min, < 0, err);

    /* -1 to take away sign */
    mdata->n_digits = sol_max(n_min, n_max) - 1;

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

    r = sol_flow_packet_get_boolean(packet, &value);
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
            /* a character not in the charset ocurred, arbitrate
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

    r = sol_flow_packet_get_boolean(packet, &value);
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

struct string_formatted_data {
    size_t columns, rows, value_prefix_len, cursor;
    char *title, *text_mem, *format, *value, *title_tag, *value_tag;
    struct sol_buffer text_grid, formatted_value;
    struct sol_vector chunks;
    struct sol_timeout *timer;
    int blink_time;
    bool circular : 1;
    bool enabled : 1;
    bool blink_on : 1;
    bool state_changed : 1;
    bool cursor_initialized : 1;
};

enum string_formatted_chunk_type {
    STR_FORMAT_INT,
    STR_FORMAT_FLOAT,
    STR_FORMAT_LITERAL
};

struct string_formatted_chunk {
    /* Slice from mdata->formatted_value.data, on INT/FLOAT types, or
     * from mdata->value, for LITERALs. We have to keep this to have
     * the information again on the blinking process (for INT/FLOAT)
     * or to easily resolve the LITERAL when formatting. */
    struct sol_str_slice rendered;

    /* Re-built from mdata->value, on INT/FLOAT types, to match
     * do_{integer,float}_markup()'s syntax */
    char *format;

    /* Starting position at mdata->text_grid.data, on INT/FLOAT types.
     * If the field does not fit at all, it will contain NULL */
    const char *pos_in_text_grid;

    enum string_formatted_chunk_type type;

    /* Numerical state, for INT/FLOAT types */
    union {
        struct sol_irange i;
        struct sol_drange d;
    } state;
};

static int
string_formatted_format_do(struct sol_flow_node *node)
{
    struct string_formatted_data *mdata = sol_flow_node_get_private_data(node);
    int r, buf_size = mdata->text_grid.capacity;
    size_t row = 0, col = 0, tmp_col;
    struct sol_vector indexes;
    bool no_more_space = false;
    char *tmp = NULL;

    if (!mdata->state_changed) {
        char *value;
        size_t start, len;
        struct string_formatted_chunk *chunk =
            sol_vector_get(&mdata->chunks, mdata->cursor);

        if (!chunk->pos_in_text_grid)
            goto send;

        start = chunk->pos_in_text_grid - (char *)mdata->text_grid.data;

        /* abs. pos modulo number of cols gives us the actual col */
        len = mdata->columns - (start % mdata->columns) + 1;

        value = (char *)sol_buffer_at(&mdata->text_grid, start);

        if (mdata->blink_on) {
            mdata->blink_on = false;
            for (size_t l = 0; l < chunk->rendered.len && len; l++, len--)
                value[l] = SPC;
        } else {
            mdata->blink_on = true;
            for (size_t l = 0; l < chunk->rendered.len && len; l++, len--)
                value[l] = chunk->rendered.data[l];
        }

        goto send;
    }

    r = format_title(&mdata->text_grid, buf_size, mdata->rows,
        mdata->columns, &row, &col, mdata->format, mdata->title,
        mdata->title_tag, mdata->value_tag, &no_more_space);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    SOL_EXP_CHECK_GOTO(no_more_space, send);

    mdata->value_prefix_len = tmp_col = col;

    sol_buffer_fini(&mdata->formatted_value);
    sol_buffer_init(&mdata->formatted_value);

    sol_vector_init(&indexes, sizeof(size_t));

    for (size_t l = 0; l < mdata->chunks.len; l++) {
        struct string_formatted_chunk *chunk =
            sol_vector_get(&mdata->chunks, l);
        size_t start, sz, *start_ptr;

        start = mdata->formatted_value.used;

        switch (chunk->type) {
        case STR_FORMAT_INT:
            r = do_integer_markup(node, chunk->format, chunk->state.i,
                &mdata->formatted_value);
            break;
        case STR_FORMAT_FLOAT:
            r = do_float_markup(node, chunk->format, chunk->state.d,
                &mdata->formatted_value);
            break;
        case STR_FORMAT_LITERAL:
            r = sol_buffer_append_slice
                    (&mdata->formatted_value, chunk->rendered);
            break;
        }
        SOL_INT_CHECK_GOTO(r, < 0, err);
        sz = mdata->formatted_value.used - start;

        if (chunk->type == STR_FORMAT_LITERAL)
            continue;

        /* We can't assume mdata->formatted_value.data + start will
         * stay as is, because the buffer may grow on these iterations
         * and be realloc()ed! We save the value to compute the ptr
         * offsets later */
        start_ptr = sol_vector_append(&indexes);
        *start_ptr = start;
        chunk->rendered.len = sz;

        if (tmp_col /* + sz */ > mdata->columns - 1)
            chunk->pos_in_text_grid = NULL;
        else {
            chunk->pos_in_text_grid = (char *)mdata->text_grid.data +
                coords_to_pos(mdata->columns, row, mdata->value_prefix_len)
                + start;
            tmp_col += sz;
        }

        if (!mdata->cursor_initialized) {
            mdata->cursor = l;
            mdata->cursor_initialized = true;
        }
    }

    for (size_t l = 0, s = 0; l < mdata->chunks.len; l++) {
        size_t *start_ptr;
        struct string_formatted_chunk *chunk =
            sol_vector_get(&mdata->chunks, l);

        if (chunk->type == STR_FORMAT_LITERAL)
            continue;

        start_ptr = sol_vector_get(&indexes, s++);

        chunk->rendered.data =
            (char *)mdata->formatted_value.data + *start_ptr;
    }

    sol_vector_clear(&indexes);

    tmp = mdata->formatted_value.data;
    r = format_chunk(&mdata->text_grid, mdata->rows,
        mdata->columns, tmp, tmp + mdata->formatted_value.used, &row, &col,
        DO_FORMAT, DITCH_NL);
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
        SOL_FLOW_NODE_TYPE_FORM_STRING_FORMATTED__OUT__STRING);

    return r;

err:
    /* we got to re-init because of the error cases. if this function
     * fails we're no better, so don't check. */
    buffer_re_init(&mdata->text_grid, mdata->text_mem,
        mdata->rows, mdata->columns);

    return r;
}

static bool
string_formatted_timeout(void *data)
{
    return string_formatted_format_do(data) == 0;
}

static void
string_formatted_force_imediate_format(struct string_formatted_data *mdata,
    bool re_init)
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
string_formatted_format(struct sol_flow_node *node)
{
    struct string_formatted_data *mdata = sol_flow_node_get_private_data(node);

    if (!mdata->timer) {
        mdata->timer = sol_timeout_add
                (mdata->blink_time, string_formatted_timeout, node);
        SOL_NULL_CHECK(mdata->timer, -ENOMEM);
        return string_formatted_format_do(node);
    }

    return 0;
}

static void
string_formatted_close(struct sol_flow_node *node, void *data)
{
    struct string_formatted_data *mdata = data;
    struct string_formatted_chunk *chunk;
    uint16_t i;

    sol_buffer_fini(&mdata->text_grid);
    sol_buffer_fini(&mdata->formatted_value);

    SOL_VECTOR_FOREACH_IDX (&mdata->chunks, chunk, i)
        free(chunk->format);

    sol_vector_clear(&mdata->chunks);

    if (mdata->timer)
        sol_timeout_del(mdata->timer);

    free(mdata->title);
    free(mdata->format);
    free(mdata->value);
}

static inline double
strtod_no_locale(const char *nptr, char **endptr)
{
    return sol_util_strtodn(nptr, endptr, -1, false);
}

static double
midpoint(double min, double max)
{
    if (min < 0 && max > 0)
        return (max + min) / 2.0;

    return ((max - min) / 2.0) + min;
}

static int
string_formatted_selected_set_do(struct sol_flow_node *node,
    void *data,
    const char *value)
{
    struct string_formatted_data *mdata = data;
    const char *ptr, *end;
    size_t len;
    int r;

    len = strlen(value);
    ptr = value;
    end = value + len;
    for (size_t l = 0; l < mdata->chunks.len && *ptr; l++) {
        struct string_formatted_chunk *chunk
            = sol_vector_get(&mdata->chunks, l), *next_chunk;

        switch (chunk->type) {
        case STR_FORMAT_INT:
        {
            char *endptr;
            int32_t *i_ptr;

            if (l + 1 < mdata->chunks.len) {
                next_chunk = sol_vector_get(&mdata->chunks, l + 1);

                if (next_chunk->type != STR_FORMAT_LITERAL) {
                    r = ENOTSUP;
                    goto error;
                }
                endptr = memmem(ptr, end - ptr,
                    next_chunk->rendered.data, next_chunk->rendered.len);
                if (!endptr) {
                    r = -EINVAL;
                    goto error;
                }
            } else
                endptr = (char *)end;

            i_ptr = &chunk->state.i.val;
            *i_ptr = strtoll(ptr, &endptr, 0);

            if (ptr == endptr || errno != 0) {
                r = -errno;
                goto error;
            }
            ptr = endptr;
            break;
        }
        case STR_FORMAT_FLOAT:
        {
            char *endptr;
            double *d_ptr;

            if (l + 1 < mdata->chunks.len) {
                next_chunk = sol_vector_get(&mdata->chunks, l + 1);

                if (next_chunk->type != STR_FORMAT_LITERAL) {
                    r = ENOTSUP;
                    goto error;
                }
                endptr = memmem(ptr, end - ptr,
                    next_chunk->rendered.data, next_chunk->rendered.len);
                if (!endptr) {
                    r = -EINVAL;
                    goto error;
                }
            } else
                endptr = (char *)end;

            d_ptr = &chunk->state.d.val;
            *d_ptr = strtod_no_locale(ptr, &endptr);

            if (ptr == endptr || errno != 0) {
                r = -errno;
                goto error;
            }
            ptr = endptr;
            break;
        }
        case STR_FORMAT_LITERAL:
            if (chunk->rendered.len > (size_t)(end - ptr) ||
                memcmp(chunk->rendered.data, ptr, chunk->rendered.len) != 0) {
                r = -EINVAL;
                goto error;
            }
            ptr += chunk->rendered.len;
            break;
        }
    }

    return 0;

error:
    sol_flow_send_error_packet(node, ENOTSUP, "The node's value formatting "
        "string (%s) is so that this entry (%s) is not parseable: %s",
        mdata->value, value, sol_util_strerrora(-r));
    return r;
}

static int
string_formatted_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    const char *ptr;
    bool numeric_field_present = false;
    struct string_formatted_data *mdata = data;
    static const char syntax_msg[] =
        "Please use the {<type>:<min>,<max>,<step>} syntax.";
    struct string_formatted_chunk *chunk = NULL;
    const struct sol_flow_node_type_form_string_formatted_options *opts =
        (const struct sol_flow_node_type_form_string_formatted_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_FORM_INT_OPTIONS_API_VERSION, -EINVAL);

    if (!opts->value) {
        SOL_WRN("A value format must be passed.");
        return -EINVAL;
    } else {
        mdata->value = strdup(opts->value);
        SOL_NULL_CHECK(mdata->value, -ENOMEM);
    }
    ptr = mdata->value;

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

    mdata->circular = opts->circular;

    mdata->enabled = true;

    mdata->blink_time = opts->blink_time;
    if (opts->blink_time < 0) {
        SOL_WRN("Invalid blink_time (%" PRId32 "), that must be positive. "
            "Setting to 1ms.", opts->blink_time);
        mdata->blink_time = 1;
    }

    mdata->blink_on = true;
    mdata->state_changed = true;

    sol_vector_init(&mdata->chunks, sizeof(struct string_formatted_chunk));

    /* For instance, a format could be something like
     * "LITERAL{3d:0,255,1}LITERAL{3d:0,255,1}LITERAL" */

    if (*ptr == CURL_BRACKET_OPEN)
        goto numeric_loop;

    /* literals state loop */
literal_loop:
    chunk = sol_vector_append(&mdata->chunks);
    if (!chunk) {
        r = -errno;
        goto value_err;
    }

    chunk->type = STR_FORMAT_LITERAL;
    chunk->rendered.data = ptr;
    chunk->pos_in_text_grid = NULL;

    while (*ptr) {
        if (*ptr != CURL_BRACKET_OPEN) {
            chunk->rendered.len++;
            ptr++;
        } else
            break;
    }

    /* numeric field state loop */
numeric_loop:
    while (*ptr) {
        const char *field_start = ptr, *field_end, *field_format_end;
        char *tmp;

        if (*ptr != CURL_BRACKET_OPEN)
            goto literal_loop;

        tmp = strchr(ptr, ':');

        if (!tmp) {
            r = -ENOMEM;
            goto value_err;
        }

        /* at least one char between '{' and ':' */
        if (tmp - ptr < 2) {
            SOL_WRN("No numeric field type specification passed (%.*s). %s.",
                (int)(tmp - ptr), ptr, syntax_msg);
            r = -EINVAL;
            goto value_err;
        }

        field_format_end = tmp - 1;

        chunk = sol_vector_append(&mdata->chunks);
        if (!chunk) {
            r = -errno;
            goto value_err;
        }

        switch (*(tmp - 1)) {
        case 'b':
        case 'c':
        case 'd':
        case 'o':
        case 'x':
        case 'X':
        case 'n':
            chunk->type = STR_FORMAT_INT;
            break;
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case '%':
            chunk->type = STR_FORMAT_FLOAT;
            break;
        default:
            SOL_WRN("Bad numeric type (%c) given on format string. It must be "
                "one of b/c/d/o/x/X/n, for integers, or e/E/f/F/g/G/%%, for"
                " floating point numbers.", *(tmp - 1));
            r = -EINVAL;
            goto value_err;
        }

        sol_buffer_init(&mdata->formatted_value);

#define NUM_SPEC_FIELDS (3)
        /* check for min, max, step */
        for (size_t i = 0; i < NUM_SPEC_FIELDS; i++) {
            char *endptr, backup;

            ptr = tmp + 1;
            if (!ptr) {
                r = -EINVAL;
                SOL_WRN("Numeric field format ended with missing min, max, "
                    "step triple (%.*s). %s.", (int)(ptr - field_start),
                    field_start, syntax_msg);
                goto value_err;
            }

            tmp = strchr(ptr,
                i < NUM_SPEC_FIELDS - 1 ? COMMA : CURL_BRACKET_CLOSE);
            if (!tmp) {
                SOL_WRN("Numeric field format ended with missing min, max, "
                    "step triple (%.s). %s.", field_start, syntax_msg);
                r = -EINVAL;
                goto value_err;
            }
            /* we edit the buffer to ease strtoll()'s job, but we can
             * still free it */
            backup = *tmp;
            *tmp = NUL;
#undef NUM_SPEC_FIELDS

            errno = 0;
            if (chunk->type == STR_FORMAT_INT) {
                int32_t *i_ptr = &chunk->state.i.val + i + 1;
                *i_ptr = strtoll(ptr, &endptr, 0);

                if (ptr == endptr || errno != 0) {
                    SOL_WRN("Failed to parse integer number %s: %s. %s.",
                        ptr, errno ? sol_util_strerrora(errno) : "bad format",
                        syntax_msg);
                    r = -errno;
                    goto value_err;
                }

                /* val = min, to start with */
                if (!i) {
                    i_ptr--;
                    *i_ptr = i_ptr[1];
                }
            } else {
                double *d_ptr = &chunk->state.d.val + i + 1;
                *d_ptr = strtod_no_locale(ptr, &endptr);

                if (ptr == endptr || errno != 0) {
                    SOL_WRN("Failed to parse floating point number"
                        " %s: %s. %s.", ptr,
                        errno ? sol_util_strerrora(errno) : "bad format",
                        syntax_msg);
                    r = -errno;
                    goto value_err;
                }

                /* val = min, to start with */
                if (!i) {
                    d_ptr--;
                    *d_ptr = d_ptr[1];
                }
            }
            *tmp = backup;
        }
        field_end = tmp;

        if (chunk->type == STR_FORMAT_INT) {
            int64_t range;

            if (chunk->state.i.min > chunk->state.i.max) {
                int32_t v;
                SOL_WRN("Max value should be greater than "
                    "min on %.*s. Swapping both values.",
                    (int)(field_end - field_start + 1), field_start);
                v = chunk->state.i.max;
                chunk->state.i.max = chunk->state.i.min;
                chunk->state.i.min = v;
            }
            range = (int64_t)chunk->state.i.max
                - (int64_t)chunk->state.i.min;
            if ((chunk->state.i.step > 0
                && chunk->state.i.step > range)
                || (chunk->state.i.step < 0
                && chunk->state.i.step < -range)) {
                SOL_WRN("Step value must fit the given range for %.*s."
                    " Assuming 1 for it.",
                    (int)(field_end - field_start + 1), field_start);
                chunk->state.i.step = 1;
            }
        } else {
            double mid_point, mid_step, mid_range;

            if (chunk->state.d.min > chunk->state.d.max) {
                double v;
                SOL_WRN("Max value should be greater than "
                    "min on %.*s. Swapping both values.",
                    (int)(field_end - field_start + 1), field_start);
                v = chunk->state.d.max;
                chunk->state.d.max = chunk->state.d.min;
                chunk->state.d.min = v;
            }
            mid_point = midpoint(chunk->state.d.min,
                chunk->state.d.max);
            mid_range = chunk->state.d.max - mid_point;
            mid_step = chunk->state.d.step / 2.0;
            if ((mid_step > 0 && mid_step > mid_range)
                || (mid_step < 0 && mid_step < -mid_range)) {
                SOL_WRN("Step value must fit the given range for %.*s."
                    " Setting it to that exact range.",
                    (int)(field_end - field_start + 1), field_start);
                chunk->state.d.step = 2.0 * mid_step;
            }
        }

        r = asprintf(&chunk->format, "{:%.*s}",
            (int)(field_format_end - field_start), field_start + 1);
        SOL_INT_CHECK_GOTO(r, < 0, value_err);

        /* here we just validate de format string with initial 0
         * state's value */
        if (chunk->type == STR_FORMAT_INT)
            r = do_integer_markup(NULL, chunk->format, chunk->state.i,
                &mdata->formatted_value);
        else
            r = do_float_markup(NULL, chunk->format, chunk->state.d,
                &mdata->formatted_value);

        /* warning already issued, on errors */
        SOL_INT_CHECK_GOTO(r, < 0, value_err);

        numeric_field_present = true;

        ptr = tmp + 1;
    }

    if (!numeric_field_present) {
        SOL_WRN("At least one numeric field must occur in the value format"
            " string (%s), but none was detected. %s at least once in that"
            " format string.", mdata->value, syntax_msg);
        r = -EINVAL;
        goto value_err;
    }

    if (opts->value_default)
        string_formatted_selected_set_do(node, data, opts->value_default);

    return string_formatted_format(node);

value_err:
    string_formatted_close(node, mdata);
    return r;
}

static int
string_formatted_up_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_formatted_data *mdata = data;
    struct string_formatted_chunk *chunk;

    if (!mdata->enabled)
        return 0;

    chunk = sol_vector_get(&mdata->chunks, mdata->cursor);

    if (chunk->type == STR_FORMAT_INT) {
        if (chunk->state.i.step > 0) {
            /* step > 0 && max - step > min, so no overflow */
            if (chunk->state.i.val <= chunk->state.i.max
                - chunk->state.i.step) {
                chunk->state.i.val += chunk->state.i.step;
            } else {
                if (mdata->circular)
                    chunk->state.i.val = chunk->state.i.min;
            }
        } else {
            /* step < 0 && min - step > max, so no overflow */
            if (chunk->state.i.val >= chunk->state.i.min
                - chunk->state.i.step) {
                chunk->state.i.val += chunk->state.i.step;
            } else {
                if (mdata->circular)
                    chunk->state.i.val = chunk->state.i.max;
            }
        }
    } else {
        if (chunk->state.d.step > 0) {
            /* step > 0 && max - step > min, so no overflow */
            if (chunk->state.d.val <= chunk->state.d.max
                - chunk->state.d.step) {
                chunk->state.d.val += chunk->state.d.step;
            } else {
                if (mdata->circular)
                    chunk->state.d.val = chunk->state.d.min;
            }
        } else {
            /* step < 0 && min - step > max, so no overflow */
            if (chunk->state.d.val >= chunk->state.d.min
                - chunk->state.d.step) {
                chunk->state.d.val += chunk->state.d.step;
            } else {
                if (mdata->circular)
                    chunk->state.d.val = chunk->state.d.max;
            }
        }
    }

    mdata->state_changed = true;
    mdata->blink_on = true;

    string_formatted_force_imediate_format(mdata, true);
    return string_formatted_format(node);
}

static int
string_formatted_down_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_formatted_data *mdata = data;
    struct string_formatted_chunk *chunk;

    if (!mdata->enabled)
        return 0;

    chunk = sol_vector_get(&mdata->chunks, mdata->cursor);

    if (chunk->type == STR_FORMAT_INT) {
        if (chunk->state.i.step > 0) {
            /* step > 0 && min + step < max, so no overflow */
            if (chunk->state.i.val >= chunk->state.i.min
                + chunk->state.i.step) {
                chunk->state.i.val -= chunk->state.i.step;
            } else {
                if (mdata->circular)
                    chunk->state.i.val = chunk->state.i.max;
            }
        } else {
            /* step < 0 && max + step < min, so no overflow */
            if (chunk->state.i.val <= chunk->state.i.max
                + chunk->state.i.step) {
                chunk->state.i.val -= chunk->state.i.step;
            } else {
                if (mdata->circular)
                    chunk->state.i.val = chunk->state.i.min;
            }
        }
    } else {
        if (chunk->state.d.step > 0) {
            /* step > 0 && min + step < max, so no overflow */
            if (chunk->state.d.val >= chunk->state.d.min
                + chunk->state.d.step) {
                chunk->state.d.val -= chunk->state.d.step;
            } else {
                if (mdata->circular)
                    chunk->state.d.val = chunk->state.d.max;
            }
        } else {
            /* step < 0 && max + step < min, so no overflow */
            if (chunk->state.d.val <= chunk->state.d.max
                + chunk->state.d.step) {
                chunk->state.d.val -= chunk->state.d.step;
            } else {
                if (mdata->circular)
                    chunk->state.d.val = chunk->state.d.min;
            }
        }
    }

    mdata->state_changed = true;
    mdata->blink_on = true;

    string_formatted_force_imediate_format(mdata, true);
    return string_formatted_format(node);
}

static int
string_formatted_next_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_formatted_data *mdata = data;
    size_t cursor_pos = mdata->cursor, l = mdata->chunks.len;
    bool found = false;

    if (!mdata->enabled)
        return 0;

    for (cursor_pos = mdata->cursor + 1; cursor_pos < l; cursor_pos++) {
        struct string_formatted_chunk *chunk;

        chunk = sol_vector_get(&mdata->chunks, cursor_pos);
        if (chunk->type == STR_FORMAT_LITERAL)
            continue;
        else {
            found = true;
            break;
        }
    }

    if (!found)
        return 0;

    mdata->cursor = cursor_pos;

    mdata->state_changed = true;
    mdata->blink_on = true;

    string_formatted_force_imediate_format(mdata, true);
    return string_formatted_format(node);
}

static int
string_formatted_previous_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_formatted_data *mdata = data;
    size_t cursor_pos = mdata->cursor;
    bool found = false;

    if (!mdata->enabled)
        return 0;

    while (cursor_pos) {
        struct string_formatted_chunk *chunk;

        cursor_pos--;
        chunk = sol_vector_get(&mdata->chunks, cursor_pos);
        if (chunk->type == STR_FORMAT_LITERAL)
            continue;
        else {
            found = true;
            break;
        }
    }

    if (!found)
        return 0;

    mdata->cursor = cursor_pos;

    mdata->state_changed = true;
    mdata->blink_on = true;

    string_formatted_force_imediate_format(mdata, true);
    return string_formatted_format(node);
}

static int
string_formatted_select_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_formatted_data *mdata = data;
    int r;

    if (!mdata->enabled)
        return 0;

    /* force new format with state changed and blink state on, so we
     * always get the full output here */
    string_formatted_force_imediate_format(mdata, false);
    mdata->state_changed = true;
    mdata->blink_on = true;

    r = string_formatted_format(node);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_string_slice_packet(node,
        SOL_FLOW_NODE_TYPE_FORM_INT__OUT__SELECTED,
        sol_buffer_get_slice(&mdata->formatted_value));
}

static int
string_formatted_selected_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_formatted_data *mdata = data;
    const char *value;
    int r;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = string_formatted_selected_set_do(node, data, value);
    SOL_INT_CHECK(r, < 0, r);

    if (!mdata->enabled)
        return 0;

    string_formatted_force_imediate_format(mdata, true);
    mdata->state_changed = true;
    mdata->blink_on = true;

    return string_formatted_format(node);
}

static int
string_formatted_enabled_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_formatted_data *mdata = data;
    bool value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->enabled = value;

    return 0;
}

#include "form-gen.c"
