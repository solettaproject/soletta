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

#ifdef HAVE_ICU
#include <unicode/ustring.h>
#include <unicode/utypes.h>
#include <unicode/uchar.h>
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define ICU_STR_FROM_UTF8(_icu_str, _icu_sz, _icu_err_val, \
        _utf_str, _inval_ret, _no_mem_ret)           \
    do { \
        _icu_err_val = U_ZERO_ERROR; \
        u_strFromUTF8(NULL, 0, &_icu_sz, _utf_str, -1, &_icu_err_val); \
        if (U_FAILURE(_icu_err_val) && \
            _icu_err_val != U_BUFFER_OVERFLOW_ERROR) \
            return _inval_ret; \
        _icu_str = calloc(_icu_sz + 1, sizeof(*_icu_str)); \
        if (!_icu_str) \
            return _no_mem_ret; \
        _icu_err_val = U_ZERO_ERROR; \
        u_strFromUTF8(_icu_str, _icu_sz + 1, &_icu_sz, _utf_str, \
            -1, &_icu_err_val); \
        if (U_FAILURE(_icu_err_val) && \
            _icu_err_val != U_BUFFER_OVERFLOW_ERROR) { \
            free(_icu_str); \
            return _inval_ret; \
        } \
    } while (0)

#define ICU_STR_FROM_UTF8_GOTO(_icu_str, _icu_sz, _icu_err_val, \
        _utf_str, _inval_ret, _no_mem_ret, _icu_conv_goto) \
    do { \
        _icu_err_val = U_ZERO_ERROR; \
        u_strFromUTF8(NULL, 0, &_icu_sz, _utf_str, -1, &_icu_err_val); \
        if (U_FAILURE(_icu_err_val) && \
            _icu_err_val != U_BUFFER_OVERFLOW_ERROR) \
            return _inval_ret; \
        _icu_str = calloc(_icu_sz + 1, sizeof(*_icu_str)); \
        if (!_icu_str) \
            return _no_mem_ret; \
        _icu_err_val = U_ZERO_ERROR; \
        u_strFromUTF8(_icu_str, _icu_sz + 1, &_icu_sz, _utf_str, \
            -1, &_icu_err_val); \
        if (U_FAILURE(_icu_err_val) && \
            _icu_err_val != U_BUFFER_OVERFLOW_ERROR) { \
            errno = -_inval_ret; \
            goto _icu_conv_goto; \
        } \
    } while (0)

//(UChar *) cast because we abuse the struct sol_str_slice with UChar
//* instead of char *
#define UTF8_FROM_ICU_STR(_utf_str, _utf_sz, _icu_err_val, \
        _icu_str, _icu_str_sz, _inval_ret, _no_mem_ret) \
    do { \
        err = U_ZERO_ERROR; \
        u_strToUTF8(NULL, 0, &_utf_sz, (UChar *)_icu_str, _icu_str_sz, \
            &_icu_err_val); \
        if (U_FAILURE(err) && err != U_BUFFER_OVERFLOW_ERROR) \
            return _inval_ret; \
        _utf_str = calloc(_utf_sz + 1, sizeof(*_utf_str)); \
        if (!_utf_str) \
            return _no_mem_ret; \
        err = U_ZERO_ERROR; \
        u_strToUTF8(_utf_str, _utf_sz + 1, &_utf_sz, (UChar *)_icu_str, \
            _icu_str_sz, &_icu_err_val); \
        if (U_FAILURE(err) || _utf_str[_utf_sz] != '\0') { \
            free(_utf_str); \
            return _inval_ret; \
        } \
    } while (0)

#define UTF8_FROM_ICU_STR_ERRNO_GOTO(_utf_str, _utf_sz, _icu_err_val, \
        _icu_str, _icu_str_sz, _sz_calc_err, _sz_calc_goto, _mem_alloc_err, \
        _mem_alloc_goto, _icu_conv_err, _icu_conv_goto) \
    do { \
        err = U_ZERO_ERROR; \
        u_strToUTF8(NULL, 0, &_utf_sz, _icu_str, _icu_str_sz, \
            &_icu_err_val); \
        if (U_FAILURE(err) && err != U_BUFFER_OVERFLOW_ERROR) { \
            errno = _sz_calc_err; \
            goto _sz_calc_goto; \
        } \
        _utf_str = calloc(_utf_sz + 1, sizeof(*_utf_str)); \
        if (!_utf_str) { \
            errno = _mem_alloc_err; \
            goto _mem_alloc_goto; \
        } \
        err = U_ZERO_ERROR; \
        u_strToUTF8(_utf_str, _utf_sz + 1, &_utf_sz, _icu_str, \
            _icu_str_sz, &_icu_err_val); \
        free(_icu_str); \
        if (U_FAILURE(err) || _utf_str[_utf_sz] != '\0') { \
            errno = _icu_conv_err; \
            goto _icu_conv_goto; \
        } \
    } while (0)

