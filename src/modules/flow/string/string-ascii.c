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

//FIXME: change to build system warning, not compile time

#warning "You're building the string nodes module without i18n support -- some nodes will only act properly on pure ASCII input, not the intended utf-8 for Soletta. Please re-configure after you have ICU development packages installed to get the intended string nodes behavior."

#include <ctype.h>
#include <errno.h>

#ifdef HAVE_LOCALE
#include <locale.h>
#endif

#include "string-gen.h"

#include "sol-flow-internal.h"
#include "sol-util.h"

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
get_string(const struct sol_flow_packet *packet,
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

    if (!get_string(packet, port, &mdata->base))
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

    if (!get_string(packet, port, &mdata->base))
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
    int index, max_split;
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
        SOL_WRN("Index (%d) must be a non-negative value", opts->index.val);
        return -EINVAL;
    }
    if (opts->max_split.val < 0) {
        SOL_WRN("Max split (%d) must be a non-negative value",
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
    int len;
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
        SOL_WRN("Index (%d) must be a non-negative value", in_value);
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
        SOL_WRN("Max split (%d) must be a non-negative value", in_value);
        return -EINVAL;
    }
    mdata->max_split = in_value;

    r = calculate_substrings(mdata, node);
    SOL_INT_CHECK(r, < 0, r);

    return send_substring(mdata, node);
}

static int
split_get_string(const struct sol_flow_packet *packet,
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

    r = split_get_string(packet, &mdata->separator);
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

    r = split_get_string(packet, &mdata->string);
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

#include "string-gen.c"
