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

#include <ctype.h>
#include <errno.h>

#ifdef HAVE_LOCALE
#include <locale.h>
#endif

#include "sol-flow/string.h"
#include "sol-flow-internal.h"

#include "string-ascii.h"
#include "string-common.h"
#include "string-regexp.h"
#include "string-uuid.h"

struct string_data {
    int32_t n;
    char *string[2];
};

struct string_concatenate_data {
    struct string_data base;
    char *separator;
};

struct string_compare_data {
    struct string_data base;
    bool ignore_case : 1;
};

static void
string_close(struct sol_flow_node *node, void *data)
{
    struct string_data *mdata = data;

    free(mdata->string[0]);
    free(mdata->string[1]);
}

static void
string_concatenate_close(struct sol_flow_node *node, void *data)
{
    struct string_concatenate_data *mdata = data;

    string_close(node, data);
    free(mdata->separator);
}

static bool
get_string_by_port(const struct sol_flow_packet *packet,
    uint16_t port,
    struct string_data *mdata)
{
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->string[port] && !strcmp(mdata->string[port], in_value))
        return false;

    free(mdata->string[port]);

    mdata->string[port] = strdup(in_value);
    if (!mdata->string[port])
        return false;

    if (!mdata->string[0] || !mdata->string[1])
        return false;

    return true;
}

static int
string_concatenate_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_concatenate_data *mdata = data;
    const struct sol_flow_node_type_string_concatenate_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_STRING_CONCATENATE_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_string_concatenate_options *)options;

    if (opts->bytes.val < 0) {
        SOL_WRN("Option 'bytes' (%" PRId32 ") must be a positive "
            "amount of bytes to be copied or zero if whole strings "
            "should be concatenated. Considering zero.", opts->bytes.val);
        mdata->base.n = 0;
    } else
        mdata->base.n = opts->bytes.val;

    if (opts->separator) {
        mdata->separator = strdup(opts->separator);
        if (!mdata->separator) {
            SOL_WRN("Failed to duplicate separator string");
            return -ENOMEM;
        }
    }

    return 0;
}

static int
string_concat(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_concatenate_data *mdata = data;
    char *dest;
    int r, len;

    if (!get_string_by_port(packet, port, &mdata->base))
        return 0;

    len = strlen(mdata->base.string[0]) + strlen(mdata->base.string[1]) + 1;
    if (mdata->separator)
        len += strlen(mdata->separator);

    dest = calloc(len, sizeof(*dest));
    SOL_NULL_CHECK(dest, -ENOMEM);

    dest = strcpy(dest, mdata->base.string[0]);

    if (mdata->separator)
        dest = strcat(dest, mdata->separator);

    if (!mdata->base.n)
        dest = strcat(dest, mdata->base.string[1]);
    else
        dest = strncat(dest, mdata->base.string[1], mdata->base.n);

    r = sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_CONCATENATE__OUT__OUT,
        dest);

    return r;
}

static int
string_compare_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_compare_data *mdata = data;
    const struct sol_flow_node_type_string_compare_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_STRING_COMPARE_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_string_compare_options *)options;

    if (opts->bytes.val < 0) {
        SOL_WRN("Option 'bytes' (%" PRId32 ") must be a positive "
            "amount of bytes to be compared or zero if whole strings "
            "should be compared. Considering zero.", opts->bytes.val);
        mdata->base.n = 0;
    } else
        mdata->base.n = opts->bytes.val;

    mdata->ignore_case = opts->ignore_case;

    return 0;
}

static int
string_compare(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_compare_data *mdata = data;
    uint32_t result;
    int r;

    if (!get_string_by_port(packet, port, &mdata->base))
        return 0;

    if (mdata->base.n) {
        if (mdata->ignore_case)
            result = strncasecmp(mdata->base.string[0], mdata->base.string[1],
                mdata->base.n);
        else
            result = strncmp(mdata->base.string[0], mdata->base.string[1],
                mdata->base.n);
    } else {
        if (mdata->ignore_case)
            result = strcasecmp(mdata->base.string[0], mdata->base.string[1]);
        else
            result = strcmp(mdata->base.string[0], mdata->base.string[1]);
    }

    r = sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_COMPARE__OUT__EQUAL, result == 0);
    if (r < 0)
        return r;

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_COMPARE__OUT__OUT, result);
}

