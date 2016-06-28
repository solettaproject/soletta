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

#include "sol-types.h"
#include "sol-log.h"
#include "sol-util-internal.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <string.h>


/********** SOL_DRANGE **********/

SOL_API int
sol_drange_add(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result)
{
    SOL_NULL_CHECK(var0, -EINVAL);
    SOL_NULL_CHECK(var1, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

    result->val = var0->val + var1->val;
    result->step = DBL_MIN;
    result->min = var0->min + var1->min;
    result->max = var0->max + var1->max;

    return 0;
}

SOL_API int
sol_drange_div(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result)
{
    SOL_NULL_CHECK(var0, -EINVAL);
    SOL_NULL_CHECK(var1, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

    result->val = var0->val / var1->val;
    result->step = DBL_MIN;
    result->min = var0->min / var1->max;
    result->max = var0->max / var1->min;

    return 0;
}

SOL_API int
sol_drange_mod(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result)
{
    SOL_NULL_CHECK(var0, -EINVAL);
    SOL_NULL_CHECK(var1, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

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
sol_drange_mul(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result)
{
    SOL_NULL_CHECK(var0, -EINVAL);
    SOL_NULL_CHECK(var1, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

    result->step = DBL_MIN;
    result->val = var0->val * var1->val;
    result->min = var0->min * var1->min;
    result->max = var0->max * var1->max;

    return 0;
}

SOL_API int
sol_drange_sub(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result)
{
    SOL_NULL_CHECK(var0, -EINVAL);
    SOL_NULL_CHECK(var1, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

    result->val = var0->val - var1->val;
    result->step = DBL_MIN;
    result->min = var0->min - var1->min;
    result->max = var0->max - var1->max;

    return 0;
}

SOL_API bool
sol_drange_eq(const struct sol_drange *var0, const struct sol_drange *var1)
{
    SOL_NULL_CHECK(var0, false);
    SOL_NULL_CHECK(var1, false);

    if (sol_util_double_eq(var0->val, var1->val) &&
        sol_util_double_eq(var0->min, var1->min) &&
        sol_util_double_eq(var0->max, var1->max) &&
        sol_util_double_eq(var0->step, var1->step))
        return true;

    return false;
}

SOL_API int
sol_drange_compose(const struct sol_drange_spec *spec, double value, struct sol_drange *result)
{
    SOL_NULL_CHECK(spec, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

    result->min = spec->min;
    result->max = spec->max;
    result->step = spec->step;
    result->val = value;

    return 0;
}


/********** SOL_IRANGE **********/

#define SOL_IRANGE_ADD_OVERFLOW(var0, var1) \
    ((var1 > 0) && (var0 > INT32_MAX - var1))

#define SOL_IRANGE_ADD_UNDERFLOW(var0, var1) \
    ((var1 < 0) && (var0 < INT32_MIN - var1))

#define SOL_IRANGE_SUB_OVERFLOW(var0, var1) \
    ((var1 < 0) && (var0 > INT32_MAX + var1))

#define SOL_IRANGE_SUB_UNDERFLOW(var0, var1) \
    ((var1 > 0) && (var0 < INT32_MIN + var1))

#define SOL_IRANGE_MUL_OVERFLOW(var0, var1) \
    ((var0 == INT32_MAX) && (var1 == -1)) || \
    ((var0 > 0) && (var1 > 0) && (var0 > INT32_MAX / var1)) || \
    ((var0 < 0) && (var1 < 0) && (var0 < INT32_MAX / var1))

#define SOL_IRANGE_MUL_UNDERFLOW(var0, var1) \
    ((var0 > 0) && (var1 < -1) && (var0 > INT32_MIN / var1)) || \
    ((var0 < 0) && (var1 > 0) && (var0 < INT32_MIN / var1))

SOL_API int
sol_irange_add(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result)
{
    SOL_NULL_CHECK(var0, -EINVAL);
    SOL_NULL_CHECK(var1, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

    if (SOL_IRANGE_ADD_OVERFLOW(var0->val, var1->val)) {
        SOL_WRN("Addition overflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else if (SOL_IRANGE_ADD_UNDERFLOW(var0->val, var1->val)) {
        SOL_WRN("Addition underflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else
        result->val = var0->val + var1->val;

    result->step = 1;

    if (SOL_IRANGE_ADD_OVERFLOW(var0->min, var1->min))
        result->min = INT32_MAX;
    else if (SOL_IRANGE_ADD_UNDERFLOW(var0->min, var1->min))
        result->min = INT32_MIN;
    else
        result->min = var0->min + var1->min;

    if (SOL_IRANGE_ADD_OVERFLOW(var0->max, var1->max))
        result->max = INT32_MAX;
    else if (SOL_IRANGE_ADD_UNDERFLOW(var0->max, var1->max))
        result->max = INT32_MIN;
    else
        result->max = var0->max + var1->max;

    return 0;
}

SOL_API bool
sol_irange_eq(const struct sol_irange *var0, const struct sol_irange *var1)
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
sol_irange_div(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result)
{
    SOL_NULL_CHECK(var0, -EINVAL);
    SOL_NULL_CHECK(var1, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

    if ((var0->val == INT32_MIN) && (var1->val == -1)) {
        SOL_WRN("Division of most negative integer by -1");
        return -EDOM;
    }

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
sol_irange_mod(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result)
{
    SOL_NULL_CHECK(var0, -EINVAL);
    SOL_NULL_CHECK(var1, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

    if (var1->val == 0) {
        SOL_WRN("Modulo by zero: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EDOM;
    }
    result->val = var0->val % var1->val;

    result->step = 1;

    if (SOL_IRANGE_SUB_UNDERFLOW(var1->min, 1))
        result->min = INT32_MIN;
    else
        result->min = var1->min - 1;

    if (SOL_IRANGE_SUB_UNDERFLOW(var1->max, 1))
        result->max = INT32_MIN;
    else
        result->max = var1->max - 1;

    return 0;
}

SOL_API int
sol_irange_mul(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result)
{
    SOL_NULL_CHECK(var0, -EINVAL);
    SOL_NULL_CHECK(var1, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

    result->step = 1;

    if (SOL_IRANGE_MUL_OVERFLOW(var0->val, var1->val)) {
        SOL_WRN("Multiplication overflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else if (SOL_IRANGE_MUL_UNDERFLOW(var0->val, var1->val)) {
        SOL_WRN("Multiplication underflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else
        result->val = var0->val * var1->val;

    if (SOL_IRANGE_MUL_OVERFLOW(var0->min, var1->min))
        result->min = INT32_MAX;
    else if (SOL_IRANGE_MUL_UNDERFLOW(var0->min, var1->min))
        result->min = INT32_MIN;
    else
        result->min = var0->min * var1->min;

    if (SOL_IRANGE_MUL_OVERFLOW(var0->max, var1->max))
        result->max = INT32_MAX;
    else if (SOL_IRANGE_MUL_UNDERFLOW(var0->max, var1->max))
        result->max = INT32_MIN;
    else
        result->max = var0->max * var1->max;

    return 0;
}

SOL_API int
sol_irange_sub(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result)
{
    SOL_NULL_CHECK(var0, -EINVAL);
    SOL_NULL_CHECK(var1, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

    if (SOL_IRANGE_SUB_OVERFLOW(var0->val, var1->val)) {
        SOL_WRN("Subtraction overflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else if (SOL_IRANGE_SUB_UNDERFLOW(var0->val, var1->val)) {
        SOL_WRN("Subtraction underflow: %" PRId32 ", %" PRId32, var0->val, var1->val);
        return -EOVERFLOW;
    } else
        result->val = var0->val - var1->val;

    result->step = 1;

    if (SOL_IRANGE_SUB_OVERFLOW(var0->min, var1->min))
        result->min = INT32_MAX;
    else if (SOL_IRANGE_SUB_UNDERFLOW(var0->min, var1->min))
        result->min = INT32_MIN;
    else
        result->min = var0->min - var1->min;

    if (SOL_IRANGE_SUB_OVERFLOW(var0->max, var1->max))
        result->max = INT32_MAX;
    else if (SOL_IRANGE_SUB_UNDERFLOW(var0->max, var1->max))
        result->max = INT32_MIN;
    else
        result->max = var0->max - var1->max;

    return 0;
}

SOL_API int
sol_irange_compose(const struct sol_irange_spec *spec, int32_t value, struct sol_irange *result)
{
    SOL_NULL_CHECK(spec, -EINVAL);
    SOL_NULL_CHECK(result, -EINVAL);

    result->min = spec->min;
    result->max = spec->max;
    result->step = spec->step;
    result->val = value;

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

SOL_API bool
sol_rgb_eq(const struct sol_rgb *var0, const struct sol_rgb *var1)
{
    SOL_NULL_CHECK(var0, false);
    SOL_NULL_CHECK(var1, false);

    if (var0->red != var1->red ||
        var0->blue != var1->blue ||
        var0->green != var1->green ||
        var0->red_max != var1->red_max ||
        var0->blue_max != var1->blue_max ||
        var0->green_max != var1->green_max)
        return false;
    return true;

}

SOL_API bool
sol_direction_vector_eq(const struct sol_direction_vector *var0,
    const struct sol_direction_vector *var1)
{
    SOL_NULL_CHECK(var0, false);
    SOL_NULL_CHECK(var1, false);

    if (sol_util_double_eq(var0->x, var1->x) &&
        sol_util_double_eq(var0->y, var1->y) &&
        sol_util_double_eq(var0->z, var1->z) &&
        sol_util_double_eq(var0->min, var1->min) &&
        sol_util_double_eq(var0->max, var1->max))
        return true;
    return false;
}

#undef SOL_IRANGE_ADD_OVERFLOW
#undef SOL_IRANGE_ADD_UNDERFLOW
#undef SOL_IRANGE_SUB_OVERFLOW
#undef SOL_IRANGE_SUB_UNDERFLOW
#undef SOL_IRANGE_MUL_OVERFLOW
#undef SOL_IRANGE_MUL_UNDERFLOW
