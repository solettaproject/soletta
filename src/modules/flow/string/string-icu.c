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

#include <unicode/ustring.h>
#include <unicode/utypes.h>

#include "sol-flow-internal.h"

#include "string-gen.h"
#include "string-icu.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

//ret_icu_str must be freed after usage
int
icu_str_from_utf8(const char *utf_str,
    UChar **ret_icu_str,
    UErrorCode *ret_icu_err)
{
    int32_t icu_sz;

    SOL_NULL_CHECK(ret_icu_str, -EINVAL);
    SOL_NULL_CHECK(ret_icu_err, -EINVAL);

    icu_sz = 0;
    *ret_icu_str = NULL;
    *ret_icu_err = U_ZERO_ERROR;
    u_strFromUTF8(NULL, 0, &icu_sz, utf_str, -1, ret_icu_err);
    if (U_FAILURE(*ret_icu_err) && *ret_icu_err != U_BUFFER_OVERFLOW_ERROR)
        return -EINVAL;

    *ret_icu_str = calloc(icu_sz + 1, sizeof(UChar));
    if (!*ret_icu_str)
        return -ENOMEM;

    *ret_icu_err = U_ZERO_ERROR;
    u_strFromUTF8(*ret_icu_str, icu_sz + 1, NULL, utf_str, -1,
        ret_icu_err);
    if (U_FAILURE(*ret_icu_err)) {
        free(*ret_icu_str);
        *ret_icu_str = NULL;
        return -EINVAL;
    }

    return 0;
}

//ret_utf_str must be freed after usage. -1 on icu_str_sz will assume
//icu_str is null terminated
static int
utf8_from_icu_str_slice(const UChar *icu_str,
    int32_t icu_str_sz,
    char **ret_utf_str,
    UErrorCode *ret_icu_err)
{
    int32_t utf_sz;

    SOL_NULL_CHECK(ret_utf_str, -EINVAL);
    SOL_NULL_CHECK(ret_icu_err, -EINVAL);

    *ret_icu_err = U_ZERO_ERROR;
    u_strToUTF8(NULL, 0, &utf_sz, icu_str, icu_str_sz, ret_icu_err);
    if (U_FAILURE(*ret_icu_err) && *ret_icu_err != U_BUFFER_OVERFLOW_ERROR)
        return -EINVAL;

    *ret_utf_str = calloc(utf_sz + 1, sizeof(char));
    if (!*ret_utf_str)
        return -ENOMEM;

    *ret_icu_err = U_ZERO_ERROR;
    u_strToUTF8(*ret_utf_str, utf_sz + 1, NULL, icu_str, icu_str_sz,
        ret_icu_err);
    if (U_FAILURE(*ret_icu_err) || (*ret_utf_str)[utf_sz] != '\0') {
        free(*ret_utf_str);
        *ret_utf_str = NULL;
        return -EINVAL;
    }

    return 0;
}

#ifdef HAVE_LOCALE
#include <locale.h>
#endif

struct string_data {
    int32_t n;
    UChar *string[2];
};

struct string_concatenate_data {
    struct string_data base;
    UChar *separator;
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
    UChar *new_str = NULL;
    const char *in_value;
    UErrorCode err;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, false);

    r = icu_str_from_utf8(in_value, &new_str, &err);
    SOL_INT_CHECK(r, < 0, false);

    if (mdata->string[port] && !u_strCompare(mdata->string[port],
        -1, new_str, -1, false)) {
        free(new_str);
        return false;
    }

    free(mdata->string[port]);

    mdata->string[port] = new_str;

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
        UErrorCode err;
        int r;

        r = icu_str_from_utf8(opts->separator, &mdata->separator, &err);
        SOL_INT_CHECK(r, < 0, r);
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
    UChar *dest = NULL;
    char *final = NULL;
    UErrorCode err;
    int r, len;

    if (!get_string_by_port(packet, port, &mdata->base))
        return 0;

    len = u_strlen(mdata->base.string[0])
        + u_strlen(mdata->base.string[1]) + 1;
    if (mdata->separator)
        len += u_strlen(mdata->separator);

    dest = calloc(len, sizeof(*dest));
    SOL_NULL_CHECK(dest, -ENOMEM);

    dest = u_strcpy(dest, mdata->base.string[0]);

    if (mdata->separator)
        dest = u_strcat(dest, mdata->separator);

    if (!mdata->base.n)
        dest = u_strcat(dest, mdata->base.string[1]);
    else
        dest = u_strncat(dest, mdata->base.string[1], mdata->base.n);

    r = utf8_from_icu_str_slice(dest, len, &final, &err);
    if (r < 0) {
        free(dest);
        sol_flow_send_error_packet(node, -r, u_errorName(err));
        return r;
    }

    free(dest);
    r = sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_CONCATENATE__OUT__OUT, final);

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
    UErrorCode err;
    int r;

    if (!get_string_by_port(packet, port, &mdata->base))
        return 0;

    if (mdata->base.n) {
        if (mdata->ignore_case) {
            err = U_ZERO_ERROR;
            result = u_strCaseCompare(mdata->base.string[0], mdata->base.n,
                mdata->base.string[1], mdata->base.n, U_FOLD_CASE_DEFAULT,
                &err);
            if (U_FAILURE(err))
                return -EINVAL;
        } else {
            result = u_strCompare(mdata->base.string[0], mdata->base.n,
                mdata->base.string[1], mdata->base.n, false);
        }
    } else {
        if (mdata->ignore_case) {
            err = U_ZERO_ERROR;
            result = u_strCaseCompare(mdata->base.string[0], -1,
                mdata->base.string[1], -1, U_FOLD_CASE_DEFAULT, &err);
            if (U_FAILURE(err))
                return -EINVAL;
        } else {
            result = u_strCompare(mdata->base.string[0], -1,
                mdata->base.string[1], -1, false);
        }
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
    UChar *value = NULL;
    uint32_t result;
    UErrorCode err;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = icu_str_from_utf8(in_value, &value, &err);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->n)
        result = MIN((uint32_t)u_strlen(value), mdata->n);
    else
        result = u_strlen(value);

    free(value);

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_LENGTH__OUT__OUT, result);
}

