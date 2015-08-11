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

/* Copyright (c) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009,
 * 2010, 2011, 2012, 2013, 2014, 2015 Python Software Foundation; All
 * Rights Reserved. This file has code excerpts (string formatting
 * module) extracted from cpython project
 * (https://hg.python.org/cpython/), that comes under the PSFL
 * license. The string formatting code was adapted here to Soletta
 * data types. The entire text for that license is present in this
 * directory */

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>

#include "string-format.h"

#define SF_MIN(x, y) (((x) > (y)) ? (y) : (x))
#define SF_MAX(x, y) (((x) > (y)) ? (x) : (y))

struct format_spec_data {
    ssize_t width;
    ssize_t precision;
    char align;
    char fill_char;
    char sign;
    char type;
    bool alternate : 1;
    bool thousands_separators : 1;
};

static inline bool
is_alignment_token(char c)
{
    switch (c) {
    case '<': case '>': case '=': case '^':
        return true;
    default:
        return false;
    }
}

static inline bool
is_sign_element(char c)
{
    switch (c) {
    case ' ': case '+': case '-':
        return true;
    default:
        return false;
    }
}

static inline int
get_digital_val(const char c)
{
    if (c < '0' || c > '9')
        return -EINVAL;

    return c - '0';
}

/*
 *  Consumes 0 or more decimal digit characters from an input string,
 *  updates *result with the corresponding positive integer, and
 *  returns the number of digits consumed.
 *
 *  Returns negative code on error.
 */
static int
get_integer(struct string_converter *mdata,
    struct sol_str_slice *str,
    size_t *pos,
    size_t end,
    ssize_t *result)
{
    ssize_t accumulator, digitval;
    int numdigits;

    accumulator = numdigits = 0;
    for (; *pos < end; (*pos)++, numdigits++) {

        digitval = get_digital_val(str->data[*pos]);
        if (digitval < 0)
            break;
        /*
         * Detect possible overflow before it happens:
         *
         * accumulator * 10 + digitval > SSIZE_MAX if and only if
         * accumulator > (SSIZE_MAX - digitval) / 10.
         */
        if (accumulator > (SSIZE_MAX - digitval) / 10) {
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "Too many decimal digits in format string");
            return -EOVERFLOW;
        }
        accumulator = accumulator * 10 + digitval;
    }
    *result = accumulator;

    return numdigits;
}

/*
 * start points to the start of the format_spec, end points just past
 * its end. fills in format with the parsed information. returns 1 on
 * success, 0 on failure. if failure, sets the exception
 */
static int
parse_internal_render_format_spec(struct string_converter *mdata,
    struct sol_str_slice *format_spec,
    struct format_spec_data *format,
    char default_type,
    char default_align)
{
    int consumed;
    size_t pos = 0, end = format_spec->len;
    bool align_specified = 0;
    bool fill_char_specified = 0;

    /* end - pos is used throughout this code to specify the length of
       the input string */

    format->fill_char = ' ';
    format->align = default_align;
    format->alternate = false;
    format->sign = '\0';
    format->width = -1;
    format->thousands_separators = false;
    format->precision = -1;
    format->type = default_type;

    /* If the second char is an alignment token,
       then parse the fill char */
    if (end - pos >= 2 && is_alignment_token(format_spec->data[pos + 1])) {
        format->align = format_spec->data[pos + 1];
        format->fill_char = format_spec->data[pos];
        fill_char_specified = true;
        align_specified = true;
        pos += 2;
    } else if (end - pos >= 1 && is_alignment_token(format_spec->data[pos])) {
        format->align = format_spec->data[pos];
        align_specified = true;
        ++pos;
    }

    /* Parse the various sign options */
    if (end - pos >= 1 && is_sign_element(format_spec->data[pos])) {
        format->sign = format_spec->data[pos];
        ++pos;
    }

    /* If the next character is #, we're in alternate mode.  This only
       applies to integers. */
    if (end - pos >= 1 && format_spec->data[pos] == '#') {
        format->alternate = true;
        ++pos;
    }

    /* The special case for 0-padding (backwards compat) */
    if (!fill_char_specified && end - pos >= 1
        && format_spec->data[pos] == '0') {
        format->fill_char = '0';
        if (!align_specified) {
            format->align = '=';
        }
        ++pos;
    }

    consumed = get_integer(mdata, format_spec, &pos, end, &format->width);
    SOL_INT_CHECK(consumed, < 0, consumed);

    /* If consumed is 0, we didn't consume any characters for the
       width. In that case, reset the width to -1, because
       get_integer() will have set it to zero. -1 is how we record
       that the width wasn't specified. */
    if (consumed == 0)
        format->width = -1;

    /* Comma signifies add thousands separators */
    if (end - pos && format_spec->data[pos] == ',') {
        format->thousands_separators = true;
        ++pos;
    }

    /* Parse field precision */
    if (end - pos && format_spec->data[pos] == '.') {
        ++pos;

        consumed = get_integer(mdata, format_spec,
            &pos, end, &format->precision);
        SOL_INT_CHECK(consumed, < 0, consumed);

        /* Not having a precision after a dot is an error. */
        if (consumed == 0) {
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "Format specifier missing precision");
            return -EINVAL;
        }
    }

    /* Finally, parse the type field. */

    if (end - pos > 1) {
        /* More than one char remain, invalid format specifier. */
        sol_flow_send_error_packet(mdata->node, -EINVAL,
            "Invalid format specifier");
        return -EINVAL;
    }

    if (end - pos == 1) {
        format->type = format_spec->data[pos];
        ++pos;
    }

    /* Do as much validating as we can, just by looking at the format
       specifier. Do not take into account what type of formatting
       we're doing (int, float, string). */
    if (format->thousands_separators) {
        switch (format->type) {
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'E':
        case 'G':
        case '%':
        case 'F':
        case '\0':
            /* These are allowed. See PEP 378.*/
            break;
        default:
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "Cannot specify ',' with '%c'.", format->type);
            return -EINVAL;
        }
    }

    return 0;
}

/* describes the layout for an integer, see the comment in
   calc_number_widths() for details */
struct number_field_widths {
    ssize_t n_lpadding;
    ssize_t n_prefix;
    ssize_t n_spadding;
    ssize_t n_rpadding;
    ssize_t n_sign; /* number of digits needed for sign (0/1) */
    ssize_t n_grouped_digits; /* Space taken up by the digits,
                               * including any grouping chars. */
    ssize_t n_decimal; /* 0 if only an integer */
    ssize_t n_remainder; /* Digits in decimal and/or exponent part,
                            excluding the decimal itself, if
                            present. */

    /* These 2 are not the widths of fields, but are needed by
       STRINGLIB_GROUPING. */

    ssize_t n_digits; /* The number of digits before a decimal or
                       * exponent. */
    ssize_t n_min_width; /* The min_width we used when we computed the
                          * n_grouped_digits width. */

    char sign;
};

/* Locale info needed for formatting integers and the part of floats
 * before and including the decimal.
 */
struct locale_info {
    const char *decimal_point;
    const char *thousands_sep;
    const char *grouping;
};

#define STATIC_LOCALE_INFO_INIT { 0, 0, 0 }

static const char DECIMAL_POINT_DEFAULT[] = ".";
static const char THOUSANDS_SEP_DEFAULT[] = ",";
static const char NO_GROUPING[1] = { CHAR_MAX };

#define LT_CURRENT_LOCALE 0
#define LT_DEFAULT_LOCALE 1
#define LT_NO_LOCALE 2

/* Find the decimal point character(s?), thousands_separator(s?), and
 * grouping description, either for the current locale, if type is
 * LT_CURRENT_LOCALE, a hard-coded locale, if LT_DEFAULT_LOCALE, or
 * none, if LT_NO_LOCALE.
 */
