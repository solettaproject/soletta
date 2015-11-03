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

#include "sol-flow/string.h"
#include "sol-flow-internal.h"

#include "string-regexp.h"

#ifdef USE_LIBPCRE
static int
pcre_compile_do(struct sol_flow_node *node,
    const char *regexp,
    pcre **compiled_re,
    pcre_extra **p_extra,
    int *sub_match_count)
{
    int r;
    int pcre_error_offset;
    const char *pcre_error_str = NULL;

    SOL_NULL_CHECK(compiled_re, -EINVAL);
    SOL_NULL_CHECK(p_extra, -EINVAL);
    SOL_NULL_CHECK(sub_match_count, -EINVAL);

    *compiled_re = NULL;
    *p_extra = NULL;
    *sub_match_count = 0;

    *compiled_re = pcre_compile(regexp, PCRE_UTF8,
        &pcre_error_str, &pcre_error_offset, NULL);
    if (!*compiled_re) {
        sol_flow_send_error_packet(node, EINVAL,
            "Could not compile '%s': %s", regexp, pcre_error_str);
        return -EINVAL;
    }

    r = pcre_fullinfo(*compiled_re, NULL, PCRE_INFO_CAPTURECOUNT,
        sub_match_count);
    if (r < 0) {
        sol_flow_send_error_packet(node, EINVAL, "Could not"
            "extract info from compiled regular expression %s", regexp);
        pcre_free(compiled_re);
        return -EINVAL;
    }

    //A null pcre_extra is fine (no optimization possible), errors are
    //reported on the last argument
    *p_extra = pcre_study(*compiled_re, 0, &pcre_error_str);
    if (pcre_error_str != NULL) {
        sol_flow_send_error_packet(node, EINVAL,
            "Error optimizing '%s': %s", regexp, pcre_error_str);
        pcre_free(compiled_re);
        return -EINVAL;
    }

    return 0;
}

static struct sol_vector
string_regexp_search_and_split(struct string_regexp_search_data *mdata)
{
    int r;
    size_t str_len, match_sz;
    pcre *compiled_re = NULL;
    pcre_extra *p_extra = NULL;
    int *match_vector, sub_match_count = 0;
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

    r = pcre_compile_do(mdata->node, mdata->regexp, &compiled_re,
        &p_extra, &sub_match_count);
    SOL_INT_CHECK(r, < 0, v);

    match_sz = (sub_match_count + 1) * 3;
    match_vector = calloc(match_sz, sizeof(*match_vector));

    r = pcre_exec(compiled_re, p_extra, mdata->string, str_len,
        0, 0, match_vector, match_sz);

    if (r < 0) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Fail on matching regular expression '%s' on string %s",
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
                " regular expression '%s' on string %s",
                mdata->regexp, mdata->string);
            goto err;
        }
        for (i = 0; i < r; i++) {
            CREATE_SLICE(mdata->string + match_vector[i * 2],
                match_vector[i * 2 + 1] - match_vector[i * 2]);
        }
    }

err:
    if (p_extra != NULL)
        pcre_free(p_extra);
    pcre_free(compiled_re);
    free(match_vector);

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

static int
string_regexp_replace_get_matches(struct string_regexp_replace_data *mdata,
    off_t whence,
    int **match_vector,
    size_t *match_cnt)
{
    int r;
    size_t str_len, match_sz;
    pcre *compiled_re = NULL;
    pcre_extra *p_extra = NULL;
    int *matchv = NULL, sub_match_count = 0;

    *match_cnt = 0;
    *match_vector = NULL;

    //mdata->orig_string should never be NULL
    str_len = strlen(mdata->orig_string + whence);
    if (!str_len)
        return -EINVAL;

    r = pcre_compile_do(mdata->node, mdata->regexp, &compiled_re,
        &p_extra, &sub_match_count);
    SOL_INT_CHECK(r, < 0, -EINVAL);

    match_sz = (sub_match_count + 1) * 3;
    matchv = calloc(match_sz, sizeof(*matchv));
    if (!matchv) {
        r = -ENOMEM;
        goto err;
    }

    r = pcre_exec(compiled_re, p_extra, mdata->orig_string + whence,
        str_len, 0, 0, matchv, match_sz);
    if (r < 0)
        goto err;
    else {
        /* The value returned by pcre_exec() is one more than the
         * highest numbered pair that has been set. If the vector is
         * too small to hold all the captured substring offsets, it is
         * used as far as possible (up to two-thirds of its length),
         * and the function returns a value of zero.
         */
        if (r == 0) { // should not happen, but let's treat the case
            sol_flow_send_error_packet(mdata->node, EINVAL,
                "A memory overflow happened while executing"
                " regular expression '%s' on string %s",
                mdata->regexp, mdata->orig_string);
            r = -EINVAL;
            goto err;
        }
        *match_cnt = r;
        *match_vector = matchv;
    }

    if (p_extra != NULL)
        pcre_free(p_extra);
    pcre_free(compiled_re);

    return 0;

err:
    free(matchv);
    if (p_extra != NULL)
        pcre_free(p_extra);
    pcre_free(compiled_re);

    return r;
}