#else
#warning "You're building the string nodes module without i18n support -- some nodes will only act properly on pure ASCII input, not the intended utf-8 for Soletta"
#endif

#ifdef HAVE_LOCALE
#include <locale.h>
#endif

#include "string-gen.h"

#include "sol-flow-internal.h"
#include "sol-util.h"

struct string_data {
    int32_t n;
#ifdef HAVE_ICU
    UChar *string[2];
#else
    char *string[2];
#endif
};

struct string_concatenate_data {
    struct string_data base;
#ifdef HAVE_ICU
    UChar *separator;
#else
    char *separator;
#endif
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
#ifdef HAVE_ICU
    UChar *new_str = NULL;
    int32_t new_sz;
    UErrorCode err;
#endif
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

#ifdef HAVE_ICU
    ICU_STR_FROM_UTF8(new_str, new_sz, err, in_value, false, false);

    if (mdata->string[port] && !u_strCompare(mdata->string[port],
        -1, new_str, -1, false)) {
        free(new_str);
        return false;
    }
#else
    if (mdata->string[port] && !strcmp(mdata->string[port], in_value))
        return false;
#endif

    free(mdata->string[port]);

#ifdef HAVE_ICU
    mdata->string[port] = new_str;
#else
    mdata->string[port] = strdup(in_value);
#endif

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
#ifdef HAVE_ICU
        UErrorCode err;
        int32_t sz;

        ICU_STR_FROM_UTF8(mdata->separator, sz, err, opts->separator,
            -EINVAL, -ENOMEM);
#else
        mdata->separator = strdup(opts->separator);
#endif
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
#ifdef HAVE_ICU
#define _STRLEN u_strlen
#define _STRCPY u_strcpy
#define _STRCAT u_strcat
#define _STRNCAT u_strncat
#else
#define _STRLEN strlen
#define _STRCPY strcpy
#define _STRCAT strcat
#define _STRNCAT strncat
#endif

    struct string_concatenate_data *mdata = data;
#ifdef HAVE_ICU
    UErrorCode err;
    UChar *dest;
    char *final;
    int32_t sz;
#else
    char *dest;
#endif
    int r, len;

    if (!get_string(packet, port, &mdata->base))
        return 0;

    len = _STRLEN(mdata->base.string[0]) + _STRLEN(mdata->base.string[1]) + 1;
    if (mdata->separator)
        len += _STRLEN(mdata->separator);

    dest = calloc(len, sizeof(*dest));
    SOL_NULL_CHECK(dest, -ENOMEM);

    dest = _STRCPY(dest, mdata->base.string[0]);

    if (mdata->separator)
        dest = _STRCAT(dest, mdata->separator);

    if (!mdata->base.n)
        dest = _STRCAT(dest, mdata->base.string[1]);
    else
        dest = _STRNCAT(dest, mdata->base.string[1], mdata->base.n);

#ifdef HAVE_ICU
    UTF8_FROM_ICU_STR_ERRNO_GOTO(final, sz, err, dest, len, EINVAL, fail_sz,
        ENOMEM, fail_sz, EINVAL, fail_to_utf8);

    r = sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_CONCATENATE__OUT__OUT, final);

    return r;

fail_sz:
    free(dest);
fail_to_utf8:
    sol_flow_send_error_packet(node, -errno, u_errorName(err));
    return -errno;
#else
    r = sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_CONCATENATE__OUT__OUT,
        dest);

    return r;
#endif

#undef _STRLEN
#undef _STRCPY
#undef _STRCAT
#undef _STRNCAT
}

static int
string_compare_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
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

#ifdef HAVE_ICU
    UErrorCode err;
#endif

    if (!get_string(packet, port, &mdata->base))
        return 0;

#ifdef HAVE_ICU
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
#else
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
#endif

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
string_length_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
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
#ifdef HAVE_ICU
    UChar *value = NULL;
    UErrorCode err;
    int32_t sz;
#define _VALUE value
#define _STRLEN u_strlen
#define _STRNLEN(_str, _sz) MIN((uint32_t)u_strlen(_str), _sz)
#else
#define _VALUE in_value
#define _STRLEN strlen
#define _STRNLEN strnlen
#endif
    struct string_length_data *mdata = data;
    const char *in_value;
    uint32_t result;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

#ifdef HAVE_ICU
    ICU_STR_FROM_UTF8(_VALUE, sz, err, in_value, -EINVAL, -ENOMEM);
#endif

    if (mdata->n)
        result = _STRNLEN(_VALUE, mdata->n);
    else
        result = _STRLEN(_VALUE);

#ifdef HAVE_ICU
    free(value);
#endif

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_LENGTH__OUT__OUT, result);
#undef _STRLEN
#undef _STRNLEN
#undef _VALUE
}

