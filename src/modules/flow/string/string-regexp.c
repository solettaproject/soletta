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

#ifdef USE_LIBPCRE
#include <pcre.h>
#endif

#include "sol-flow-internal.h"

#include "string-gen.h"
#include "string-regexp.h"

#ifdef USE_LIBPCRE
static struct sol_vector
string_regexp_search_and_split(struct string_regexp_search_data *mdata)
{
    int r;
    pcre *compiled_re;
    int pcre_error_offset;
    pcre_extra *pcre_extra;
    size_t str_len, match_sz;
    int *match_vector, sub_str_count;
    const char *pcre_error_str = NULL;
    struct sol_vector v = SOL_VECTOR_INIT(struct sol_str_slice);

#define CREATE_SLICE(_str, _len) \
    do { \
        struct sol_str_slice *s; \
        s = sol_vector_append(&v); \
        if (!s) \
            goto err; \
        s->data = (const char *)_str; \
        s->len = _len; \
    } while (0)

    //mdata->string should never be NULL
    str_len = strlen(mdata->string);
    if (!str_len)
        return v;

    compiled_re = pcre_compile(mdata->regexp, PCRE_UTF8,
        &pcre_error_str, &pcre_error_offset, NULL);
    if (!compiled_re) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Could not compile '%s': %s\n", mdata->regexp,
            pcre_error_str);
        return v;
    }

    r = pcre_fullinfo(compiled_re, NULL, PCRE_INFO_CAPTURECOUNT,
        &sub_str_count);
    if (r < 0) {
        sol_flow_send_error_packet(mdata->node, EINVAL, "Could not"
            "extract info from compiled regular expression %s",
            mdata->regexp);
        goto err_compile;
    }

    match_sz = (sub_str_count + 1) * 3;
    match_vector = alloca(match_sz);

    //A null pcre_extra is fine (no optimization possible), errors are
    //reported on the last argument
    pcre_extra = pcre_study(compiled_re, 0, &pcre_error_str);
    if (pcre_error_str != NULL) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Error optimizing '%s': %s\n", mdata->regexp,
            pcre_error_str);
        goto err;
    }

    r = pcre_exec(compiled_re, pcre_extra, mdata->string, str_len,
        0, 0, match_vector, match_sz);

    if (r < 0) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Fail on matching regular expression '%s' on string %s\n",
            mdata->regexp, mdata->string);
        goto err;
    } else {
        int i;

        /* The value returned by pcre_exec() is one more than the
         * highest numbered pair that has been set. If the vector is
         * too small to hold all the captured substring offsets, it is
         * used as far as possible (up to two-thirds of its length),
         * and the function returns a value of zero.
         */
        if (r == 0) { // should not happen, but let's treat the case
            sol_flow_send_error_packet(mdata->node, EINVAL,
                "A memory overflow happened while executing"
                " regular expression '%s' on string %s\n",
                mdata->regexp, mdata->string);
            goto err;
        }
        for (i = 0; i < r; i++) {
            CREATE_SLICE(mdata->string + match_vector[i * 2],
                match_vector[i * 2 + 1] - match_vector[i * 2]);
        }
    }

err:
    if (pcre_extra != NULL)
        pcre_free(pcre_extra);
err_compile:
    pcre_free(compiled_re);

    return v;
#undef CREATE_SLICE
}

static int
calculate_regexp_substrings(struct string_regexp_search_data *mdata,
    struct sol_flow_node *node)
{
    if (!(mdata->string && mdata->regexp))
        return 0;

    sol_vector_clear(&mdata->substrings);

    mdata->substrings = string_regexp_search_and_split(mdata);

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_REGEXP_SEARCH__OUT__LENGTH,
        mdata->substrings.len);
}

static int
send_regexp_substring(struct string_regexp_search_data *mdata)
{
    struct sol_str_slice *sub_slice;
    int len;

    len = mdata->substrings.len;
    if (!len)
        return 0;

    if (mdata->index >= len) {
        SOL_WRN("Index (%d) greater than substrings "
            "length (%d).", mdata->index, len);
        return -EINVAL;
    }

    sub_slice = sol_vector_get(&mdata->substrings, mdata->index);

    return sol_flow_send_string_slice_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_STRING_REGEXP_SEARCH__OUT__OUT, *sub_slice);
}

