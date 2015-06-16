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

#include "sol-types.h"
#include "sol-log.h"
#include "sol-util.h"

#include <errno.h>
#include <float.h>
#include <math.h>


/********** SOL_DRANGE **********/

SOL_API int
sol_drange_addition(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result)
{
    SOL_NULL_CHECK(var0, -EBADR);
    SOL_NULL_CHECK(var1, -EBADR);
    SOL_NULL_CHECK(result, -EBADR);

    result->val = var0->val + var1->val;
    result->step = DBL_MIN;
    result->min = var0->min + var1->min;
    result->max = var0->max + var1->max;

    return 0;
}

SOL_API int
sol_drange_division(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result)
{
    SOL_NULL_CHECK(var0, -EBADR);
    SOL_NULL_CHECK(var1, -EBADR);
    SOL_NULL_CHECK(result, -EBADR);

    result->val = var0->val / var1->val;
    result->step = DBL_MIN;
    result->min = var0->min / var1->max;
    result->max = var0->max / var1->min;

    return 0;
}

SOL_API int
sol_drange_modulo(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result)
{
    SOL_NULL_CHECK(var0, -EBADR);
    SOL_NULL_CHECK(var1, -EBADR);
    SOL_NULL_CHECK(result, -EBADR);

    errno = 0;
    result->val = fmod(var0->val, var1->val);
    if (errno) {
        SOL_WRN("Modulo failed: %f, %f", var0->val, var1->val);
        return -errno;
    }

    result->step = DBL_MIN;
    result->min = var1->min - 1;
    result->max = var1->max - 1;

    return 0;
}

SOL_API int
sol_drange_multiplication(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result)
{
    SOL_NULL_CHECK(var0, -EBADR);
    SOL_NULL_CHECK(var1, -EBADR);
    SOL_NULL_CHECK(result, -EBADR);

    result->step = DBL_MIN;
    result->val = var0->val * var1->val;
    result->min = var0->min * var1->min;
    result->max = var0->max * var1->max;

    return 0;
}

SOL_API int
sol_drange_subtraction(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result)
{
    SOL_NULL_CHECK(var0, -EBADR);
    SOL_NULL_CHECK(var1, -EBADR);
    SOL_NULL_CHECK(result, -EBADR);

    result->val = var0->val - var1->val;
    result->step = DBL_MIN;
    result->min = var0->min - var1->min;
    result->max = var0->max - var1->max;

    return 0;
}

SOL_API bool
sol_drange_val_equal(double var0, double var1)
{
    double abs_var0, abs_var1, diff;

    diff = fabs(var0 - var1);

    /* when a or b are close to zero relative error isn't meaningful -
     * it handles subnormal case */
    if (fpclassify(var0) == FP_ZERO || fpclassify(var1) == FP_ZERO ||
        isless(diff, DBL_MIN)) {
        return isless(diff, (DBL_EPSILON * DBL_MIN));
    }

    /* use relative error for other cases */
    abs_var0 = fabs(var0);
    abs_var1 = fabs(var1);

    return isless(diff / fmin((abs_var0 + abs_var1), DBL_MAX), DBL_EPSILON);
}

SOL_API bool
sol_drange_equal(const struct sol_drange *var0, const struct sol_drange *var1)
{
    SOL_NULL_CHECK(var0, false);
    SOL_NULL_CHECK(var1, false);

    if (sol_drange_val_equal(var0->val, var1->val) &&
        sol_drange_val_equal(var0->min, var1->min) &&
        sol_drange_val_equal(var0->max, var1->max) &&
        sol_drange_val_equal(var0->step, var1->step))
        return true;

    return false;
}


/********** SOL_IRANGE **********/

#define SOL_IRANGE_ADDITION_OVERFLOW(var0, var1) \
    ((var1 > 0) && (var0 > INT32_MAX - var1))

#define SOL_IRANGE_ADDITION_UNDERFLOW(var0, var1) \
    ((var1 < 0) && (var0 < INT32_MIN - var1))

#define SOL_IRANGE_SUBTRACTION_OVERFLOW(var0, var1) \
    ((var1 < 0) && (var0 > INT32_MAX + var1))