static int
get_locale_info(int type, struct locale_info *locale_info)
{
    switch (type) {
    case LT_CURRENT_LOCALE: {
#ifdef HAVE_LOCALE
        struct lconv *locale_data = localeconv();
        locale_info->decimal_point = locale_data->decimal_point;
        if (locale_info->decimal_point == NULL)
            return -EINVAL;
        locale_info->thousands_sep = locale_data->thousands_sep;
        if (locale_info->thousands_sep == NULL)
            return -EINVAL;
        locale_info->grouping = locale_data->grouping;
        break;
#endif //slip to default case if no locale support
    }
    case LT_DEFAULT_LOCALE:
        locale_info->decimal_point = DECIMAL_POINT_DEFAULT;
        locale_info->thousands_sep = THOUSANDS_SEP_DEFAULT;
        locale_info->grouping = "\3"; /* Group every 3 characters. The
                                         (implicit) trailing 0 means
                                         repeat infinitely. */
        break;
    case LT_NO_LOCALE:
        locale_info->decimal_point = DECIMAL_POINT_DEFAULT;
        locale_info->thousands_sep = "";
        if (!locale_info->decimal_point) {
            return -EINVAL;
        }
        locale_info->grouping = NO_GROUPING;
        break;
    default:
        assert(0);
    }
    return 0;
}

static int
int32_to_char(int32_t input,
    char **p_output)
{
    int r = asprintf(p_output, "%c", input);

    SOL_INT_CHECK(r, < 0, -EINVAL);

    return 0;
}

static int
int32_to_decimal_string(int32_t input,
    char **p_output)
{
    int r = asprintf(p_output, "%" PRId32, input);

    SOL_INT_CHECK(r, < 0, -EINVAL);

    return 0;
}

static const char *
byte_to_binary(int32_t input)
{
    static char buffer[sizeof(int32_t) * 8 + 1] = { 0 };
    char *ret = NULL;
    unsigned i;
    size_t z;

    for (z = 1UL << (sizeof(int32_t) * 8 - 1), i = 0; z > 0; z >>= 1, i++) {
        buffer[i] = (((input & z) == z) ? '1' : '0');

        /* we don't want the leading zeros at this point */
        if (i && !ret && buffer[i - 1] == '0' && buffer[i] != '0') {
            ret = &buffer[i];
        }
    }

    if (!ret)
        ret = &buffer[i - 1];

    buffer[i] = 0;

    return ret;
}

/* Convert an integer to a string, using a given conversion base,
 * which should be one of 2, 8 or 16. If base is 2, 8 or 16, add the
 * proper prefix '0b', '0o' or '0x' if alternate is nonzero.
 */
static int
int32_to_binary_string(int32_t input,
    int base,
    bool alternate,
    char **p_output)
{
    bool negative = input < 0;
    const char *to_bin = NULL;
    char base_prefix[] = "0 ";
    char sign[] = "-";
    int r;

    if (negative)
        input *= -1;

    assert(base == 2 || base == 8 || base == 16);

    if (alternate) {
        if (base == 16)
            base_prefix[1] = 'x';
        else if (base == 8)
            base_prefix[1] = 'o';
        else /* (base == 2) */
            base_prefix[1] = 'b';
    } else
        base_prefix[0] = '\0';

    if (!negative)
        sign[0] = '\0';

#define FORMAT_EVAL(_fmt, _arg) \
    do { \
        r = asprintf(p_output, _fmt, sign, base_prefix, _arg); \
        SOL_INT_CHECK(r, < 0, -EINVAL); \
    } while (0)

    switch (base) {
    case 16:
        FORMAT_EVAL("%s%s%x", input);
        break;
    case 8:
        FORMAT_EVAL("%s%s%o", input);
        break;
    case 2:
        to_bin = byte_to_binary(input);
        SOL_NULL_CHECK(to_bin, -EINVAL);
        FORMAT_EVAL("%s%s%s", to_bin);
        break;
    default:
        assert(0); /* shouldn't ever get here */
    }
#undef FORMAT_EVAL

    return 0;
}

static int
int32_format_base(int32_t in_value, int base, int alternate, char **out_value)
{
    int r;

    if (base == 10)
        r = int32_to_decimal_string(in_value, out_value);
    else
        r = int32_to_binary_string(in_value, base, alternate, out_value);

    return r;
}

static void
fast_fill(char *buffer,
    ssize_t start,
    ssize_t length,
    unsigned char fill_char)
{
    assert(start >= 0);
    memset(buffer + start, fill_char, length);
}

static void
write(char **data, ssize_t index, unsigned char value)
{
    (*data)[index] = value;
}

static int
copy_characters(char **to,
    size_t to_start,
    const char *from,
    size_t from_start,
    size_t how_many)
{
    if (how_many == 0)
        return 0;

    memcpy(*to + to_start, from + from_start, how_many);

    return 0;
}

struct group_generator {
    const char *grouping;
    char previous;
    ssize_t i; /* Where we're currently pointing in grouping. */
};

static void
group_generator_init(struct group_generator *self, const char *grouping)
{
    self->grouping = grouping;
    self->i = 0;
    self->previous = 0;
}

/* Returns the next grouping, or 0 to signify end. */
static ssize_t
group_generator_next(struct group_generator *self)
{
    /* Note that we don't really do much error checking here. If a
       grouping string contains just CHAR_MAX, for example, then just
       terminate the generator. That shouldn't happen, but at least we
       fail gracefully. */
    switch (self->grouping[self->i]) {
    case 0:
        return self->previous;
    case CHAR_MAX:
        /* Stop the generator. */
        return 0;
    default: {
        char ch = self->grouping[self->i];
        self->previous = ch;
        self->i++;
        return (ssize_t)ch;
    }
    }
}

/* Fill in some digits, leading zeros, and thousands separator. All
   are optional, depending on when we're called. */
static void
ascii_fill(char **digits_end, char **buffer_end,
    size_t n_chars, size_t n_zeros, const char *thousands_sep,
    size_t thousands_sep_len)
{
    size_t i;

    if (thousands_sep) {
        *buffer_end -= thousands_sep_len;

        /* Copy the thousands_sep chars into the buffer. */
        memcpy(*buffer_end, thousands_sep,
            thousands_sep_len * sizeof(unsigned char));
    }

    *buffer_end -= n_chars;
    *digits_end -= n_chars;
    memcpy(*buffer_end, *digits_end, n_chars * sizeof(unsigned char));

    *buffer_end -= n_zeros;
    for (i = 0; i < n_zeros; i++)
        (*buffer_end)[i] = '0';
}

/*
 * @buffer: A pointer to the start of a string.
 * @n_buffer: Number of characters in @buffer.
 * @digits: A pointer to the digits we're reading from. If count
 *          is non-NULL, this is unused.
 * @n_digits: The number of digits in the string, in which we want
 *            to put the grouping chars.
 * @min_width: The minimum width of the digits in the output string.
 *             Output will be zero-padded on the left to fill.
 * @grouping: see definition in localeconv().
 * @thousands_sep: see definition in localeconv().
 *
 * There are 2 modes: counting and filling. If @buffer is NULL, we are
 * in counting mode, else filling mode. If counting, the required
 * buffer size is returned. If filling, we know the buffer will be
 * large enough, so we don't need to pass in the buffer size. Inserts
 * thousand grouping characters (as defined by grouping and
 * thousands_sep) into the string between buffer and buffer+n_digits.
 *
 * Return value: 0 on error, else 1. Note that no error can occur if
 * count is non-NULL.
 */
