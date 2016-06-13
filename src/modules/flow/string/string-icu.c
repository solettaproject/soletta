/*
 * This file is part of the Soletta™ Project
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

#include <ctype.h>
#include <errno.h>

#include "sol-flow/string.h"
#include "sol-flow-internal.h"

#include "string-common.h"
#include "string-icu.h"
#include "string-uuid.h"

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
    SOL_NULL_CHECK(*ret_icu_str, -ENOMEM);

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
    SOL_NULL_CHECK(*ret_utf_str, -ENOMEM);

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

#define CONCATENATE_IN_LEN (SOL_FLOW_NODE_TYPE_STRING_CONCATENATE__IN__IN_LAST + 1)
struct string_concatenate_data {
    UChar *string[CONCATENATE_IN_LEN];
    UChar *separator;
    uint32_t var_initialized;
    uint32_t var_connected;
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
    uint8_t i;

    for (i = 0; i < CONCATENATE_IN_LEN; i++)
        free(mdata->string[i]);
    free(mdata->separator);
}

static int
get_string_by_port(const struct sol_flow_packet *packet,
    uint16_t port,
    UChar **string)
{
    UChar *new_str = NULL;
    const char *in_value;
    UErrorCode err;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = icu_str_from_utf8(in_value, &new_str, &err);
    SOL_INT_CHECK(r, < 0, r);

    if (string[port] && !u_strCompare(string[port],
        -1, new_str, -1, r)) {
        free(new_str);
        return 0;
    }

    free(string[port]);
    string[port] = new_str;
    return 0;
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

    if (opts->separator) {
        UErrorCode err;
        int r;

        r = icu_str_from_utf8(opts->separator, &mdata->separator, &err);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

static int
string_concat_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct string_concatenate_data *mdata = data;

    mdata->var_connected |= 1u << port;
    return 0;
}

static int
string_concat_to_buffer(struct sol_buffer *buffer, UChar **string, uint32_t var_initialized, const UChar *separator)
{
    int r;
    uint8_t i, count = 0;
    size_t sep_size = 0;

    if (separator)
        sep_size = u_strlen(separator) * sizeof(UChar);
    for (i = 0; i < CONCATENATE_IN_LEN; i++) {
        if (!(var_initialized & (1u << i)))
            continue;

        if (count && separator) {
            r = sol_buffer_append_bytes(buffer, (uint8_t *)separator, sep_size);
            SOL_INT_CHECK(r, < 0, r);
        }

        r = sol_buffer_append_bytes(buffer, (uint8_t *)string[i],
            u_strlen(string[i]) * sizeof(UChar));
        SOL_INT_CHECK(r, < 0, r);
        count++;
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
    struct sol_buffer buffer = SOL_BUFFER_INIT_FLAGS(NULL, 0,
        SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    UChar *dest = NULL;
    char *final = NULL;
    UErrorCode err;
    int r;
    size_t buf_len;

    r = get_string_by_port(packet, port, mdata->string);
    SOL_INT_CHECK(r, < 0, r);

    mdata->var_initialized |= 1u << port;
    if (mdata->var_initialized != mdata->var_connected)
        return 0;

    r = string_concat_to_buffer(&buffer, mdata->string,
        mdata->var_initialized, mdata->separator);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    dest = sol_buffer_steal(&buffer, &buf_len);

    r = utf8_from_icu_str_slice(dest, buf_len / sizeof(*dest), &final, &err);
    if (r < 0) {
        free(dest);
        sol_flow_send_error_packet_str(node, -r, u_errorName(err));
        return r;
    }

    free(dest);
    r = sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_CONCATENATE__OUT__OUT, final);

error:
    sol_buffer_fini(&buffer);
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

    if (opts->chars < 0) {
        SOL_WRN("Option 'chars' (%" PRId32 ") must be a positive "
            "amount of chars to be compared or zero if whole strings "
            "should be compared. Considering zero.", opts->chars);
        mdata->base.n = 0;
    } else
        mdata->base.n = opts->chars;

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

    r = get_string_by_port(packet, port, mdata->base.string);
    SOL_INT_CHECK(r, < 0, r);

    if (!mdata->base.string[0] || !mdata->base.string[1])
        return false;

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

struct string_slice_data {
    struct sol_flow_node *node;
    UChar *str;
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
        len = u_strlen(mdata->str);
    char *outstr = NULL;
    UErrorCode err;
    int r;

    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    start = sol_util_int32_clamp(0, len, start);
    end = sol_util_int32_clamp(0, len, end);

    slice = SOL_STR_SLICE_STR((char *)(mdata->str + start), end - start);

    r = utf8_from_icu_str_slice((const UChar *)slice.data, slice.len,
        &outstr, &err);
    if (r < 0) {
        sol_flow_send_error_packet_str(mdata->node, -r, u_errorName(err));
        return r;
    }

    return sol_flow_send_string_take_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_STRING_SLICE__OUT__OUT, outstr);
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
    UErrorCode err;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->str);
    mdata->str = NULL;
    r = icu_str_from_utf8(in_value, &mdata->str, &err);
    if (r < 0) {
        sol_flow_send_error_packet_str(mdata->node, -r, u_errorName(err));
        return r;
    }

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
        SOL_FLOW_NODE_TYPE_STRING_SLICE_OPTIONS_API_VERSION, -EINVAL);

    opts = (const struct sol_flow_node_type_string_slice_options *)options;

    mdata->idx[0] = opts->start;
    mdata->idx[1] = opts->end;
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

    if (opts->maxlen < 0) {
        SOL_WRN("Option 'maxlen' (%" PRId32 ") must be a positive "
            "or zero if the whole string should be measured. "
            "Considering zero.", opts->maxlen);
        mdata->n = 0;
    } else
        mdata->n = opts->maxlen;

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
        result = sol_util_min((uint32_t)u_strlen(value), mdata->n);
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

    if (opts->index < 0) {
        SOL_WRN("Index (%" PRId32 ") must be a non-negative value",
            opts->index);
        return -EINVAL;
    }
    if (opts->max_split < 0) {
        SOL_WRN("Max split (%" PRId32 ") must be a non-negative value",
            opts->max_split);
        return -EINVAL;
    }
    mdata->index = opts->index;
    mdata->max_split = opts->max_split;

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
string_split_close(struct sol_flow_node *node, void *data)
{
    struct string_split_data *mdata = data;

    sol_vector_clear(&mdata->substrings);
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
        struct sol_str_slice *s; \
        s = sol_vector_append(&v); \
        if (!s) \
            goto err; \
        s->data = (const char *)_str; \
        s->len = _len; \
    } while (0)

    while (str && (v.len < max_split + 1)) {
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

    return v;

err:
    sol_vector_clear(&v);
    return v;
#undef CREATE_SLICE
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
        sol_flow_send_error_packet_str(node, -r, u_errorName(err));
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
        sol_flow_send_error_packet_str(node, errno, u_errorName(err));
        return -errno;
    }
    u_lower = calloc(u_changed_sz + 1, sizeof(*u_lower));
    if (!u_lower) {
        errno = ENOMEM;
        free(u_orig);
        sol_flow_send_error_packet_errno(node, errno);
        return -errno;
    }

    err = U_ZERO_ERROR;
    case_func(u_lower, u_changed_sz + 1, u_orig, -1, curr_locale, &err);
    if (U_FAILURE(err) || u_lower[u_changed_sz] != 0) {
        errno = EINVAL;
        free(u_orig);
        free(u_lower);
        sol_flow_send_error_packet_str(node, errno, u_errorName(err));
        return -errno;
    }

    r = utf8_from_icu_str_slice(u_lower, u_changed_sz, &final, &err);
    if (r < 0) {
        free(u_orig);
        free(u_lower);
        sol_flow_send_error_packet_str(node, -r, u_errorName(err));
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
    bool forward_on_no_match;
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
    mdata->forward_on_no_match = opts->forward_on_no_match;
    if (opts->max_replace < 0) {
        SOL_WRN("Max replace (%" PRId32 ") must be a non-negative value",
            opts->max_replace);
        return -EINVAL;
    }
    mdata->max_replace = opts->max_replace ? : INT32_MAX;

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
    bool replaced;
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
        mdata->from_string, mdata->to_string, &replaced, mdata->max_replace);
    if (!orig_string_replaced) { // err packets already generated by the call
        return -EINVAL;
    }

    if (!mdata->forward_on_no_match && !replaced) {
        char *from;
        r = utf8_from_icu_str_slice(mdata->from_string, -1, &from, &err);
        SOL_INT_CHECK(r, < 0, r);

        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Fail on matching '%s' on string" " %s", from, value);
        free(from);
        free(orig_string_replaced);
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

struct string_prefix_suffix_data {
    struct sol_flow_node *node;
    UChar *in_str, *sub_str;
    int32_t start, end;
    bool starts_with;
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
    UErrorCode err;
    struct string_prefix_suffix_data *mdata = data;
    const struct sol_flow_node_type_string_starts_with_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_STRING_STARTS_WITH_OPTIONS_API_VERSION, -EINVAL);

    opts = (const struct sol_flow_node_type_string_starts_with_options *)options;

    mdata->node = node;
    r = prefix_suffix_open(mdata, opts->start, opts->end);
    SOL_INT_CHECK(r, < 0, r);

    SOL_NULL_CHECK_MSG(opts->prefix, -EINVAL, "Option 'prefix' must not be NULL");

    r = icu_str_from_utf8(opts->prefix, &mdata->sub_str, &err);
    SOL_INT_CHECK(r, < 0, r);

    mdata->starts_with = true;

    return 0;
}

static int
string_ends_with_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    UErrorCode err;
    struct string_prefix_suffix_data *mdata = data;
    const struct sol_flow_node_type_string_ends_with_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_STRING_ENDS_WITH_OPTIONS_API_VERSION, -EINVAL);

    opts = (const struct sol_flow_node_type_string_ends_with_options *)options;

    mdata->node = node;
    r = prefix_suffix_open(mdata, opts->start, opts->end);
    SOL_INT_CHECK(r, < 0, r);

    SOL_NULL_CHECK_MSG(opts->suffix, -EINVAL, "Option 'suffix' must not be NULL");

    r = icu_str_from_utf8(opts->suffix, &mdata->sub_str, &err);
    SOL_INT_CHECK(r, < 0, r);

    mdata->starts_with = false;

    return 0;
}

static int
prefix_suffix_match_do(struct string_prefix_suffix_data *mdata,
    const char *new_in_str,
    bool starts_with)
{
    int r;
    int32_t end;
    UErrorCode err;
    bool ret = false;
    int32_t in_str_len, sub_str_len, off;

    if (!new_in_str) {
        in_str_len = u_strlen(mdata->in_str);
        goto starts_with;
    }

    free(mdata->in_str);
    mdata->in_str = NULL;

    r = icu_str_from_utf8(new_in_str, &mdata->in_str, &err);
    SOL_INT_CHECK(r, < 0, r);
    in_str_len = u_strlen(mdata->in_str);

starts_with:
    if (mdata->start > in_str_len || mdata->end < mdata->start)
        goto end;

    sub_str_len = u_strlen(mdata->sub_str);
    end = mdata->end > 0 ? mdata->end : in_str_len;
    if (end > in_str_len)
        end = in_str_len;
    end -= sub_str_len;

    if (end < mdata->start)
        goto end;

    if (!starts_with)
        off = end;
    else
        off = mdata->start;

    ret = u_memcmp(mdata->in_str + off, mdata->sub_str, sub_str_len) == 0;

end:
    return sol_flow_send_boolean_packet(mdata->node,
        starts_with ? SOL_FLOW_NODE_TYPE_STRING_STARTS_WITH__OUT__OUT :
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

    return prefix_suffix_match_do(mdata, in_value, mdata->starts_with);
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
    UErrorCode err;
    int r;

    r = sol_flow_packet_get_string(packet, &sub_str);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->sub_str);
    mdata->sub_str = NULL;

    r = icu_str_from_utf8(sub_str, &mdata->sub_str, &err);
    SOL_INT_CHECK(r, < 0, r);

    if (!mdata->in_str)
        return 0;

    return prefix_suffix_match_do(mdata, NULL, mdata->starts_with);
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

    return prefix_suffix_match_do(mdata, NULL, mdata->starts_with);
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

    return prefix_suffix_match_do(mdata, NULL, mdata->starts_with);
}

#include "string-gen.c"