struct string_slice_data {
    struct sol_flow_node *node;
    char *str;
    int idx[2];
};

static int
get_slice_idx_by_port(const struct sol_flow_packet *packet,
    uint16_t port,
    struct string_slice_data *mdata)
{
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->idx[port] = in_value;

    return 0;
}

static int
slice_do(struct string_slice_data *mdata)
{
    struct sol_str_slice slice;
    int32_t start = mdata->idx[0], end = mdata->idx[1],
        len = strlen(mdata->str);

    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    start = sol_util_int32_clamp(0, len, start);
    end = sol_util_int32_clamp(0, len, end);

    slice = SOL_STR_SLICE_STR(mdata->str + start, end - start);
    return sol_flow_send_string_slice_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_STRING_SLICE__OUT__OUT, slice);
}

static int
string_slice_input(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_slice_data *mdata = data;
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->str);
    mdata->str = strdup(in_value);
    if (!mdata->str)
        return -ENOMEM;

    return slice_do(mdata);
}

static int
string_slice(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_slice_data *mdata = data;
    int r;

    r = get_slice_idx_by_port(packet, port, mdata);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->str)
        return slice_do(mdata);

    return 0;
}

static int
string_slice_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_slice_data *mdata = data;

    const struct sol_flow_node_type_string_slice_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_STRING_SLICE_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_string_slice_options *)options;

    mdata->idx[0] = opts->start.val;
    mdata->idx[1] = opts->end.val;
    mdata->node = node;

    return 0;
}

static void
string_slice_close(struct sol_flow_node *node, void *data)
{
    struct string_slice_data *mdata = data;

    free(mdata->str);
}

struct string_length_data {
    uint32_t n;
};

static int
string_length_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_data *mdata = data;
    const struct sol_flow_node_type_string_length_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_STRING_LENGTH_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_string_length_options *)options;

    if (opts->maxlen.val < 0) {
        SOL_WRN("Option 'maxlen' (%" PRId32 ") must be a positive "
            "or zero if the whole string should be measured. "
            "Considering zero.", opts->maxlen.val);
        mdata->n = 0;
    } else
        mdata->n = opts->maxlen.val;

    return 0;
}

static int
string_length_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_length_data *mdata = data;
    const char *in_value;
    uint32_t result;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->n)
        result = strnlen(in_value, mdata->n);
    else
        result = strlen(in_value);

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_LENGTH__OUT__OUT, result);
}

struct string_split_data {
    struct sol_vector substrings;
    char *string;
    char *separator;
    int32_t index, max_split;
};

static int
string_split_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_split_data *mdata = data;
    const struct sol_flow_node_type_string_split_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (options, SOL_FLOW_NODE_TYPE_STRING_SPLIT_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_string_split_options *)options;

    if (opts->index.val < 0) {
        SOL_WRN("Index (%" PRId32 ") must be a non-negative value",
            opts->index.val);
        return -EINVAL;
    }
    if (opts->max_split.val < 0) {
        SOL_WRN("Max split (%" PRId32 ") must be a non-negative value",
            opts->max_split.val);
        return -EINVAL;
    }
    mdata->index = opts->index.val;
    mdata->max_split = opts->max_split.val;

    if (opts->separator) {
        mdata->separator = strdup(opts->separator);
        SOL_NULL_CHECK(mdata->separator, -ENOMEM);
    }

    sol_vector_init(&mdata->substrings, sizeof(struct sol_str_slice));

    return 0;
}

static void
clear_substrings(struct string_split_data *mdata)
{
    sol_vector_clear(&mdata->substrings);
}