static ssize_t
insert_thousands_grouping(
    char **buffer,
    ssize_t n_buffer,
    char *digits,
    ssize_t n_digits,
    ssize_t min_width,
    const char *grouping,
    const char *thousands_sep)
{
    ssize_t count = 0;
    ssize_t n_zeros;
    int loop_broken = 0;
    int use_separator = 0; /* First time through, don't append the
                              separator. They only go between
                              groups. */
    char *buffer_end = NULL;
    char *digits_end = NULL;
    ssize_t l;
    ssize_t n_chars;
    ssize_t remaining = n_digits; /* Number of chars remaining to be
                                     looked at */
    /* A generator that returns all of the grouping widths, until it
       returns 0. */
    struct group_generator groupgen;

    size_t thousands_sep_len = strlen(thousands_sep);

    group_generator_init(&groupgen, grouping);

    if (buffer && *buffer) {
        buffer_end = *buffer + n_buffer;
        digits_end = digits + n_digits;
    }

    while ((l = group_generator_next(&groupgen)) > 0) {
        l = SF_MIN(l, SF_MAX(SF_MAX(remaining, min_width), 1));
        n_zeros = SF_MAX(0, l - remaining);
        n_chars = SF_MAX(0, SF_MIN(remaining, l));

        /* Use n_zero zero's and n_chars chars */
        /* Count only, don't do anything. */
        count += (use_separator ? thousands_sep_len : 0) + n_zeros + n_chars;

        if (buffer && *buffer) {
            /* Copy into the output buffer. */
            ascii_fill(&digits_end, &buffer_end, n_chars, n_zeros,
                use_separator ? thousands_sep : NULL, thousands_sep_len);
        }

        /* Use a separator next time. */
        use_separator = 1;

        remaining -= n_chars;
        min_width -= l;

        if (remaining <= 0 && min_width <= 0) {
            loop_broken = 1;
            break;
        }
        min_width -= thousands_sep_len;
    }
    if (!loop_broken) {
        /* We left the loop without using a break statement. */

        l = SF_MAX(SF_MAX(remaining, min_width), 1);
        n_zeros = SF_MAX(0, l - remaining);
        n_chars = SF_MAX(0, SF_MIN(remaining, l));

        /* Use n_zero zero's and n_chars chars */
        count += (use_separator ? thousands_sep_len : 0) + n_zeros + n_chars;
        if (buffer && *buffer) {
            /* Copy into the output buffer. */
            ascii_fill(&digits_end, &buffer_end, n_chars, n_zeros,
                use_separator ? thousands_sep : NULL, thousands_sep_len);
        }
    }
    return count;
}

/* Fill in the digit parts of a numbers's string representation,
   as determined in calc_number_widths().
   Returns negative code on error, or 0 on success. */
static int
fill_number(char **out_value, const struct number_field_widths *spec,
    char *digits, ssize_t d_start, ssize_t d_end,
    char *prefix, ssize_t p_start, int fill_char,
    struct locale_info *locale, bool to_upper)
{
    /* Used to keep track of digits, decimal, and remainder. */
    ssize_t d_pos = d_start;
    ssize_t r, pos = 0;

    if (spec->n_lpadding) {
        fast_fill(*out_value, pos, spec->n_lpadding, fill_char);
        pos += spec->n_lpadding;
    }
    if (spec->n_sign == 1) {
        write(out_value, pos, spec->sign);
        pos++;
    }
    if (spec->n_prefix) {
        copy_characters(out_value, pos, prefix, p_start, spec->n_prefix);
        if (to_upper) {
            ssize_t t;
            for (t = 0; t < spec->n_prefix; t++) {
                char c = (*out_value)[pos + t];
                c = toupper(c);
                write(out_value, pos + t, c);
            }
        }
        pos += spec->n_prefix;
    }
    if (spec->n_spadding) {
        fast_fill(*out_value, pos, spec->n_spadding, fill_char);
        pos += spec->n_spadding;
    }

    /* Only for type 'c' special case, it has no digits. */
    if (spec->n_digits != 0) {
        /* Fill the digits with insert_thousands_grouping(). */
        char *pvalue = *out_value + pos;

        r = insert_thousands_grouping(
            &pvalue,
            spec->n_grouped_digits,
            digits + d_pos,
            spec->n_digits,
            spec->n_min_width,
            locale->grouping,
            locale->thousands_sep);
        if (r == -1)
            return -1;
        assert(r == spec->n_grouped_digits);
        d_pos += spec->n_digits;
    }
    if (to_upper) {
        ssize_t t;
        for (t = 0; t < spec->n_grouped_digits; t++) {
            unsigned char c = (*out_value)[pos + t];
            c = toupper(c);
            write(out_value, pos + t, c);
        }
    }
    pos += spec->n_grouped_digits;

    if (spec->n_decimal) {
        copy_characters(out_value, pos,
            locale->decimal_point, 0, spec->n_decimal);
        pos += spec->n_decimal;
        d_pos += 1;
    }

    if (spec->n_remainder) {
        copy_characters(out_value, pos, digits, d_pos, spec->n_remainder);
        pos += spec->n_remainder;
    }

    if (spec->n_rpadding) {
        fast_fill(*out_value, pos, spec->n_rpadding, fill_char);
        pos += spec->n_rpadding;
    }
    return 0;
}

static ssize_t
calc_number_widths(struct number_field_widths *spec,
    ssize_t n_prefix,
    char sign_char,
    char *number,
    ssize_t n_start,
    ssize_t n_end,
    ssize_t n_remainder,
    int has_decimal,
    const struct locale_info *locale,
    const struct format_spec_data *format)
{
    ssize_t n_non_digit_non_padding;
    ssize_t n_padding;

    spec->n_digits = n_end - n_start - n_remainder - (has_decimal ? 1 : 0);
    spec->n_lpadding = 0;
    spec->n_prefix = n_prefix;
    spec->n_decimal = has_decimal ? strlen(locale->decimal_point) : 0;
    spec->n_remainder = n_remainder;
    spec->n_spadding = 0;
    spec->n_rpadding = 0;
    spec->sign = '\0';
    spec->n_sign = 0;

    /* the output will look like:
     * |                                                                      |
     * | <lpad><sign><prefix><spad><grouped_digits><decimal><remainder><rpad> |
     * |                                                                      |
     *
     * sign is computed from format->sign and the actual sign of the
     * number
     *
     * prefix is given (it's for the '0x' prefix)
     *
     * digits is already known
     *
     * the total width is either given, or computed from the actual
     * digits
     *
     * only one of lpadding, spadding, and rpadding can be non-zero,
     * and it's calculated from the width and other fields
     */

    /* compute the various parts we're going to write */
    switch (format->sign) {
    case '+':
        /* always put a + or - */
        spec->n_sign = 1;
        spec->sign = (sign_char == '-' ? '-' : '+');
        break;
    case ' ':
        spec->n_sign = 1;
        spec->sign = (sign_char == '-' ? '-' : ' ');
        break;
    default:
        /* Not specified, or the default (-) */
        if (sign_char == '-') {
            spec->n_sign = 1;
            spec->sign = '-';
        }
    }

    /* The number of chars used for non-digits and non-padding. */
    n_non_digit_non_padding = spec->n_sign + spec->n_prefix + spec->n_decimal +
        spec->n_remainder;

    /* min_width can go negative, that's okay. format->width == -1 means
       we don't care. */
    if (format->fill_char == '0' && format->align == '=')
        spec->n_min_width = format->width - n_non_digit_non_padding;
    else
        spec->n_min_width = 0;

    if (spec->n_digits == 0)
        /* This case only occurs when using 'c' formatting, we need to
           special case it because the grouping code always wants to
           have at least one character. */
        spec->n_grouped_digits = 0;
    else {
        spec->n_grouped_digits = insert_thousands_grouping(
            NULL, 0, NULL, spec->n_digits, spec->n_min_width,
            locale->grouping, locale->thousands_sep);
    }

    /* Given the desired width and the total of digit and non-digit
       space we consume, see if we need any padding. format->width can
       be negative (meaning no padding), but this code still works in
       that case. */
    n_padding = format->width -
        (n_non_digit_non_padding + spec->n_grouped_digits);
    if (n_padding > 0) {
        /* Some padding is needed. Determine if it's left, space, or right. */
        switch (format->align) {
        case '<':
            spec->n_rpadding = n_padding;
            break;
        case '^':
            spec->n_lpadding = n_padding / 2;
            spec->n_rpadding = n_padding - spec->n_lpadding;
            break;
        case '=':
            spec->n_spadding = n_padding;
            break;
        case '>':
            spec->n_lpadding = n_padding;
            break;
        default:
            /* Shouldn't get here, but treat it as '>' */
            spec->n_lpadding = n_padding;
            assert(0);
            break;
        }
    }

    return spec->n_lpadding + spec->n_sign + spec->n_prefix +
           spec->n_spadding + spec->n_grouped_digits + spec->n_decimal +
           spec->n_remainder + spec->n_rpadding;
}