/*
 * This function takes mdata->to_regexp and copies it into a struct
 * sol_buffer, with all the number ref entries already replaced by the
 * due text. It assumes that @a buf is already initialized.
 */
static int
get_unescaped_regexp_replacement_str(struct string_regexp_replace_data *mdata,
    struct sol_buffer *buf,
    int *match_vector,
    size_t match_cnt)
{
    int r;
    struct sol_str_slice slice;
    char *ptr = mdata->to_regexp;

    SOL_NULL_CHECK(buf, -EINVAL);

    /* \g<number> or \<number> syntax. Go on as far as numbers are in
     * [0-9] range. Leading zeros are permitted.
     */
    while (*ptr) {
        if (*ptr == '\\') {
            int n;
            uint64_t grp_num = 0;

            ptr++;

            if (*ptr == 'g')
                ptr++;

            for (; (n = *ptr - '0') >= 0 && n <= 9; ptr++) {
                if (!grp_num && !n) {
                    continue;
                }

                r = sol_util_uint64_mul(grp_num, 10, &grp_num);
                SOL_INT_CHECK_GOTO(r, < 0, err);

                r = sol_util_uint64_add(grp_num, n, &grp_num);
                SOL_INT_CHECK_GOTO(r, < 0, err);
            }

            if (!grp_num || grp_num > match_cnt) {
                sol_flow_send_error_packet(mdata->node, EINVAL,
                    "Could not process '%s' pattern's reference "
                    "to non-existent subpattern on '%s'",
                    mdata->to_regexp, mdata->regexp);
                r = -EINVAL;
                goto err;
            }

            slice = SOL_STR_SLICE_STR(mdata->orig_string
                + match_vector[grp_num * 2],
                match_vector[grp_num * 2 + 1] - match_vector[grp_num * 2]);
            r = sol_buffer_append_slice(buf, slice);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        } else {
            slice = SOL_STR_SLICE_STR(ptr++, 1);
            r = sol_buffer_append_slice(buf, slice);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        }
    }

    if (buf->used >= (SIZE_MAX - 1) ||
        sol_buffer_ensure(buf, buf->used + 1) < 0)
        goto err;
    *(char *)sol_buffer_at(buf, buf->used) = '\0';

    return 0;

err:
    return r;
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

    if (opts->index < 0) {
        SOL_WRN("Index (%" PRId32 ") must be a non-negative value",
            opts->index);
        return -EINVAL;
    }
    if (opts->max_regexp_search < 0) {
        SOL_WRN("Max regexp matches (%" PRId32 ") must be"
            " a non-negative value", opts->max_regexp_search);
        return -EINVAL;
    }
    if (!opts->regexp || !strlen(opts->regexp)) {
        SOL_WRN("A non-empty regular expression string must be provided");
        return -EINVAL;
    }

    mdata->node = node;
    mdata->index = opts->index;
    mdata->max_regexp_search = opts->max_regexp_search > 0 ?
        (size_t)opts->max_regexp_search : SIZE_MAX;
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

int
string_regexp_replace_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_regexp_replace_data *mdata = data;
    const struct sol_flow_node_type_string_regexp_replace_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (options, SOL_FLOW_NODE_TYPE_STRING_REGEXP_REPLACE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_string_regexp_replace_options *)options;

    mdata->node = node;
    mdata->forward_on_no_match = opts->forward_on_no_match;

    if (opts->max_regexp_replace < 0) {
        SOL_WRN("Max regexp matches (%" PRId32 ") must be"
            " a non-negative value", opts->max_regexp_replace);
        return -EINVAL;
    }
    mdata->max_regexp_replace = opts->max_regexp_replace > 0 ?
        (size_t)opts->max_regexp_replace : SIZE_MAX;

    if (!opts->regexp || !strlen(opts->regexp)) {
        SOL_WRN("A non-empty regular expression string must be provided");
        return -EINVAL;
    }
    mdata->regexp = strdup(opts->regexp);

    SOL_NULL_CHECK_MSG(opts->to, -EINVAL, "A non-null substitution regular expression string"
        " must be provided");
    mdata->to_regexp = strdup(opts->to);

    return 0;
}