#define SOL_IRANGE_SUBTRACTION_UNDERFLOW(var0, var1) \
    ((var1 > 0) && (var0 < INT32_MIN + var1))

#define SOL_IRANGE_MULTIPLICATION_OVERFLOW(var0, var1) \
    ((var0 > 0) && (var1 > 0) && (var0 > INT32_MAX / var1)) || \
    ((var0 < 0) && (var1 < 0) && (var0 < INT32_MAX / var1))

#define SOL_IRANGE_MULTIPLICATION_UNDERFLOW(var0, var1) \
    ((var0 > 0) && (var1 < 0) && (var0 > INT32_MIN / var1)) || \
    ((var0 < 0) && (var1 > 0) && (var0 < INT32_MIN / var1))

SOL_API int
sol_irange_addition(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result)
{
    SOL_NULL_CHECK(var0, -EBADR);
    SOL_NULL_CHECK(var1, -EBADR);
    SOL_NULL_CHECK(result, -EBADR);

    if (SOL_IRANGE_ADDITION_OVERFLOW(var0->val, var1->val)) {
        SOL_WRN("Addition overflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else if (SOL_IRANGE_ADDITION_UNDERFLOW(var0->val, var1->val)) {
        SOL_WRN("Addition underflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else
        result->val = var0->val + var1->val;

    result->step = 1;

    if (SOL_IRANGE_ADDITION_OVERFLOW(var0->min, var1->min))
        result->min = INT32_MAX;
    else if (SOL_IRANGE_ADDITION_UNDERFLOW(var0->min, var1->min))
        result->min = INT32_MIN;
    else
        result->min = var0->min + var1->min;

    if (SOL_IRANGE_ADDITION_OVERFLOW(var0->max, var1->max))
        result->max = INT32_MAX;
    else if (SOL_IRANGE_ADDITION_UNDERFLOW(var0->max, var1->max))
        result->max = INT32_MIN;
    else
        result->max = var0->max + var1->max;

    return 0;
}

SOL_API bool
sol_irange_equal(const struct sol_irange *var0, const struct sol_irange *var1)
{
    SOL_NULL_CHECK(var0, false);
    SOL_NULL_CHECK(var1, false);

    if ((var0->val != var1->val) ||
        (var0->min != var1->min) ||
        (var0->max != var1->max) ||
        (var0->step != var1->step))
        return false;

    return true;
}

SOL_API int
sol_irange_division(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result)
{
    SOL_NULL_CHECK(var0, -EBADR);
    SOL_NULL_CHECK(var1, -EBADR);
    SOL_NULL_CHECK(result, -EBADR);

    if (var1->val == 0) {
        SOL_WRN("Division by zero: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EDOM;
    }
    result->val = var0->val / var1->val;

    result->step = 1;

    if (var1->max == 0)
        result->min = INT32_MIN;
    else
        result->min = var0->min / var1->max;

    if (var1->min == 0)
        result->max = INT32_MAX;
    else
        result->max = var0->max / var1->min;

    return 0;
}

SOL_API int
sol_irange_modulo(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result)
{
    SOL_NULL_CHECK(var0, -EBADR);
    SOL_NULL_CHECK(var1, -EBADR);
    SOL_NULL_CHECK(result, -EBADR);

    if (var1->val == 0) {
        SOL_WRN("Modulo by zero: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EDOM;
    }
    result->val = var0->val % var1->val;

    result->step = 1;

    if (SOL_IRANGE_SUBTRACTION_UNDERFLOW(var1->min, 1))
        result->min = INT32_MIN;
    else
        result->min = var1->min - 1;

    if (SOL_IRANGE_SUBTRACTION_UNDERFLOW(var1->max, 1))
        result->max = INT32_MIN;
    else
        result->max = var1->max - 1;

    return 0;
}

SOL_API int
sol_irange_multiplication(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result)
{
    SOL_NULL_CHECK(var0, -EBADR);
    SOL_NULL_CHECK(var1, -EBADR);
    SOL_NULL_CHECK(result, -EBADR);

    result->step = 1;

    if (SOL_IRANGE_MULTIPLICATION_OVERFLOW(var0->val, var1->val)) {
        SOL_WRN("Multiplication overflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else if (SOL_IRANGE_MULTIPLICATION_UNDERFLOW(var0->val, var1->val)) {
        SOL_WRN("Multiplication underflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else
        result->val = var0->val * var1->val;

    if (SOL_IRANGE_MULTIPLICATION_OVERFLOW(var0->min, var1->min))
        result->min = INT32_MAX;
    else if (SOL_IRANGE_MULTIPLICATION_UNDERFLOW(var0->min, var1->min))
        result->min = INT32_MIN;
    else
        result->min = var0->min * var1->min;

    if (SOL_IRANGE_MULTIPLICATION_OVERFLOW(var0->max, var1->max))
        result->max = INT32_MAX;
    else if (SOL_IRANGE_MULTIPLICATION_UNDERFLOW(var0->max, var1->max))
        result->max = INT32_MIN;
    else
        result->max = var0->max * var1->max;

    return 0;
}

SOL_API int
sol_irange_subtraction(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result)
{
    SOL_NULL_CHECK(var0, -EBADR);
    SOL_NULL_CHECK(var1, -EBADR);
    SOL_NULL_CHECK(result, -EBADR);

    if (SOL_IRANGE_SUBTRACTION_OVERFLOW(var0->val, var1->val)) {
        SOL_WRN("Subtraction overflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else if (SOL_IRANGE_SUBTRACTION_UNDERFLOW(var0->val, var1->val)) {
        SOL_WRN("Subtraction underflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else
        result->val = var0->val - var1->val;

    result->step = 1;

    if (SOL_IRANGE_SUBTRACTION_OVERFLOW(var0->min, var1->min))
        result->min = INT32_MAX;
    else if (SOL_IRANGE_SUBTRACTION_UNDERFLOW(var0->min, var1->min))
        result->min = INT32_MIN;
    else
        result->min = var0->min - var1->min;

    if (SOL_IRANGE_SUBTRACTION_OVERFLOW(var0->max, var1->max))
        result->max = INT32_MAX;
    else if (SOL_IRANGE_SUBTRACTION_UNDERFLOW(var0->max, var1->max))
        result->max = INT32_MIN;
    else
        result->max = var0->max - var1->max;

    return 0;
}

/********** SOL_RGB **********/

SOL_API int
sol_rgb_set_max(struct sol_rgb *color, uint32_t max_value)
{
    uint64_t val;

    if (!max_value) {
        SOL_WRN("Max value can't be zero");
        return -EINVAL;
    }

    if (!color->red_max || !color->green_max || !color->blue_max) {
        SOL_WRN("Color max values can't be zero.");
        return -EINVAL;
    }

    if (color->red > color->red_max) {
        SOL_WRN("Red component out of range: %" PRId32 " > %" PRId32 ". "
            "Assuming max value.", color->red, color->red_max);
        color->red = color->red_max;
    }
    if (color->green > color->green_max) {
        SOL_WRN("Green component out of range: %" PRId32 " > %" PRId32 ". "
            "Assuming max value.", color->green, color->green_max);
        color->green = color->green_max;
    }
    if (color->blue > color->blue_max) {
        SOL_WRN("Blue component out of range: %" PRId32 " > %" PRId32 ". "
            "Assuming max value.", color->blue, color->blue_max);
        color->blue = color->blue_max;
    }

    val = (uint64_t)color->red * max_value / color->red_max;
    color->red = val;
    color->red_max = max_value;

    val = (uint64_t)color->green * max_value / color->green_max;
    color->green = val;
    color->green_max = max_value;

    val = (uint64_t)color->blue * max_value / color->blue_max;
    color->blue = val;
    color->blue_max = max_value;

    return 0;
}

#undef SOL_IRANGE_ADDITION_OVERFLOW
#undef SOL_IRANGE_ADDITION_UNDERFLOW
#undef SOL_IRANGE_SUBTRACTION_OVERFLOW
#undef SOL_IRANGE_SUBTRACTION_UNDERFLOW
#undef SOL_IRANGE_MULTIPLICATION_OVERFLOW
#undef SOL_IRANGE_MULTIPLICATION_UNDERFLOW
