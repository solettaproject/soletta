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

#pragma once

#ifndef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN &_form_common_log_domain
extern struct sol_log_domain _form_common_log_domain;
#endif

#include "sol-flow-internal.h"

#define DITCH_NL (true)
#define KEEP_NL (false)

#define DO_FORMAT (false)
#define CALC_ONLY (true)

static const char CR = '\r', NL = '\n', SPC = ' ', UNDERSCORE = '_',
    NUL = '\0', CURL_BRACKET_OPEN = '{', CURL_BRACKET_CLOSE = '}', COMMA = ',';

static const char TITLE_TAG[] = "{title}", VALUE_TAG[] = "{value}",
    EMPTY_STR[] = "";

static inline size_t
coords_to_pos(size_t n_cols, size_t r, size_t c)
{
    /* account for extra col for (implicit) trailing NLs, thus + r */
    return (r * n_cols) + c + r;
}

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

int format_title(struct sol_buffer *buf, size_t buf_size, size_t n_rows, size_t n_cols, size_t *row_ptr, size_t *col_ptr, const char *format, const char *title, const char *title_tag, const char *value_tag, bool *no_more_space);

int format_chunk(struct sol_buffer *buf, size_t n_rows, size_t n_cols, const char *ptr, const char *end_ptr, size_t *row, size_t *col, bool calc_only, bool ditch_new_lines);

int format_post_value(struct sol_buffer *buf, size_t n_rows, size_t n_cols, size_t *row_ptr, size_t *col_ptr, const char *format, const char *value_tag);

int format_send(struct sol_flow_node *node, struct sol_buffer *buf, uint16_t out_port);

int common_form_init(struct sol_buffer *buf, int32_t in_rows, size_t *out_rows,  int32_t in_cols, size_t *out_cols, const char *in_format, char **out_format, const char *in_title, char **out_title, char **out_title_tag, char **out_value_tag, char **out_text_mem);

int go_to_new_line(struct sol_buffer *buf, size_t n_rows, size_t n_cols, size_t *row, size_t *col);

int fill_spaces(struct sol_buffer *buf, size_t n_cols, size_t *row, size_t *col, size_t length);

int fill_line(struct sol_buffer *buf, size_t n_rows, size_t n_cols, size_t *row, size_t *col, bool calc_only);

