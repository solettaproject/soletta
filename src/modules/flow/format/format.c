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

#include "sol-flow/format.h"
#include "string-format.h"
#include "../form/form-common.h"
#include "sol-mainloop.h"

struct string_converter {
    struct sol_flow_node *node;
    char *format;
};

static int
drange_to_string_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_converter *mdata = data;
    const struct sol_flow_node_type_format_float_to_string_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_FORMAT_FLOAT_TO_STRING_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_format_float_to_string_options *)options;

    mdata->format = strdup(opts->format_spec);
    SOL_NULL_CHECK(mdata->format, -ENOMEM);

    return 0;
}

static void
drange_to_string_close(struct sol_flow_node *node, void *data)
{
    struct string_converter *mdata = data;

    free(mdata->format);
}

static int
irange_to_string_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_converter *mdata = data;
    const struct sol_flow_node_type_format_int_to_string_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_FORMAT_INT_TO_STRING_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_format_int_to_string_options *)options;

    mdata->format = strdup(opts->format_spec);
    SOL_NULL_CHECK(mdata->format, -ENOMEM);

    return 0;
}

static void
irange_to_string_close(struct sol_flow_node *node, void *data)
{
    struct string_converter *mdata = data;

    free(mdata->format);
}

static int
drange_to_string_convert(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_drange in_value;
    struct string_converter *mdata = data;
    struct sol_buffer out = SOL_BUFFER_INIT_EMPTY;

    mdata->node = node;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = do_float_markup(node, mdata->format, in_value, &out);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    r = sol_flow_send_string_slice_packet(node,
        SOL_FLOW_NODE_TYPE_FORMAT_FLOAT_TO_STRING__OUT__OUT,
        sol_buffer_get_slice(&out));

end:
    sol_buffer_fini(&out);
    return r;
}

static int
irange_to_string_convert(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange in_value;
    struct string_converter *mdata = data;
    struct sol_buffer out = SOL_BUFFER_INIT_EMPTY;

    mdata->node = node;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = do_integer_markup(node, mdata->format, in_value, &out);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    r = sol_flow_send_string_slice_packet(node,
        SOL_FLOW_NODE_TYPE_FORMAT_INT_TO_STRING__OUT__OUT,
        sol_buffer_get_slice(&out));

end:
    sol_buffer_fini(&out);
    return r;
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

    sol_vector_init(&indexes, sizeof(size_t));

    r = format_title(&mdata->text_grid, buf_size, mdata->rows,
        mdata->columns, &row, &col, mdata->format, mdata->title,
        mdata->title_tag, mdata->value_tag, &no_more_space);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    SOL_EXP_CHECK_GOTO(no_more_space, send);

    mdata->value_prefix_len = tmp_col = col;

    sol_buffer_fini(&mdata->formatted_value);
    sol_buffer_init(&mdata->formatted_value);

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
        if (!start_ptr) {
            r = -ENOMEM;
            goto err;
        }

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
        if (!start_ptr) {
            r = -ERANGE;
            goto err;
        }

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
        SOL_FLOW_NODE_TYPE_FORMAT_STRING_FORMATTED_FORM__OUT__STRING);

    return r;

err:
    /* we got to re-init because of the error cases. if this function
     * fails we're no better, so don't check. */
    buffer_re_init(&mdata->text_grid, mdata->text_mem,
        mdata->rows, mdata->columns);

    sol_buffer_fini(&mdata->formatted_value);
    sol_vector_clear(&indexes);

    return r;
}

static bool
string_formatted_timeout(void *data)
{
    struct string_formatted_data *mdata = sol_flow_node_get_private_data(data);

    if (!string_formatted_format_do(data)) {
        mdata->timer = NULL;
        return true;
    }

    return false;
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
    return sol_util_strtod_n(nptr, endptr, -1, false);
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
                    r = -ENOTSUP;
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
                    r = -ENOTSUP;
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
    const struct sol_flow_node_type_format_string_formatted_form_options
    *opts =
        (const struct sol_flow_node_type_format_string_formatted_form_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_FORMAT_STRING_FORMATTED_FORM_OPTIONS_API_VERSION,
        -EINVAL);

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
            r = -EINVAL;
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
            if (!*ptr) {
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
                    *tmp = backup;
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
                    *tmp = backup;
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
        sol_buffer_fini(&mdata->formatted_value);
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
        if (chunk->type != STR_FORMAT_LITERAL) {
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
        SOL_FLOW_NODE_TYPE_FORMAT_STRING_FORMATTED_FORM__OUT__SELECTED,
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

    r = sol_flow_packet_get_bool(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->enabled = value;

    return 0;
}

#include "format-gen.c"