struct string_split_data {
    struct sol_vector substrings;
    UChar *string;
    UChar *separator;
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
        UErrorCode err;
        int r;

        r = icu_str_from_utf8(opts->separator, &mdata->separator, &err);
        SOL_INT_CHECK(r, < 0, r);
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

static struct sol_vector
icu_str_split(const struct sol_str_slice slice,
    const UChar *delim,
    size_t max_split)
{
    struct sol_vector v = SOL_VECTOR_INIT(struct sol_str_slice);
    const UChar *str = (const UChar *)slice.data;
    int32_t dlen;
    size_t len;

    if (!slice.len || !delim)
        return v;

    max_split = (max_split) ? : slice.len - 1;
    dlen = u_strlen(delim);
    len = slice.len;

#define CREATE_SLICE(_str, _len) \
    do { \
        s = sol_vector_append(&v); \
        if (!s) \
            goto err; \
        s->data = (const char *)_str; \
        s->len = _len; \
    } while (0)

    while (str && (v.len < max_split + 1)) {
        struct sol_str_slice *s;
        UChar *token = u_strFindFirst(str, len, delim, dlen);
        if (!token) {
            CREATE_SLICE(str, len);
            break;
        }

        if (v.len == (uint16_t)max_split)
            CREATE_SLICE(str, len);
        else
            CREATE_SLICE(str, (size_t)(token - str));

        len -= (token - str) + dlen;
        str = token + dlen;
    }
#undef CREATE_SLICE

    return v;

err:
    sol_vector_clear(&v);
    return v;
}

static inline struct sol_str_slice
str_slice_from_icu_str(const UChar *s)
{
    return SOL_STR_SLICE_STR((const char *)s, u_strlen(s));
}

static int
calculate_substrings(struct string_split_data *mdata,
    struct sol_flow_node *node)
{
    if (!(mdata->string && mdata->separator))
        return 0;

    sol_vector_clear(&mdata->substrings);

    mdata->substrings = icu_str_split(str_slice_from_icu_str
            (mdata->string), mdata->separator, mdata->max_split);

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_SPLIT__OUT__LENGTH,
        mdata->substrings.len);
}