static int
int32_format_do(struct string_converter *mdata,
    int32_t in_value,
    const struct format_spec_data *format,
    char **out_value)
{
    int result = -EINVAL;
    char *tmp = NULL;
    ssize_t inumeric_chars;
    char sign_char = '\0';
    ssize_t n_digits;
    ssize_t n_remainder = 0; /* Used only for 'c' formatting, which
                              * produces non-digits */
    ssize_t n_prefix = 0; /* Count of prefix chars, (e.g., '0x') */
    ssize_t n_total;
    ssize_t prefix = 0;
    struct number_field_widths spec;

    /* Locale settings, either from the actual locale or
       from a hard-coded pseudo-locale */
    struct locale_info locale = STATIC_LOCALE_INFO_INIT;

    /* no precision allowed on integers */
    if (format->precision != -1) {
        sol_flow_send_error_packet(mdata->node, -EINVAL,
            "Precision not allowed in integer format specifier");
        goto done;
    }

    /* special case for character formatting */
    if (format->type == 'c') {
        /* error to specify a sign */
        if (format->sign != '\0') {
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "Sign not allowed with integer format specifier 'c'");
            goto done;
        }
        /* error to request alternate format */
        if (format->alternate) {
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "Alternate form (#) not allowed with integer"
                " format specifier 'c'");
            goto done;
        }

        /* Integer input truncated to a character */
        if (in_value < 0 || in_value > 0x10ffff) {
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "%%c arg not in range(0x110000)");
            goto done;
        }
        result = int32_to_char(in_value, &tmp);
        SOL_INT_CHECK_GOTO(result, < 0, done);

        inumeric_chars = 0;
        n_digits = 1;

        /* As a sort-of hack, we tell calc_number_widths() that we
           only have "remainder" characters and it thinks these are
           characters that don't get formatted, only copied into the
           output string. We do this for 'c' formatting, because the
           characters are likely to be non-digits. */
        n_remainder = 1;
    } else {
        int base;
        int leading_chars_to_skip = 0;  /* Number of characters added
                                           that we want to skip
                                           over. */

        /* Compute the base and how many characters will be added */
        switch (format->type) {
        case 'b':
            base = 2;
            leading_chars_to_skip = 2; /* 0b */
            break;
        case 'o':
            base = 8;
            leading_chars_to_skip = 2; /* 0o */
            break;
        case 'x':
        case 'X':
            base = 16;
            leading_chars_to_skip = 2; /* 0x */
            break;
        default:  /* shouldn't be needed, but stops a compiler warning */
        case 'd':
        case 'n':
            base = 10;
            break;
        }

        if (format->sign != '+' && format->sign != ' '
            && format->width == -1
            && format->type != 'X' && format->type != 'n'
            && !format->thousands_separators) {

            /* Fast path */
            return int32_format_base(in_value, base, format->alternate,
                out_value);
        }

        /* The number of prefix chars is the same as the leading
           chars to skip */
        if (format->alternate)
            n_prefix = leading_chars_to_skip;

        /* Do the hard part, converting to a string in a given base */
        result = int32_format_base(in_value, base, true, &tmp);
        SOL_INT_CHECK_GOTO(result, < 0, done);

        inumeric_chars = 0;
        n_digits = strlen(tmp);

        prefix = inumeric_chars;

        /* Is a sign character present in the output? If so, remember it
           and skip it */
        if (tmp[0] == '-') {
            sign_char = '-';
            ++prefix;
            ++leading_chars_to_skip;
        }

        /* Skip over the leading chars (0x, 0b, etc.) */
        n_digits -= leading_chars_to_skip;
        inumeric_chars += leading_chars_to_skip;
    }

    /* Determine the grouping, separator, and decimal point, if any. */
    if (get_locale_info(format->type == 'n' ? LT_CURRENT_LOCALE :
        (format->thousands_separators ? LT_DEFAULT_LOCALE : LT_NO_LOCALE),
        &locale) == -1)
        goto done;

    /* Calculate how much memory we'll need. */
    n_total = calc_number_widths(&spec, n_prefix, sign_char, tmp,
        inumeric_chars, inumeric_chars + n_digits, n_remainder, 0,
        &locale, format);

    *out_value = calloc(n_total + 1, sizeof(char));
    SOL_NULL_CHECK_GOTO(out_value, done);

    /* Populate the memory. */
    result = fill_number(out_value, &spec,
        tmp, inumeric_chars, inumeric_chars + n_digits,
        tmp, prefix, format->fill_char, &locale, format->type == 'X');

done:
    free(tmp);

    return result;
}

/* double_to_string's "flags" parameter can be set to 0 or more of: */
#define DTSF_SIGN      0x01 /* always add the sign */
#define DTSF_ADD_DOT_0 0x02 /* if the result is an integer add ".0" */
#define DTSF_ALT       0x04 /* "alternate" formatting. it's
                             * format_code specific */

/* double_to_string's "type", if non-NULL, will be set to one of: */
#define DTST_FINITE 0
#define DTST_INFINITE 1
#define DTST_NAN 2

/* Given a string that may have a decimal point in the current locale,
 * change it back to a dot. Since the string cannot get longer, no
 * need for a maximum buffer size parameter.
 */
static inline void
change_decimal_from_locale_to_dot(char *buffer)
{
#ifdef HAVE_LOCALE
    struct lconv *locale_data = localeconv();
    const char *decimal_point = locale_data->decimal_point;
#else
    const char *decimal_point = DECIMAL_POINT_DEFAULT;
#endif

    if (decimal_point[0] != '.' || decimal_point[1] != 0) {
        size_t decimal_point_len = strlen(decimal_point);

        if (*buffer == '+' || *buffer == '-')
            buffer++;
        while (isdigit(*buffer))
            buffer++;
        if (strncmp(buffer, decimal_point, decimal_point_len) == 0) {
            *buffer = '.';
            buffer++;
            if (decimal_point_len > 1) {
                /* buffer needs to get smaller */
                size_t rest_len = strlen(buffer + (decimal_point_len - 1));
                memmove(buffer, buffer + (decimal_point_len - 1), rest_len);
                buffer[rest_len] = 0;
            }
        }
    }
}

/* From the C99 standard, section 7.19.6: The exponent always contains
 * at least two digits, and only as many more digits as necessary to
 * represent the exponent.
 */
#define MIN_EXPONENT_DIGITS 2

/* Ensure that any exponent, if present, is at least
   MIN_EXPONENT_DIGITS in length. */
static inline void
ensure_minimum_exponent_length(char *buffer, size_t buf_size)
{
    char *p = strpbrk(buffer, "eE");

    if (p && (*(p + 1) == '-' || *(p + 1) == '+')) {
        char *start = p + 2;
        int exponent_digit_cnt = 0;
        int leading_zero_cnt = 0;
        int in_leading_zeros = 1;
        int significant_digit_cnt;

        /* Skip over the exponent and the sign. */
        p += 2;

        /* Find the end of the exponent, keeping track of leading
           zeros. */
        while (*p && isdigit(*p)) {
            if (in_leading_zeros && *p == '0')
                ++leading_zero_cnt;
            if (*p != '0')
                in_leading_zeros = 0;
            ++p;
            ++exponent_digit_cnt;
        }

        significant_digit_cnt = exponent_digit_cnt - leading_zero_cnt;
        if (exponent_digit_cnt == MIN_EXPONENT_DIGITS) {
            /* If there are 2 exactly digits, we're done,
               regardless of what they contain */
        } else if (exponent_digit_cnt > MIN_EXPONENT_DIGITS) {
            int extra_zeros_cnt;

            /* There are more than 2 digits in the exponent.  See
               if we can delete some of the leading zeros */
            if (significant_digit_cnt < MIN_EXPONENT_DIGITS)
                significant_digit_cnt = MIN_EXPONENT_DIGITS;
            extra_zeros_cnt = exponent_digit_cnt - significant_digit_cnt;

            /* Delete extra_zeros_cnt worth of characters from the
               front of the exponent */
            assert(extra_zeros_cnt >= 0);

            /* Add one to significant_digit_cnt to copy the
               trailing 0 byte, thus setting the length */
            memmove(start, start + extra_zeros_cnt, significant_digit_cnt + 1);
        } else {
            /* If there are fewer than 2 digits, add zeros until there
               are 2, if there's enough room */
            int zeros = MIN_EXPONENT_DIGITS - exponent_digit_cnt;
            if (start + zeros + exponent_digit_cnt + 1 < buffer + buf_size) {
                memmove(start + zeros, start, exponent_digit_cnt + 1);
                memset(start, '0', zeros);
            }
        }
    }
}