struct string_split_data {
    struct sol_vector substrings;
#ifdef HAVE_ICU
    UChar *string;
    UChar *separator;
#else
    char *string;
    char *separator;
#endif
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
#ifdef HAVE_ICU
        UChar *new_str = NULL;
        int32_t new_sz;
        UErrorCode err;

        ICU_STR_FROM_UTF8(new_str, new_sz, err, opts->separator,
            -EINVAL, -ENOMEM);

        mdata->separator = new_str;
#else
        mdata->separator = strdup(opts->separator);
        SOL_NULL_CHECK(mdata->separator, -ENOMEM);
#endif
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

#ifdef HAVE_ICU
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
#endif

static int
calculate_substrings(struct string_split_data *mdata,
    struct sol_flow_node *node)
{
    if (!(mdata->string && mdata->separator))
        return 0;

    sol_vector_clear(&mdata->substrings);

#ifdef HAVE_ICU
    mdata->substrings = icu_str_split(str_slice_from_icu_str
            (mdata->string), mdata->separator, mdata->max_split);
#else
    mdata->substrings = sol_util_str_split(sol_str_slice_from_str
            (mdata->string), mdata->separator, mdata->max_split);
#endif

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_SPLIT__OUT__LENGTH,
        mdata->substrings.len);
}

static int
send_substring(struct string_split_data *mdata, struct sol_flow_node *node)
{
    int len;
    struct sol_str_slice *sub_slice;

#ifdef HAVE_ICU
    char *outstr = NULL;
    UErrorCode err;
    int32_t sz;
#endif

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
#ifdef HAVE_ICU
    UTF8_FROM_ICU_STR(outstr, sz, err, sub_slice->data, sub_slice->len,
        -EINVAL, -ENOMEM);

    return sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_SPLIT__OUT__OUT, outstr);
#else
    return sol_flow_send_string_slice_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_SPLIT__OUT__OUT, sub_slice);
#endif
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
#ifdef HAVE_ICU
    UChar **string
#else
    char **string
#endif
    )
{
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(*string);
    if (!in_value)
        *string = NULL;
    else {
#ifdef HAVE_ICU
        UChar *new_str = NULL;
        int32_t new_sz;
        UErrorCode err;

        ICU_STR_FROM_UTF8(new_str, new_sz, err, in_value, -EINVAL, -ENOMEM);
        *string = new_str;
#else
        *string = strdup(in_value);
        SOL_NULL_CHECK(*string, -ENOMEM);
#endif
    }

    return 0;
}

static int
set_string_separator(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
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
string_split(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
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
#ifdef HAVE_ICU
    int32_t (*case_func)(UChar *dest, int32_t destCapacity, const UChar *src,
        int32_t srcLength, const char *locale,
        UErrorCode *pErrorCode) = lower ? u_strToLower : u_strToUpper;
    UChar *u_orig = NULL, *u_lower = NULL;
    const char *curr_locale = NULL;
    const char *value = NULL;
    int32_t u_changed_sz;
    char *final = NULL;
    UErrorCode err;
    int32_t sz;
    int r;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    ICU_STR_FROM_UTF8_GOTO(u_orig, sz, err, value, -EINVAL, -ENOMEM,
        fail_from_utf8);

#ifdef HAVE_LOCALE
    curr_locale = setlocale(LC_ALL, NULL);
    if (curr_locale == NULL) {
        curr_locale = "";
    }
#else
    curr_locale = "";
#endif

    err = U_ZERO_ERROR;
    u_changed_sz = case_func(NULL, 0, u_orig, sz, curr_locale, &err);
    if (U_FAILURE(err) && err != U_BUFFER_OVERFLOW_ERROR) {
        errno = EINVAL;
        goto fail_from_utf8;
    }
    u_lower = calloc(u_changed_sz + 1, sizeof(*u_lower));
    if (!u_lower) {
        errno = ENOMEM;
        goto fail_from_utf8;
    }

    err = U_ZERO_ERROR;
    case_func(u_lower, u_changed_sz + 1, u_orig, sz, curr_locale, &err);
    free(u_orig);
    if (U_FAILURE(err) || u_lower[u_changed_sz] != 0) {
        errno = EINVAL;
        goto fail_case_func;
    }

    UTF8_FROM_ICU_STR_ERRNO_GOTO(final, sz, err, u_lower, u_changed_sz,
        EINVAL, fail_case_func, ENOMEM, fail_case_func, EINVAL, fail_to_utf8);

    r = sol_flow_send_string_packet(node,
        lower ? SOL_FLOW_NODE_TYPE_STRING_LOWERCASE__OUT__OUT :
        SOL_FLOW_NODE_TYPE_STRING_UPPERCASE__OUT__OUT, final);
    free(final);

    return r;

fail_to_utf8:
    free(final);
fail_case_func:
    free(u_lower);
fail_from_utf8:
    free(u_orig);
    sol_flow_send_error_packet(node, -errno, u_errorName(err));
    return -errno;
#else
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
#endif
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