void
string_regexp_replace_close(struct sol_flow_node *node, void *data)
{
    struct string_regexp_replace_data *mdata = data;

    free(mdata->orig_string);
    free(mdata->regexp);
    free(mdata->to_regexp);
}

int
string_regexp_replace(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
#ifdef USE_LIBPCRE
    struct string_regexp_replace_data *mdata = data;
    struct sol_buffer repl_buf, final_buf = { 0 };
    size_t pos = 0, str_len, count;
    struct sol_str_slice slice;
    int *match_vector = NULL;
    size_t match_cnt = 0;
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->orig_string);
    mdata->orig_string = strdup(in_value);
    str_len = strlen(mdata->orig_string);
    count = mdata->max_regexp_replace;

    sol_buffer_init(&final_buf);

    while (pos < str_len && count) {
        free(match_vector);

        r = string_regexp_replace_get_matches
                (mdata, pos, &match_vector, &match_cnt);
        if (r < 0) {
            /* no match from the beginning means no match at all */
            if (pos == 0) {
                if (!mdata->forward_on_no_match) {
                    sol_flow_send_error_packet(mdata->node, EINVAL,
                        "Fail on matching regular expression '%s' on string"
                        " %s", mdata->regexp, mdata->orig_string);
                    goto err;
                } else
                    goto forward;
            }
            /* no match onwards means we got at least one match
             * before, not an error scenario */
            else {
                goto end;
            }
        }

        sol_buffer_init(&repl_buf);

        /* mdata->to_regexp with numbered back references translated */
        r = get_unescaped_regexp_replacement_str
                (mdata, &repl_buf, match_vector, match_cnt);
        if (r < 0) {
            sol_buffer_fini(&repl_buf);
            goto err;
        }

        /* copy verbatim the chars before the match */
        if (match_vector[0] > 0) {
            slice = SOL_STR_SLICE_STR(mdata->orig_string + pos,
                match_vector[0]);
            r = sol_buffer_append_slice(&final_buf, slice);
            if (r < 0) {
                sol_buffer_fini(&repl_buf);
                goto err;
            }
        }

        /* insert the replacement afterwards */
        r = sol_buffer_append_slice
                (&final_buf, sol_buffer_get_slice(&repl_buf));
        sol_buffer_fini(&repl_buf);
        if (r < 0)
            goto err;

        pos += match_vector[1];
        count--;
    }

end:
    /* insert the unmatched suffix verbatim */
    slice = SOL_STR_SLICE_STR(mdata->orig_string + pos,
        strlen(mdata->orig_string + pos) + 1);
    r = sol_buffer_append_slice(&final_buf, slice);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    if (final_buf.used >= (SIZE_MAX - 1) ||
        sol_buffer_ensure(&final_buf, final_buf.used + 1) < 0)
        goto err;

    *(char *)sol_buffer_at(&final_buf, final_buf.used) = '\0';

    r = sol_flow_send_string_slice_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_STRING_REGEXP_REPLACE__OUT__OUT,
        sol_buffer_get_slice(&final_buf));

err:
    free(match_vector);
    sol_buffer_fini(&final_buf);

    return r;

forward:
    free(match_vector);
    sol_buffer_fini(&final_buf);

    return sol_flow_send_string_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_STRING_REGEXP_REPLACE__OUT__OUT,
        mdata->orig_string);
#else
    sol_flow_send_error_packet(node, ENOTSUP, "The string/regexp-search"
        " can't work on this Soletta build -- libpcre dependency is needed "
        "in order for this node to work");
    return -EINVAL;
#endif
}

int
set_string_regexp_replace_pattern(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    return 0;
}

int
set_string_regexp_replace_to(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    return 0;
}

int
set_string_regexp_replace_max_match(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    return 0;
}