/* Remove trailing zeros after the decimal point from a numeric
 * string; also remove the decimal point if all digits following it
 * are zero. The numeric string must end in '\0', and should not have
 * any leading or trailing whitespace. Assumes that the decimal point
 * is '.'.
 */
static inline void
remove_trailing_zeros(char *buffer)
{
    char *old_fraction_end, *new_fraction_end, *end, *p;

    p = buffer;
    if (*p == '-' || *p == '+')
        /* Skip leading sign, if present */
        ++p;
    while (isdigit(*p))
        ++p;

    /* if there's no decimal point there's nothing to do */
    if (*p++ != '.')
        return;

    /* scan any digits after the point */
    while (isdigit(*p))
        ++p;
    old_fraction_end = p;

    /* scan up to ending '\0' */
    while (*p != '\0')
        p++;
    /* +1 to make sure that we move the null byte as well */
    end = p + 1;

    /* scan back from fraction_end, looking for removable zeros */
    p = old_fraction_end;
    while (*(p - 1) == '0')
        --p;
    /* and remove point if we've got that far */
    if (*(p - 1) == '.')
        --p;
    new_fraction_end = p;

    memmove(new_fraction_end, old_fraction_end, end - old_fraction_end);
}

/*
 * Ensure that buffer has a decimal point in it. The decimal point
 * will not be in the current locale, it will always be '.'. Don't add
 * a decimal point if an exponent is present. Also, convert to
 * exponential notation where adding a '.0' would produce too many
 * significant digits.
 *
 * Returns a pointer to the fixed buffer, or NULL on failure.
 */
static inline char *
ensure_decimal_point(char *buffer, size_t buf_size, int precision)
{
    int digit_count, insert_count = 0, convert_to_exp = 0;
    const char *chars_to_insert;
    char *digits_start;

    /* search for the first non-digit character */
    char *p = buffer;

    /* Skip leading sign, if present. I think this could only ever be
       '-', but it can't hurt to check for both. */
    if (*p == '-' || *p == '+')
        ++p;
    digits_start = p;
    while (*p && isdigit(*p))
        ++p;
    digit_count = (int)(p - digits_start);

    if (*p == '.') {
        if (isdigit(*(p + 1))) {
            /* Nothing to do, we already have a decimal point and a
               digit after it */
        } else {
            /* We have a decimal point, but no following digit. Insert
               a zero after the decimal. */
            /* can't ever get here via double_to_string */
            assert(precision == -1);
            ++p;
            chars_to_insert = "0";
            insert_count = 1;
        }
    } else if (!(*p == 'e' || *p == 'E')) {
        /* Don't add ".0" if we have an exponent. */
        if (digit_count == precision) {
            convert_to_exp = 1;
            /* no exponent, no point, and we shouldn't land here for
               infs and nans, so we must be at the end of the
               string. */
            assert(*p == '\0');
        } else {
            assert(precision == -1 || digit_count < precision);
            chars_to_insert = ".0";
            insert_count = 2;
        }
    }
    if (insert_count) {
        size_t buf_len = strlen(buffer);
        if (buf_len + insert_count + 1 >= buf_size) {
            /* If there is not enough room in the buffer for the
               additional text, just skip it. It's not worth
               generating an error over. */
        } else {
            memmove(p + insert_count, p, buffer + strlen(buffer) - p + 1);
            memcpy(p, chars_to_insert, insert_count);
        }
    }
    if (convert_to_exp) {
        int written;
        size_t buf_avail;
        p = digits_start;
        /* insert decimal point */
        assert(digit_count >= 1);
        memmove(p + 2, p + 1, digit_count); /* safe, but overwrites nul */
        p[1] = '.';
        p += digit_count + 1;
        assert(p <= buf_size + buffer);
        buf_avail = buf_size + buffer - p;
        if (buf_avail == 0)
            return NULL;
        /* Add exponent. It's okay to use lower case 'e': we only
           arrive here as a result of using the empty format code or
           repr/str builtins and those never want an upper case 'E' */
        written = snprintf(p, buf_avail, "e%+.02d", digit_count - 1);
        if (!(0 <= written && written < (int)buf_avail))
            /* output truncated, or something else bad happened */
            return NULL;
        remove_trailing_zeros(buffer);
    }
    return buffer;
}

#define FLOAT_FORMATBUFLEN 120

/*
 * ascii_format_double:
 * @buffer: A buffer to place the resulting string in
 * @buf_size: The length of the buffer.
 * @format: The printf()-style format to use for the
 *          code to use for converting.
 * @d: The double to convert
 * @precision: The precision to use when formatting.
 *
 * Converts a double to a string, using the '.' as
 * decimal point. To format the number you pass in
 * a printf()-style format string. Allowed conversion
 * specifiers are 'e', 'E', 'f', 'F', 'g', 'G', and 'Z'.
 *
 * 'Z' is the same as 'g', except it always has a decimal and at least
 * one digit after the decimal.
 *
 * Return value: 0 on success, negative error code otherwise.
 **/
static int
ascii_format_double(char **buffer,
    size_t buf_size,
    const char *format,
    double d,
    int precision)
{
    char format_char;
    size_t format_len = strlen(format);

    /* code 'Z' requires copying the format. 'Z' is 'g',
       but also with at least one character past the decimal. */
    char tmp_format[FLOAT_FORMATBUFLEN];

    /* The last character in the format string must be the format char */
    format_char = format[format_len - 1];

    if (format[0] != '%')
        return -EINVAL;

    if (strpbrk(format + 1, "'l%"))
        return -EINVAL;

    if (!(format_char == 'e' || format_char == 'E' ||
        format_char == 'f' || format_char == 'F' ||
        format_char == 'g' || format_char == 'G' ||
        format_char == 'Z'))
        return -EINVAL;

    /* Map 'Z' format_char to 'g', by copying the format string and
       replacing the final char with a 'g' */
    if (format_char == 'Z') {
        if (format_len + 1 >= sizeof(tmp_format)) {
            /* The format won't fit in our copy. Error out. In
               practice, this will never happen and will be detected
               by returning NULL */
            return -EINVAL;
        }
        strcpy(tmp_format, format);
        tmp_format[format_len - 1] = 'g';
        format = tmp_format;
    }

    #pragma GCC diagnostic ignored "-Wformat-nonliteral"
    /* Have snprintf do the hard work */
    snprintf(*buffer, buf_size, format, d);
    #pragma GCC diagnostic warning "-Wformat-nonliteral"

    /* Do various fixups on the return string */

    /* Get the current locale, and find the decimal point string.
       Convert that string back to a dot. */
    change_decimal_from_locale_to_dot(*buffer);

    /* If an exponent exists, ensure that the exponent is at least
       MIN_EXPONENT_DIGITS digits, providing the buffer is large
       enough for the extra zeros. Also, if there are more than
       MIN_EXPONENT_DIGITS, remove as many zeros as possible until we
       get back to MIN_EXPONENT_DIGITS */
    ensure_minimum_exponent_length(*buffer, buf_size);

    /* If format_char is 'Z', make sure we have at least one character
       after the decimal point (and make sure we have a decimal
       point); also switch to exponential notation in some edge cases
       where the extra character would produce more significant digits
       that we really want. */
    if (format_char == 'Z')
        *buffer = ensure_decimal_point(*buffer, buf_size, precision);

    return 0;
}

