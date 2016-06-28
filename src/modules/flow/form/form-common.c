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

#include "form-common.h"
SOL_LOG_INTERNAL_DECLARE(_form_common_log_domain, "form-common");

#define CUR_POS (coords_to_pos(n_cols, *row, *col))
#define CUR_EXTRA_COL (coords_to_pos(n_cols, *row, n_cols - 1) + 1)

int
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

int
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

int
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

int
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

int
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

int
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

int
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

int
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
        r = sol_util_replace_str_if_changed(out_format, VALUE_TAG);
        *out_value_tag = *out_format;
        *out_title_tag = NULL;
        SOL_INT_CHECK_GOTO(r, < 0, err_tags);
    }

    if (*out_title_tag > *out_value_tag) {
        SOL_WRN("Bad format, {title} tag placed after {value} tag: %s."
            " Fallbacking to pristine one, i. e. '{value}'.",
            *out_format);
        r = sol_util_replace_str_if_changed(out_format, VALUE_TAG);
        *out_value_tag = *out_format;
        *out_title_tag = NULL;
        SOL_INT_CHECK_GOTO(r, < 0, err_tags);
    }

    if (in_title && *out_title_tag) {
        *out_title = strdup(in_title);
        if (!*out_title) {
            r = -ENOMEM;
            goto err_title;
        }
    } else {
        *out_title = NULL;
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