static void
string_split_close(struct sol_flow_node *node, void *data)
{
    struct string_split_data *mdata = data;

    clear_substrings(mdata);
    free(mdata->string);
    free(mdata->separator);
}

static int
calculate_substrings(struct string_split_data *mdata,
    struct sol_flow_node *node)
{
    if (!(mdata->string && mdata->separator))
        return 0;

    sol_vector_clear(&mdata->substrings);

    mdata->substrings = sol_util_str_split(sol_str_slice_from_str
            (mdata->string), mdata->separator, mdata->max_split);

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_SPLIT__OUT__LENGTH,
        mdata->substrings.len);
}

static int
send_substring(struct string_split_data *mdata, struct sol_flow_node *node)
{
    int32_t len;
    struct sol_str_slice *sub_slice;

    if (!(mdata->string && mdata->separator))
        return 0;

    len = mdata->substrings.len;
    if (!len)
        return 0;

    if (mdata->index >= len) {
        SOL_WRN("Index (%" PRId32 ") greater than substrings "
            "length (%" PRId32 ").", mdata->index, len);
        return -EINVAL;
    }

    sub_slice = sol_vector_get(&mdata->substrings, mdata->index);
    return sol_flow_send_string_slice_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_SPLIT__OUT__OUT, *sub_slice);
}

static int
set_string_index(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_split_data *mdata = data;
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value < 0) {
        SOL_WRN("Index (%" PRId32 ") must be a non-negative value", in_value);
        return -EINVAL;
    }
    mdata->index = in_value;

    return send_substring(mdata, node);
}

static int
set_max_split(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_split_data *mdata = data;
    int r;
    int32_t in_value;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value < 0) {
        SOL_WRN("Max split (%" PRId32 ") must be a non-negative value",
            in_value);
        return -EINVAL;
    }
    mdata->max_split = in_value;

    r = calculate_substrings(mdata, node);
    SOL_INT_CHECK(r, < 0, r);

    return send_substring(mdata, node);
}

static int
get_string(const struct sol_flow_packet *packet,
    char **string)
{
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(*string);
    if (!in_value)
        *string = NULL;
    else {
        *string = strdup(in_value);
        SOL_NULL_CHECK(*string, -ENOMEM);
    }

    return 0;
}

static int
set_string_separator(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_split_data *mdata = data;
    int r;

    r = get_string(packet, &mdata->separator);
    SOL_INT_CHECK(r, < 0, r);

    r = calculate_substrings(mdata, node);
    SOL_INT_CHECK(r, < 0, r);

    return send_substring(mdata, node);
}

static int
string_split(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_split_data *mdata = data;
    int r;

    r = get_string(packet, &mdata->string);
    SOL_INT_CHECK(r, < 0, r);

    r = calculate_substrings(mdata, node);
    SOL_INT_CHECK(r, < 0, r);

    return send_substring(mdata, node);
}

static int
string_change_case(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet,
    bool lower)
{
    int (*case_func)(int c) = lower ? tolower : toupper;
    const char *value;
    char *cpy, *ptr;
    int r;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    cpy = strdup(value);
    SOL_NULL_CHECK(cpy, -ENOMEM);

    for (ptr = cpy; *ptr; ptr++)
        *ptr = case_func(*ptr);

    r = sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_UPPERCASE__OUT__OUT, cpy);
    free(cpy);

    return r;
}

static int
string_lowercase(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    return string_change_case(node, data, port, conn_id, packet, true);
}

static int
string_uppercase(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    return string_change_case(node, data, port, conn_id, packet, false);
}

struct string_replace_data {
    struct sol_flow_node *node;
    char *orig_string;
    char *from_string;
    char *to_string;
    int32_t max_replace;
    bool forward_on_no_match;
};