#endif

int
string_regexp_search_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_regexp_search_data *mdata = data;
    const struct sol_flow_node_type_string_regexp_search_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (options, SOL_FLOW_NODE_TYPE_STRING_REGEXP_SEARCH_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_string_regexp_search_options *)options;

    if (opts->index.val < 0) {
        SOL_WRN("Index (%" PRId32 ") must be a non-negative value",
            opts->index.val);
        return -EINVAL;
    }
    if (opts->max_regexp_search.val < 0) {
        SOL_WRN("Max regexp matches (%" PRId32 ") must be"
            " a non-negative value", opts->max_regexp_search.val);
        return -EINVAL;
    }
    if (!opts->regexp || !strlen(opts->regexp)) {
        SOL_WRN("A non-empty regular expression string must be provided");
        return -EINVAL;
    }

    mdata->node = node;
    mdata->index = opts->index.val;
    mdata->max_regexp_search = opts->max_regexp_search.val > 0 ?
        (size_t)opts->max_regexp_search.val : SIZE_MAX;
    mdata->regexp = strdup(opts->regexp);

    sol_vector_init(&mdata->substrings, sizeof(struct sol_str_slice));

    return 0;
}

void
string_regexp_search_close(struct sol_flow_node *node, void *data)
{
    struct string_regexp_search_data *mdata = data;

    sol_vector_clear(&mdata->substrings);
    free(mdata->string);
    free(mdata->regexp);
}

int
string_regexp_search(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
#ifdef USE_LIBPCRE
    struct string_regexp_search_data *mdata = data;
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->string);
    if (!in_value) {
        mdata->string = NULL;
        return 0;
    } else
        mdata->string = strdup(in_value);

    r = calculate_regexp_substrings(mdata, node);
    SOL_INT_CHECK(r, < 0, r);

    return send_regexp_substring(mdata);
#else
    sol_flow_send_error_packet(node, ENOTSUP, "The string/regexp-search"
        " can't work on this Soletta build -- libpcre dependency is needed "
        "in order for this node to work");
    return -EINVAL;
#endif
}

int
set_string_regexp_pattern(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
#ifdef USE_LIBPCRE
    struct string_regexp_search_data *mdata = data;
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->regexp);
    if (!in_value || !strlen(in_value)) {
        sol_flow_send_error_packet(mdata->node, EINVAL, "The regular"
            " expression must never be empty");
        return -EINVAL;
    } else
        mdata->regexp = strdup(in_value);

    r = calculate_regexp_substrings(mdata, node);
    SOL_INT_CHECK(r, < 0, r);

    return send_regexp_substring(mdata);
#else
    sol_flow_send_error_packet(node, ENOTSUP, "The string/regexp-search"
        " can't work on this Soletta build -- libpcre dependency is needed "
        "in order for this node to work");
    return -EINVAL;
#endif
}

int
set_string_regexp_index(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
#ifdef USE_LIBPCRE
    struct string_regexp_search_data *mdata = data;
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value < 0) {
        SOL_WRN("Index (%" PRId32 ") must be a non-negative value", in_value);
        return -EINVAL;
    }
    mdata->index = in_value;

    return send_regexp_substring(mdata);
#else
    sol_flow_send_error_packet(node, ENOTSUP, "The string/regexp-search"
        " can't work on this Soletta build -- libpcre dependency is needed "
        "in order for this node to work");
    return -EINVAL;
#endif
}

int
set_string_regexp_max_match(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
#ifdef USE_LIBPCRE
    struct string_regexp_search_data *mdata = data;
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value < 0) {
        SOL_WRN("Maximum regexp matches counter (%" PRId32 ") must "
            "be a non-negative value", in_value);
        return -EINVAL;
    }
    mdata->max_regexp_search = in_value;

    r = calculate_regexp_substrings(mdata, node);
    SOL_INT_CHECK(r, < 0, r);

    return send_regexp_substring(mdata);
#else
    sol_flow_send_error_packet(node, ENOTSUP, "The string/regexp-search"
        " can't work on this Soletta build -- libpcre dependency is needed "
        "in order for this node to work");
    return -EINVAL;
#endif
}