static char *
double_to_string(double val,
    char format_code,
    int precision,
    int flags,
    int *type)
{
    char format[32];
    ssize_t bufsize;
    char *buf;
    int t, exp;
    bool upper = false;

    /* Validate format_code, and map upper and lower case */
    switch (format_code) {
    case 'e': /* exponent */
    case 'f': /* fixed */
    case 'g': /* general */
        break;
    case 'E':
        upper = true;
        format_code = 'e';
        break;
    case 'F':
        upper = true;
        format_code = 'f';
        break;
    case 'G':
        upper = true;
        format_code = 'g';
        break;
    //No 'r' case here
    default:
        return NULL;
    }

    /* Here's a quick-and-dirty calculation to figure out how big a
     * buffer we need. In general, for a finite float we need:
     *
     *   1 byte for each digit of the decimal significand, and
     *
     *   1 for a possible sign
     *   1 for a possible decimal point
     *   2 for a possible [eE][+-]
     *   1 for each digit of the exponent; if we allow 19 digits
     *     total then we're safe up to exponents of 2**63.
     *   1 for the trailing nul byte
     *
     * This gives a total of 24 + the number of digits in the
     * significand, and the number of digits in the significand is:
     *
     *   for 'g' format: at most precision, except possibly
     *     when precision == 0, when it's 1.
     *   for 'e' format: precision+1
     *   for 'f' format: precision digits after the point, at least 1
     *   before. To figure out how many digits appear before the point
     *   we have to examine the size of the number. If fabs(val) < 1.0
     *   then there will be only one digit before the point. If
     *   fabs(val) >= 1.0, then there are at most
     *
     *   1+floor(log10(ceiling(fabs(val))))
     *
     *   digits before the point (where the 'ceiling' allows for the
     *   possibility that the rounding rounds the integer part of val
     *   up). A safe upper bound for the above quantity is
     *   1+floor(exp/3), where exp is the unique integer such that 0.5
     *   <= fabs(val)/2**exp < 1.0. This exp can be obtained from
     *   frexp.
     *
     * So we allow room for precision+1 digits for all formats, plus
     * an extra floor(exp/3) digits for 'f' format.
     */

    if (fpclassify(val) == FP_NAN || fpclassify(val) == FP_INFINITE) {
        /* 3 for 'inf'/'nan', 1 for sign, 1 for '\0' */
        bufsize = 5;
    } else {
        bufsize = 25 + precision;
        if (format_code == 'f' && fabs(val) >= 1.0) {
            frexp(val, &exp);
            bufsize += exp / 3;
        }
    }

    buf = calloc(1, bufsize);
    SOL_NULL_CHECK(buf, NULL);

    /* Handle nan and inf. */
    if (fpclassify(val) == FP_NAN) {
        strcpy(buf, "nan");
        t = DTST_NAN;
    } else if (fpclassify(val) == FP_INFINITE) {
        if (sol_drange_val_equal(copysign(1., val), 1.))
            strcpy(buf, "inf");
        else
            strcpy(buf, "-inf");
        t = DTST_INFINITE;
    } else {
        t = DTST_FINITE;
        if (flags & DTSF_ADD_DOT_0)
            format_code = 'Z';

        snprintf(format, sizeof(format), "%%%s.%i%c",
            (flags & DTSF_ALT ? "#" : ""), precision, format_code);
        ascii_format_double(&buf, bufsize, format, val, precision);
    }

    /* Add sign when requested.  It's convenient (esp. when formatting
       complex numbers) to include a sign even for inf and nan. */
    if (flags & DTSF_SIGN && buf[0] != '-') {
        size_t len = strlen(buf);
        /* the bufsize calculations above should ensure that we've got
           space to add a sign */
        assert((size_t)bufsize >= len + 2);
        memmove(buf + 1, buf, len + 1);
        buf[0] = '+';
    }
    if (upper) {
        /* Convert to upper case. */
        char *p1;
        for (p1 = buf; *p1; p1++)
            *p1 = toupper(*p1);
    }

    if (type)
        *type = t;
    return buf;
}

static char *
string_alloc_init(const char *buffer, size_t size)
{
    char *out = calloc(1, size);

    SOL_NULL_CHECK(out, NULL);
    memcpy(out, buffer, size);

    return out;
}

static void
parse_number(char *s,
    ssize_t pos,
    ssize_t end,
    ssize_t *n_remainder,
    int *has_decimal)
{
    ssize_t remainder;

    while (pos < end && isdigit(s[pos]))
        ++pos;
    remainder = pos;

    /* Does remainder start with a decimal point? */
    *has_decimal = pos < end && s[remainder] == '.';

    /* Skip the decimal point. */
    if (*has_decimal)
        remainder++;

    *n_remainder = end - remainder;
}

static int
float_format_do(struct string_converter *mdata,
    double in_value,
    const struct format_spec_data *format,
    char **out_value)
{
    char *buf = NULL;
    ssize_t n_digits;
    ssize_t n_remainder;
    ssize_t n_total;
    int has_decimal;
    double val;
    int precision, default_precision = 6;
    char type = format->type;
    int add_pct = 0;
    ssize_t index;
    struct number_field_widths spec;
    int flags = 0;
    int result = -EINVAL;
    char sign_char = '\0';
    int float_type; /* Used to see if we have a nan, inf, or regular float. */
    char *tmp = NULL;

    /* Locale settings, either from the actual locale or
       from a hard-coded pseudo-locale */
    struct locale_info locale = STATIC_LOCALE_INFO_INIT;

    if (format->precision > INT32_MAX) {
        sol_flow_send_error_packet(mdata->node, -EINVAL, "precision too big");
        goto done;
    }
    precision = (int)format->precision;

    if (format->alternate)
        flags |= DTSF_ALT;

    if (type == '\0') {
        /* Omitted type specifier. Behaves in the same way as 'f' if
           no precision is given, else like 'g', but with at least one
           digit after the decimal point. */
        flags |= DTSF_ADD_DOT_0;
        type = 'f';
        default_precision = 0;
    }

    if (type == 'n')
        /* 'n' is the same as 'g', except for the locale used to
           format the result. We take care of that later. */
        type = 'g';

    val = in_value;

    if (type == '%') {
        type = 'f';
        val *= 100;
        add_pct = 1;
    }

    if (precision < 0)
        precision = default_precision;
    else if (type == 'r')
        type = 'g';

    buf = double_to_string(val, (char)type, precision, flags, &float_type);
    if (buf == NULL)
        goto done;
    n_digits = strlen(buf);

    if (add_pct) {
        /* We know that buf has a trailing zero (since we just called
           strlen() on it), and we don't use that fact any more. So we
           can just write over the trailing zero. */
        buf[n_digits] = '%';
        n_digits += 1;
    }

    if (format->sign != '+'
        && format->sign != ' '
        && format->width == -1
        && format->type != 'n'
        && !format->thousands_separators) {
        /* Fast path */
        *out_value = string_alloc_init(buf, n_digits + 1); //one more for \0
        if (!*out_value) {
            free(buf);
            return -ENOMEM;
        }
        free(buf);
        return 0;
    }

    /* Since there is no char * version of double_to_string, just use
       the 8 bit version and then convert to char *. */
    tmp = string_alloc_init(buf, n_digits);
    free(buf);
    if (tmp == NULL)
        goto done;

    /* Is a sign character present in the output? If so, remember it
       and skip it */
    index = 0;
    if (tmp[index] == '-') {
        sign_char = '-';
        ++index;
        --n_digits;
    }

    /* Determine if we have any "remainder" (after the digits, might
       include decimal or exponent or both (or neither)) */
    parse_number(tmp, index, index + n_digits, &n_remainder, &has_decimal);

    /* Determine the grouping, separator, and decimal point, if any. */
    if (get_locale_info(format->type == 'n' ? LT_CURRENT_LOCALE :
        (format->thousands_separators ? LT_DEFAULT_LOCALE : LT_NO_LOCALE),
        &locale) == -1)
        goto done;

    /* Calculate how much memory we'll need. */
    n_total = calc_number_widths(&spec, 0, sign_char, tmp, index,
        index + n_digits, n_remainder, has_decimal,
        &locale, format);

    *out_value = calloc(n_total + 1, sizeof(char));
    SOL_NULL_CHECK_GOTO(out_value, done);

    /* Populate the memory. */
    result = fill_number(out_value, &spec, tmp, index, index + n_digits,
        NULL, 0, format->fill_char, &locale, 0);

done:
    free(tmp);
    return result;
}

static void
unknown_presentation_type(struct string_converter *mdata,
    unsigned char presentation_type,
    const char *type_name)
{
    /* %c might be out-of-range, hence the two cases. */
    if (presentation_type > 32 && presentation_type < 128)
        sol_flow_send_error_packet(mdata->node, -EINVAL,
            "Unknown format code '%c' for object of type '%.200s'",
            presentation_type, type_name);
    else
        sol_flow_send_error_packet(mdata->node, -EINVAL,
            "Unknown format code '\\x%x' for object of type '%.200s'",
            presentation_type, type_name);
}