static int
string_replace_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_replace_data *mdata = data;
    const struct sol_flow_node_type_string_replace_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_STRING_REPLACE_OPTIONS_API_VERSION, -EINVAL);

    opts = (const struct sol_flow_node_type_string_replace_options *)options;

    mdata->node = node;
    mdata->forward_on_no_match = opts->forward_on_no_match;
    if (opts->max_replace.val < 0) {
        SOL_WRN("Max replace (%" PRId32 ") must be a non-negative value",
            opts->max_replace.val);
        return -EINVAL;
    }
    mdata->max_replace = opts->max_replace.val ? : INT32_MAX;

    mdata->from_string = strdup(opts->from_string);
    if (!opts->from_string)
        return -ENOMEM;

    mdata->to_string = strdup(opts->to_string);
    if (!opts->to_string) {
        free(mdata->from_string);
        return -ENOMEM;
    }

    return 0;
}

static void
string_replace_close(struct sol_flow_node *node, void *data)
{
    struct string_replace_data *mdata = data;

    free(mdata->orig_string);
    free(mdata->from_string);
    free(mdata->to_string);
}

static int
string_replace_do(struct string_replace_data *mdata,
    const char *value)
{
    char *orig_string_replaced;
    bool replaced;

    if (!value)
        goto replace;

    free(mdata->orig_string);
    mdata->orig_string = strdup(value);
    if (!mdata->orig_string)
        return -ENOMEM;

replace:
    orig_string_replaced = string_replace(mdata->node, mdata->orig_string,
        mdata->from_string, mdata->to_string, &replaced, mdata->max_replace);
    if (!orig_string_replaced) { // err packets already generated by the call
        return -EINVAL;
    }

    if (!mdata->forward_on_no_match && !replaced) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Fail on matching '%s' on string"
            " %s", mdata->from_string, mdata->orig_string);
        free(orig_string_replaced);
        return -EINVAL;
    }

    return sol_flow_send_string_take_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_STRING_REPLACE__OUT__OUT, orig_string_replaced);
}

static int
string_replace_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_replace_data *mdata = data;
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return string_replace_do(mdata, in_value);
}

static int
set_replace_from(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_replace_data *mdata = data;
    int r;

    r = get_string(packet, &mdata->from_string);
    SOL_INT_CHECK(r, < 0, r);

    if (!mdata->orig_string)
        return 0;

    return string_replace_do(mdata, NULL);
}

static int
set_replace_to(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_replace_data *mdata = data;
    int r;

    r = get_string(packet, &mdata->to_string);
    SOL_INT_CHECK(r, < 0, r);

    if (!mdata->orig_string)
        return 0;

    return string_replace_do(mdata, NULL);
}

static int
set_max_replace(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_replace_data *mdata = data;
    int r;
    int32_t in_value;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value < 0) {
        SOL_WRN("Max replace (%" PRId32 ") must be a non-negative value",
            in_value);
        return -EINVAL;
    }
    mdata->max_replace = in_value;

    if (!mdata->orig_string)
        return 0;

    return string_replace_do(mdata, NULL);
}

struct string_prefix_suffix_data {
    struct sol_flow_node *node;
    char *in_str, *sub_str;
    int32_t start, end;
};

static int
prefix_suffix_open(struct string_prefix_suffix_data *mdata,
    int32_t start,
    int32_t end)
{
    mdata->start = start;
    if (mdata->start < 0)
        mdata->start = 0;

    if (start > 0 && end > 0 && end < start) {
        SOL_WRN("'end' option (%" PRId32 ") must be greater than "
            "the 'start' (%" PRId32 ") one", end, start);
        return -EINVAL;
    }
    mdata->end = end;
    if (mdata->end < 0)
        mdata->end = INT32_MAX;

    return 0;
}

static void
string_prefix_suffix_close(struct sol_flow_node *node, void *data)
{
    struct string_prefix_suffix_data *mdata = data;

    free(mdata->in_str);
    free(mdata->sub_str);
}