static int
send_substring(struct string_split_data *mdata, struct sol_flow_node *node)
{
    struct sol_str_slice *sub_slice;
    char *outstr = NULL;
    UErrorCode err;
    int len, r;

    if (!(mdata->string && mdata->separator))
        return 0;

    len = mdata->substrings.len;
    if (!len)
        return 0;

    if (mdata->index >= len) {
        SOL_WRN("Index (%d) greater than substrings "
            "length (%d).", mdata->index, len);
        return -EINVAL;
    }

    sub_slice = sol_vector_get(&mdata->substrings, mdata->index);

    r = utf8_from_icu_str_slice((const UChar *)sub_slice->data,
        sub_slice->len, &outstr, &err);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_SPLIT__OUT__OUT, outstr);
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
    UChar **string)
{
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(*string);
    if (!in_value)
        *string = NULL;
    else {
        UErrorCode err;

        r = icu_str_from_utf8(in_value, string, &err);
        SOL_INT_CHECK(r, < 0, r);
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
    int32_t (*case_func)(UChar *dest, int32_t destCapacity, const UChar *src,
        int32_t srcLength, const char *locale,
        UErrorCode *pErrorCode) = lower ? u_strToLower : u_strToUpper;
    UChar *u_orig = NULL, *u_lower = NULL;
    const char *curr_locale = NULL;
    const char *value = NULL;
    int32_t u_changed_sz;
    char *final = NULL;
    UErrorCode err;
    int r;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = icu_str_from_utf8(value, &u_orig, &err);
    if (r < 0) {
        sol_flow_send_error_packet(node, -r, u_errorName(err));
        return -errno;
    }

#ifdef HAVE_LOCALE
    curr_locale = setlocale(LC_ALL, NULL);
    if (curr_locale == NULL) {
        curr_locale = "";
    }
#else
    curr_locale = "";
#endif

    err = U_ZERO_ERROR;
    u_changed_sz = case_func(NULL, 0, u_orig, -1, curr_locale, &err);
    if (U_FAILURE(err) && err != U_BUFFER_OVERFLOW_ERROR) {
        errno = EINVAL;
        free(u_orig);
        sol_flow_send_error_packet(node, errno, u_errorName(err));
        return -errno;
    }
    u_lower = calloc(u_changed_sz + 1, sizeof(*u_lower));
    if (!u_lower) {
        errno = ENOMEM;
        free(u_orig);
        sol_flow_send_error_packet(node, errno, "Out of memory");
        return -errno;
    }

    err = U_ZERO_ERROR;
    case_func(u_lower, u_changed_sz + 1, u_orig, -1, curr_locale, &err);
    if (U_FAILURE(err) || u_lower[u_changed_sz] != 0) {
        errno = EINVAL;
        free(u_orig);
        free(u_lower);
        sol_flow_send_error_packet(node, errno, u_errorName(err));
        return -errno;
    }

    r = utf8_from_icu_str_slice(u_lower, u_changed_sz, &final, &err);
    if (r < 0) {
        free(u_orig);
        free(u_lower);
        sol_flow_send_error_packet(node, -r, u_errorName(err));
        return r;
    }

    r = sol_flow_send_string_take_packet(node,
        lower ? SOL_FLOW_NODE_TYPE_STRING_LOWERCASE__OUT__OUT :
        SOL_FLOW_NODE_TYPE_STRING_UPPERCASE__OUT__OUT, final);
    free(u_orig);
    free(u_lower);

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
    UChar *orig_string;
    UChar *from_string;
    UChar *to_string;
    int32_t max_replace;
};

static int
string_replace_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    UErrorCode err;
    struct string_replace_data *mdata = data;
    const struct sol_flow_node_type_string_replace_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_STRING_REPLACE_OPTIONS_API_VERSION, -EINVAL);

    opts = (const struct sol_flow_node_type_string_replace_options *)options;

    mdata->node = node;
    if (opts->max_replace.val < 0) {
        SOL_WRN("Max replace (%" PRId32 ") must be a non-negative value",
            opts->max_replace.val);
        return -EINVAL;
    }
    mdata->max_replace = opts->max_replace.val ? : INT32_MAX;

    if (!opts->from_string) {
        SOL_WRN("Option 'from_string' must not be NULL");
        return -EINVAL;
    }
    if (!opts->to_string) {
        SOL_WRN("Option 'to_string' must not be NULL");
        return -EINVAL;
    }

    r = icu_str_from_utf8(opts->from_string, &mdata->from_string, &err);
    SOL_INT_CHECK(r, < 0, r);

    r = icu_str_from_utf8(opts->to_string, &mdata->to_string, &err);
    if (r < 0) {
        free(mdata->from_string);
        return r;
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
    UChar *orig_string_replaced;
    UErrorCode err;
    char *final;
    int r;

    if (!value)
        goto replace;

    free(mdata->orig_string);
    mdata->orig_string = NULL;

    r = icu_str_from_utf8(value, &mdata->orig_string, &err);
    SOL_INT_CHECK(r, < 0, r);

replace:
    orig_string_replaced = string_replace(mdata->node, mdata->orig_string,
        mdata->from_string, mdata->to_string, mdata->max_replace);
    if (!orig_string_replaced) { // err packets already generated by the call
        return -EINVAL;
    }

    r = utf8_from_icu_str_slice(orig_string_replaced, -1, &final, &err);
    if (r < 0) {
        free(orig_string_replaced);
        sol_flow_send_error_packet(mdata->node, -r, "Failed to replace "
            "string: %s", u_errorName(err));
        return r;
    }

    free(orig_string_replaced);
    return sol_flow_send_string_take_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_STRING_REPLACE__OUT__OUT, final);
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
    int32_t in_value;
    int r;

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

#include "string-gen.c"