static int
float_format(struct string_converter *mdata,
    double in_value,
    struct sol_str_slice *format_spec,
    char **out_value)
{
    struct sol_str_slice *fs = format_spec;
    struct sol_str_slice def = { .data = "f", .len = 1 };
    struct format_spec_data format;
    int ret = -EINVAL;

    if (!fs)
        return -EINVAL;

    /* check for the special case of zero length format spec (as in
       "{:}"), make it equivalent to "{:f}" */
    if (!fs->len)
        fs = &def;

    /* parse the format_spec */
    ret = parse_internal_render_format_spec(mdata, fs, &format, '\0', '>');
    SOL_INT_CHECK_GOTO(ret, < 0, done);

    /* type conversion? */
    switch (format.type) {
    case '\0': /* No format code: like 'g', but with at least one decimal. */
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case 'n':
    case '%':
        /* no conversion, already a float. do the formatting */
        return float_format_do(mdata, in_value, &format, out_value);

    default:
        /* unknown */
        unknown_presentation_type(mdata, format.type, "float");
        ret = -EINVAL;
    }

done:
    return ret;
}

static int
int32_format(struct string_converter *mdata,
    int32_t in_value,
    struct sol_str_slice *format_spec,
    char **out_value)
{
    struct format_spec_data format;
    int ret = -EINVAL;

    if (!format_spec)
        return -EINVAL;

    /* check for the special case of zero length format spec (as in
       "{:}"), make it equivalent to "{:d}" */
    if (!format_spec->len)
        return int32_format_base(in_value, 10, 0, out_value);

    /* parse the format_spec */
    ret = parse_internal_render_format_spec
            (mdata, format_spec, &format, 'd', '>');
    SOL_INT_CHECK_GOTO(ret, < 0, done);

    /* type conversion? */
    switch (format.type) {
    case 'b':
    case 'c':
    case 'd':
    case 'o':
    case 'x':
    case 'X':
    case 'n':
        /* no type conversion needed, already an int. do the formatting */
        ret = int32_format_do(mdata, in_value, &format, out_value);
        break;

    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case '%':
        /* convert to float */
        ret = float_format_do(mdata, (double)in_value, &format, out_value);
        break;

    default:
        /* unknown */
        unknown_presentation_type(mdata, format.type, "integer");
        ret = -EINVAL;
    }

done:
    return ret;
}

static int
parse_field(struct string_converter *mdata,
    struct sol_str_slice *input,
    struct sol_str_slice *field_name,
    struct sol_str_slice *format_spec)
{
    /* Note this function works if the field name is zero length,
       which is good. Zero length field names are handled later */

    char c = 0;

    format_spec->len = 0;
    format_spec->data = "";

    /* Search for the field name.  it's terminated by the end of
       the string or a ':' */
    field_name->data = input->data;
    while (input->len) {
        c = input->data[0];
        input->data++;
        input->len--;

        switch (c) {
        case '{':
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "unexpected '{' in field name");
            return -EINVAL;
        case '[':
            for (; input->len; input->len--, input->data++)
                if (input->data[0] == ']')
                    break;
            continue;
        case '}':
        case ':':
            break;
        default:
            continue;
        }
        break;
    }

    field_name->len = input->data - field_name->data - 1;

    if (c == ':') {
        /* we have a format specifier */
        /* don't include the last character */

        format_spec->data = input->data;
        while (input->len) {
            c = input->data[0];
            input->data++;
            input->len--;

            switch (c) {
            case '}':
                format_spec->len = input->data - format_spec->data - 1;
                return 0;
            default:
                break;
            }
        }

        sol_flow_send_error_packet(mdata->node, -EINVAL,
            "unmatched '{' in format spec");
        return -EINVAL;
    } else if (c != '}') {
        sol_flow_send_error_packet(mdata->node, -EINVAL,
            "expected '}' before end of string");
        return -EINVAL;
    }

    return 0;
}

/* returns a negative code on error, 0 on non-error termination, and 1
   if it got a string (or something to be expanded) */
static int
markup_iterator_next(struct string_converter *mdata,
    struct sol_str_slice *input,
    struct sol_str_slice *literal,
    struct sol_str_slice *field_name,
    struct sol_str_slice *format_spec,
    bool *field_present)
{
    int r;
    char c = 0;
    bool at_end;
    ssize_t len = 0;
    const char *start;
    bool markup_follows = false;

    literal->len = field_name->len = format_spec->len = 0;
    literal->data = field_name->data = format_spec->data = "";

    *field_present = 0;

    /* No more input, end of iterator. This is the normal exit
       path. */
    if (input->len == 0)
        return 0;

    start = input->data;

    /* First read any literal text. Read until the end of string, an
       escaped '{' or '}', or an unescaped '{'. In order to never
       allocate memory and so we can just pass pointers around, if
       there's an escaped '{' or '}' then we'll return the literal
       including the brace, but no format string. The next time
       through, we'll return the rest of the literal, skipping past
       the second consecutive brace. */
    while (input->len) {
        c = input->data[0];
        input->data++;
        input->len--;
        len++;

        switch (c) {
        case '{':
        case '}':
            markup_follows = true;
            break;
        default:
            continue;
        }
        break;
    }

    at_end = input->len == 0;

    if ((c == '}') && (at_end || (c != input->data[0]))) {
        sol_flow_send_error_packet(mdata->node, -EINVAL,
            "Single '}' encountered in format string");
        return -EINVAL;
    }
    if (at_end && c == '{') {
        sol_flow_send_error_packet(mdata->node, -EINVAL,
            "Single '{' encountered in format string");
        return -EINVAL;
    }
    if (!at_end) {
        if (c == input->data[0]) {
            /* escaped } or {, skip it in the input -> there is no
               markup object following us, just this literal text */
            input->data++;
            input->len--;
            markup_follows = false;
        } else
            len--;
    }

    /* record the literal text */
    literal->data = start;
    literal->len = len;

    if (!markup_follows)
        return 1;

    /* this is markup; parse the field */
    *field_present = true;
    r = parse_field(mdata, input, field_name, format_spec);
    SOL_INT_CHECK(r, < 0, r);

    return 1;
}

/* Return -EINVAL if an error has been detected switching between
   automatic field numbering and manual field specification, else
   return 0. */
static int
auto_number_check_error(struct string_converter *mdata,
    enum auto_number_state state,
    bool field_name_is_empty)
{
    if (state == ANS_MANUAL) {
        if (field_name_is_empty) {
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "cannot switch from manual field specification to automatic"
                " field numbering");
            return -EINVAL;
        }
    } else {
        if (!field_name_is_empty) {
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "cannot switch from automatic field numbering to manual field"
                " specification");
            return -EINVAL;
        }
    }
    return 0;
}

static int
field_name_get_integer_idx(struct string_converter *mdata,
    struct sol_str_slice *str,
    ssize_t *idx,
    struct auto_number *auto_number)
{
    int ret;
    size_t tmp = 0;
    bool field_name_is_empty;
    bool using_numeric_index;

    /* see if "str" is an integer, in which case it's used as an index
     * -- tmp is passed but not used. */
    ret = get_integer(mdata, str, &tmp, str->len, idx);
    /* no char consumed, field name case. we flag it upwards by
     * altering idx */
    if (ret == 0)
        *idx = -1;

    field_name_is_empty = str->len == 0;

    /* If the field name is omitted or if we have a numeric index
       specified, then we're doing numeric indexing into args. */
    using_numeric_index = field_name_is_empty || ret > 0;

    /* We always get here exactly one time for each field we're
       processing. And we get here in field order (counting by left
       braces). So this is the perfect place to handle automatic field
       numbering if the field name is omitted. */

    /* Check if we need to do the auto-numbering. It's not needed if
       we're called from string.Format routines, because it's handled
       in that class by itself. */
    if (auto_number) {
        /* Initialize our auto numbering state if this is the str
           time we're either auto-numbering or manually numbering. */
        if (auto_number->an_state == ANS_INIT && using_numeric_index)
            auto_number->an_state = field_name_is_empty ?
                ANS_AUTO : ANS_MANUAL;

        /* Make sure our state is consistent with what we're doing
           this time through. Only check if we're using a numeric
           index. */
        if (using_numeric_index) {
            ret = auto_number_check_error(mdata, auto_number->an_state,
                field_name_is_empty);
            SOL_INT_CHECK(ret, < 0, ret);
        }
        /* Zero length field means we want to do auto-numbering of the
           fields. */
        if (field_name_is_empty)
            *idx = (auto_number->an_field_number)++;
    }