static int
string_starts_with_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct string_prefix_suffix_data *mdata = data;
    const struct sol_flow_node_type_string_starts_with_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_STRING_STARTS_WITH_OPTIONS_API_VERSION, -EINVAL);

    opts = (const struct sol_flow_node_type_string_starts_with_options *)options;

    mdata->node = node;
    r = prefix_suffix_open(mdata, opts->start.val, opts->end.val);
    SOL_INT_CHECK(r, < 0, r);

    if (!opts->prefix) {
        SOL_WRN("Option 'prefix' must not be NULL");
        return -EINVAL;
    }

    mdata->sub_str = strdup(opts->prefix);
    if (!mdata->sub_str)
        return -ENOMEM;

    return 0;
}

static int
string_ends_with_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct string_prefix_suffix_data *mdata = data;
    const struct sol_flow_node_type_string_ends_with_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_STRING_ENDS_WITH_OPTIONS_API_VERSION, -EINVAL);

    opts = (const struct sol_flow_node_type_string_ends_with_options *)options;

    mdata->node = node;
    r = prefix_suffix_open(mdata, opts->start.val, opts->end.val);
    SOL_INT_CHECK(r, < 0, r);

    if (!opts->suffix) {
        SOL_WRN("Option 'suffix' must not be NULL");
        return -EINVAL;
    }

    mdata->sub_str = strdup(opts->suffix);
    if (!mdata->sub_str)
        return -ENOMEM;

    return 0;
}

static int
prefix_suffix_match_do(struct string_prefix_suffix_data *mdata,
    const char *new_in_str,
    bool start)
{
    int32_t end;
    bool ret = false;
    int32_t in_str_len, sub_str_len, off;

    if (!new_in_str) {
        in_str_len = strlen(mdata->in_str);
        goto starts_with;
    }

    free(mdata->in_str);
    mdata->in_str = strdup(new_in_str);
    if (!mdata->in_str)
        return -ENOMEM;
    in_str_len = strlen(mdata->in_str);

starts_with:
    if (mdata->start > in_str_len || mdata->end < mdata->start)
        goto end;

    sub_str_len = strlen(mdata->sub_str);
    end = mdata->end > 0 ? mdata->end : in_str_len;
    if (end > in_str_len)
        end = in_str_len;
    end -= sub_str_len;

    if (end < mdata->start)
        goto end;

    if (!start)
        off = end;
    else
        off = mdata->start;

    ret = memcmp(mdata->in_str + off, mdata->sub_str, sub_str_len) == 0;

end:
    return sol_flow_send_boolean_packet(mdata->node,
        start ? SOL_FLOW_NODE_TYPE_STRING_STARTS_WITH__OUT__OUT :
        SOL_FLOW_NODE_TYPE_STRING_ENDS_WITH__OUT__OUT, ret);
}

static int
string_prefix_suffix_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_prefix_suffix_data *mdata = data;
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return prefix_suffix_match_do(mdata, in_value, true);
}

static int
set_prefix_suffix_sub_str(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_prefix_suffix_data *mdata = data;
    const char *sub_str;
    int r;

    r = sol_flow_packet_get_string(packet, &sub_str);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->sub_str);
    mdata->sub_str = strdup(sub_str);
    if (!mdata->sub_str)
        return -ENOMEM;

    if (!mdata->in_str)
        return 0;

    return prefix_suffix_match_do(mdata, NULL, true);
}

static int
set_prefix_suffix_start(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_prefix_suffix_data *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->start = value;
    if (mdata->start < 0)
        mdata->start = 0;

    if (!mdata->in_str || !mdata->sub_str)
        return 0;

    return prefix_suffix_match_do(mdata, NULL, true);
}

static int
set_prefix_suffix_end(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_prefix_suffix_data *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->end = value;
    if (mdata->end < 0)
        mdata->end = INT32_MAX;

    if (!mdata->in_str || !mdata->sub_str)
        return 0;

    return prefix_suffix_match_do(mdata, NULL, true);
}

#include "string-gen.c"