    return 0;
}

static int32_t *
get_integer_field(struct string_converter *mdata,
    struct sol_str_slice *input,
    struct sol_irange *args,
    struct auto_number *auto_number)
{
    int r;
    ssize_t index;
    int32_t *obj = NULL;

    r = field_name_get_integer_idx(mdata, input, &index, auto_number);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    /* not an integer field, but a literal one */
    if (index == -1) {
        /* look up in val, min, max, step */
        if (sol_str_slice_str_eq(*input, "val"))
            obj = &args->val;
        else if (sol_str_slice_str_eq(*input, "min"))
            obj = &args->min;
        else if (sol_str_slice_str_eq(*input, "max"))
            obj = &args->max;
        else if (sol_str_slice_str_eq(*input, "step"))
            obj = &args->step;
        else {
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "Field %.*s does not exist for integer type",
                SOL_STR_SLICE_PRINT(*input));
            return NULL;
        }
        /* integer index */
    } else {
        switch (index) {
        case 0:
            obj = &args->val;
            break;
        case 1:
            obj = &args->min;
            break;
        case 2:
            obj = &args->max;
            break;
        case 3:
            obj = &args->step;
            break;
        default:
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "Field index %d does not exist for integer type", index);
        }
    }

end:
    return obj;
}

static double *
get_float_field(struct string_converter *mdata,
    struct sol_str_slice *input,
    struct sol_drange *args,
    struct auto_number *auto_number)
{
    int r;
    ssize_t index;
    double *obj = NULL;

    r = field_name_get_integer_idx(mdata, input, &index, auto_number);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    /* not an float field, but a literal one */
    if (index == -1) {
        /* look up in val, min, max, step */
        if (sol_str_slice_str_eq(*input, "val"))
            obj = &args->val;
        else if (sol_str_slice_str_eq(*input, "min"))
            obj = &args->min;
        else if (sol_str_slice_str_eq(*input, "max"))
            obj = &args->max;
        else if (sol_str_slice_str_eq(*input, "step"))
            obj = &args->step;
        else {
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "Field %.*s does not exist for float type",
                SOL_STR_SLICE_PRINT(*input));
            return NULL;
        }
        /* float index */
    } else {
        switch (index) {
        case 0:
            obj = &args->val;
            break;
        case 1:
            obj = &args->min;
            break;
        case 2:
            obj = &args->max;
            break;
        case 3:
            obj = &args->step;
            break;
        default:
            sol_flow_send_error_packet(mdata->node, -EINVAL,
                "Field index %d does not exist for float type", index);
        }
    }

end:
    return obj;
}

/* given:
 *
 * {field_name:format_spec}
 *
 * compute the result and write it to out_value. field_name is allowed
 * to be zero length, in which case we are doing auto field numbering.
 */
static int
output_integer_markup(struct string_converter *mdata,
    struct sol_str_slice *field_name,
    struct sol_str_slice *format_spec,
    struct sol_irange *args,
    struct auto_number *auto_number,
    char **out_value)
{
    int32_t *field_obj = NULL;
    int result = -EINVAL;

    /* convert field_name to an actual int32_t ptr */
    field_obj = get_integer_field(mdata, field_name, args, auto_number);
    if (field_obj == NULL)
        return result;

    result = int32_format(mdata, *field_obj, format_spec, out_value);
    SOL_INT_CHECK(result, < 0, result);

    return result;
}

int
do_integer_markup(struct string_converter *mdata,
    const char *format,
    struct sol_irange *args,
    struct auto_number *auto_number,
    char **out_value)
{
    struct sol_str_slice format_spec = SOL_STR_SLICE_EMPTY;
    struct sol_str_slice field_name = SOL_STR_SLICE_EMPTY;
    struct sol_str_slice literal = SOL_STR_SLICE_EMPTY;
    struct sol_str_slice iter;
    bool field_present;
    size_t size = 1; //reserve the null byte space
    int result;

    iter = SOL_STR_SLICE_STR(format, strlen(format));

    while ((result = markup_iterator_next(mdata, &iter, &literal, &field_name,
            &format_spec, &field_present)) == 1) {
        char *tmp_out = NULL, *tmp = NULL;
        int r;

        /* literal sub string in format, no markup */
        if (literal.len > 0) {
            tmp = realloc(*out_value, size + literal.len);
            if (!tmp)
                return -ENOMEM;

            *out_value = tmp;
            strncpy((*out_value) + size - 1, literal.data, literal.len);

            size += literal.len;
            (*out_value)[size - 1] = '\0';
        }

        if (field_present) {
            size_t tmp_sz;

            r = output_integer_markup(mdata, &field_name, &format_spec, args,
                auto_number, &tmp_out);
            SOL_INT_CHECK(r, < 0, r);
            tmp_sz = strlen(tmp_out);

            tmp = realloc(*out_value, size + tmp_sz);
            if (!tmp)
                return -ENOMEM;

            *out_value = tmp;
            strncpy((*out_value) + size - 1, tmp_out, tmp_sz);
            free(tmp_out);

            size += tmp_sz;
            (*out_value)[size - 1] = '\0';
        }
    }

    return result > 0 ? 0 : result;
}

/* given:
 *
 * {field_name:format_spec}
 *
 * compute the result and write it to out_value. field_name is allowed
 * to be zero length, in which case we are doing auto field numbering.
 */
static int
output_float_markup(struct string_converter *mdata,
    struct sol_str_slice *field_name,
    struct sol_str_slice *format_spec,
    struct sol_drange *args,
    struct auto_number *auto_number,
    char **out_value)
{
    double *field_obj = NULL;
    int result = -EINVAL;

    /* convert field_name to an actual float_t ptr */
    field_obj = get_float_field(mdata, field_name, args, auto_number);
    if (field_obj == NULL)
        return result;

    result = float_format(mdata, *field_obj, format_spec, out_value);
    SOL_INT_CHECK(result, < 0, result);

    return result;
}

int
do_float_markup(struct string_converter *mdata,
    const char *format,
    struct sol_drange *args,
    struct auto_number *auto_number,
    char **out_value)
{
    struct sol_str_slice format_spec = SOL_STR_SLICE_EMPTY;
    struct sol_str_slice field_name = SOL_STR_SLICE_EMPTY;
    struct sol_str_slice literal = SOL_STR_SLICE_EMPTY;
    struct sol_str_slice iter;
    bool field_present;
    size_t size = 1; //reserve the null byte space
    int result;

    iter = SOL_STR_SLICE_STR(format, strlen(format));

    while ((result = markup_iterator_next(mdata, &iter, &literal, &field_name,
            &format_spec, &field_present)) == 1) {
        char *tmp_out = NULL, *tmp = NULL;
        int r;

        /* literal sub string in format, no markup */
        if (literal.len > 0) {
            tmp = realloc(*out_value, size + literal.len);
            if (!tmp)
                return -ENOMEM;

            *out_value = tmp;
            strncpy((*out_value) + size - 1, literal.data, literal.len);

            size += literal.len;
            (*out_value)[size - 1] = '\0';
        }

        if (field_present) {
            size_t tmp_sz;

            r = output_float_markup(mdata, &field_name, &format_spec, args,
                auto_number, &tmp_out);
            SOL_INT_CHECK(r, < 0, r);
            tmp_sz = strlen(tmp_out);

            tmp = realloc(*out_value, size + tmp_sz);
            if (!tmp)
                return -ENOMEM;

            *out_value = tmp;
            strncpy((*out_value) + size - 1, tmp_out, tmp_sz);
            free(tmp_out);

            size += tmp_sz;
            (*out_value)[size - 1] = '\0';
        }
    }

    return result > 0 ? 0 : result;
}

void
auto_number_init(struct auto_number *auto_number)
{
    auto_number->an_state = ANS_INIT;
    auto_number->an_field_number = 0;
}
