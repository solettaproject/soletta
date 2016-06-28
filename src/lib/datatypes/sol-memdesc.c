/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <stdlib.h>
#include <errno.h>

#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-util-internal.h"
#include "sol-str-table.h"
#include "sol-memdesc.h"

#ifndef SOL_NO_API_VERSION
#define _CHECK_API(h, b, on_failure) \
    do { \
        if ((h)->api_version != (b)) { \
            SOL_WRN("" # h "(%p)->api_version(%" PRIu16 ") != " # b "(%" PRIu16 ")", \
                (h), (h)->api_version, b); \
            on_failure; \
        } \
    } while (0)

#else
#define _CHECK_API(h, b, on_failure)
#endif

#define CHECK_API(h, b, ...) _CHECK_API(h, b, return __VA_ARGS__)
#define CHECK_API_GOTO(h, b, label) _CHECK_API(h, b, goto label)

#define VALIDATE_MEMDESC(memdesc, on_failure) \
    do { \
        _CHECK_API(memdesc, SOL_MEMDESC_API_VERSION, on_failure); \
        if (memdesc->ops) { \
            _CHECK_API(memdesc->ops, SOL_MEMDESC_OPS_API_VERSION, on_failure); \
            if (memdesc->type == SOL_MEMDESC_TYPE_ARRAY && memdesc->ops->array) { \
                _CHECK_API(memdesc->ops->array, SOL_MEMDESC_OPS_ARRAY_API_VERSION, on_failure); \
            } else if (memdesc->type == SOL_MEMDESC_TYPE_ENUMERATION && memdesc->ops->enumeration) { \
                _CHECK_API(memdesc->ops->enumeration, SOL_MEMDESC_OPS_ENUMERATION_API_VERSION, on_failure); \
            } \
        } \
        if (memdesc->type == SOL_MEMDESC_TYPE_ARRAY) { \
            if (!memdesc->size) { \
                SOL_WRN("" # memdesc "(%p)->size cannot be zero for array.", memdesc); \
                on_failure; \
            } \
            if (memdesc->array_item) { \
                _CHECK_API(memdesc->array_item, SOL_MEMDESC_API_VERSION, on_failure); \
            } \
        } else if (memdesc->type == SOL_MEMDESC_TYPE_STRUCTURE) { \
            if (!memdesc->size) { \
                SOL_WRN("" # memdesc "(%p)->size cannot be zero for structure.", memdesc); \
                on_failure; \
            } \
            if (memdesc->structure_members) { \
                _CHECK_API(&(memdesc->structure_members->base), SOL_MEMDESC_API_VERSION, on_failure); \
            } \
        } else if (memdesc->type == SOL_MEMDESC_TYPE_PTR) { \
            if (memdesc->pointed_item) { \
                _CHECK_API(memdesc->pointed_item, SOL_MEMDESC_API_VERSION, on_failure); \
            } \
        } else if (memdesc->type == SOL_MEMDESC_TYPE_ENUMERATION) { \
            if (!memdesc->size) { \
                SOL_WRN("" # memdesc "(%p)->size cannot be zero for enumeration.", memdesc); \
                on_failure; \
            } else if (memdesc->size > sizeof(int64_t)) { \
                SOL_WRN("" # memdesc "(%p)->size cannot be larger than 8 bytes (64bits) for enumeration.", memdesc); \
                on_failure; \
            } \
        } \
    } while (0)

#define CHECK_MEMDESC(memdesc, ...) \
    do { \
        SOL_NULL_CHECK(memdesc, __VA_ARGS__); \
        VALIDATE_MEMDESC(memdesc, return __VA_ARGS__); \
    } while (0)

#define CHECK_MEMDESC_GOTO(memdesc, label) \
    do { \
        SOL_NULL_CHECK_GOTO(memdesc, label); \
        VALIDATE_MEMDESC(memdesc, goto label); \
    } while (0)

#ifndef SOL_NO_API_VERSION
SOL_API const uint16_t SOL_MEMDESC_API_VERSION_COMPILED = SOL_MEMDESC_API_VERSION;
#endif

SOL_API enum sol_memdesc_type
sol_memdesc_type_from_str(const char *str)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("uint8_t", SOL_MEMDESC_TYPE_UINT8),
        SOL_STR_TABLE_ITEM("uint16_t", SOL_MEMDESC_TYPE_UINT16),
        SOL_STR_TABLE_ITEM("uint32_t", SOL_MEMDESC_TYPE_UINT32),
        SOL_STR_TABLE_ITEM("uint64_t", SOL_MEMDESC_TYPE_UINT64),
        SOL_STR_TABLE_ITEM("unsigned long", SOL_MEMDESC_TYPE_ULONG),
        SOL_STR_TABLE_ITEM("size_t", SOL_MEMDESC_TYPE_SIZE),
        SOL_STR_TABLE_ITEM("int8_t", SOL_MEMDESC_TYPE_INT8),
        SOL_STR_TABLE_ITEM("int16_t", SOL_MEMDESC_TYPE_INT16),
        SOL_STR_TABLE_ITEM("int32_t", SOL_MEMDESC_TYPE_INT32),
        SOL_STR_TABLE_ITEM("int64_t", SOL_MEMDESC_TYPE_INT64),
        SOL_STR_TABLE_ITEM("long", SOL_MEMDESC_TYPE_LONG),
        SOL_STR_TABLE_ITEM("ssize_t", SOL_MEMDESC_TYPE_SSIZE),
        SOL_STR_TABLE_ITEM("boolean", SOL_MEMDESC_TYPE_BOOL),
        SOL_STR_TABLE_ITEM("double", SOL_MEMDESC_TYPE_DOUBLE),
        SOL_STR_TABLE_ITEM("string", SOL_MEMDESC_TYPE_STRING),
        SOL_STR_TABLE_ITEM("const string", SOL_MEMDESC_TYPE_CONST_STRING),
        SOL_STR_TABLE_ITEM("enumeration", SOL_MEMDESC_TYPE_ENUMERATION),
        SOL_STR_TABLE_ITEM("pointer", SOL_MEMDESC_TYPE_PTR),
        SOL_STR_TABLE_ITEM("structure", SOL_MEMDESC_TYPE_STRUCTURE),
        SOL_STR_TABLE_ITEM("array", SOL_MEMDESC_TYPE_ARRAY),
        { }
    };

    SOL_NULL_CHECK(str, SOL_MEMDESC_TYPE_UNKNOWN);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(str), SOL_MEMDESC_TYPE_UNKNOWN);
}

SOL_API const char *
sol_memdesc_type_to_str(enum sol_memdesc_type type)
{
    static const char *strs[] = {
        [SOL_MEMDESC_TYPE_UINT8] = "uint8_t",
        [SOL_MEMDESC_TYPE_UINT16] = "uint16_t",
        [SOL_MEMDESC_TYPE_UINT32] = "uint32_t",
        [SOL_MEMDESC_TYPE_UINT64] = "uint64_t",
        [SOL_MEMDESC_TYPE_ULONG] = "unsigned long",
        [SOL_MEMDESC_TYPE_SIZE] = "size_t",
        [SOL_MEMDESC_TYPE_INT8] = "int8_t",
        [SOL_MEMDESC_TYPE_INT16] = "int16_t",
        [SOL_MEMDESC_TYPE_INT32] = "int32_t",
        [SOL_MEMDESC_TYPE_INT64] = "int64_t",
        [SOL_MEMDESC_TYPE_LONG] = "long",
        [SOL_MEMDESC_TYPE_SSIZE] = "ssize_t",
        [SOL_MEMDESC_TYPE_BOOL] = "boolean",
        [SOL_MEMDESC_TYPE_DOUBLE] = "double",
        [SOL_MEMDESC_TYPE_STRING] = "string",
        [SOL_MEMDESC_TYPE_CONST_STRING] = "const string",
        [SOL_MEMDESC_TYPE_ENUMERATION] = "enumeration",
        [SOL_MEMDESC_TYPE_PTR] = "pointer",
        [SOL_MEMDESC_TYPE_STRUCTURE] = "structure",
        [SOL_MEMDESC_TYPE_ARRAY] = "array",
    };

    if (type < sol_util_array_size(strs))
        return strs[type];

    return NULL;
}

static int copy_structure(const struct sol_memdesc *structure_desc, void *memory, const void *ptr_content);
static int copy_array(const struct sol_memdesc *array_desc, void *dst_memory, const void *src_memory);


static inline uint8_t
enumeration_offsetof_int64(const struct sol_memdesc *desc)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if (desc->size < sizeof(int64_t))
        return sizeof(desc->defcontent.e) - desc->size;
#endif

    return 0;
}

static inline int64_t
enumeration_as_int64(const struct sol_memdesc *desc, const void *mem)
{
    int64_t i64 = 0;
    uint8_t offset = enumeration_offsetof_int64(desc);


    memcpy((uint8_t *)&i64 + offset, mem, desc->size);
    return i64;
}

static inline const void *
get_defcontent(const struct sol_memdesc *desc)
{
    switch (desc->type) {
    case SOL_MEMDESC_TYPE_UINT8:
        return &desc->defcontent.u8;
    case SOL_MEMDESC_TYPE_UINT16:
        return &desc->defcontent.u16;
    case SOL_MEMDESC_TYPE_UINT32:
        return &desc->defcontent.u32;
    case SOL_MEMDESC_TYPE_UINT64:
        return &desc->defcontent.u64;
    case SOL_MEMDESC_TYPE_ULONG:
        return &desc->defcontent.ul;
    case SOL_MEMDESC_TYPE_SIZE:
        return &desc->defcontent.sz;
    case SOL_MEMDESC_TYPE_INT8:
        return &desc->defcontent.i8;
    case SOL_MEMDESC_TYPE_INT16:
        return &desc->defcontent.i16;
    case SOL_MEMDESC_TYPE_INT32:
        return &desc->defcontent.i32;
    case SOL_MEMDESC_TYPE_INT64:
        return &desc->defcontent.i64;
    case SOL_MEMDESC_TYPE_LONG:
        return &desc->defcontent.l;
    case SOL_MEMDESC_TYPE_SSIZE:
        return &desc->defcontent.ssz;
    case SOL_MEMDESC_TYPE_BOOL:
        return &desc->defcontent.b;
    case SOL_MEMDESC_TYPE_DOUBLE:
        return &desc->defcontent.d;
    case SOL_MEMDESC_TYPE_STRING:
    case SOL_MEMDESC_TYPE_CONST_STRING:
        return &desc->defcontent.s;
    case SOL_MEMDESC_TYPE_ENUMERATION: {
        const uint8_t *m = (const uint8_t *)&desc->defcontent.e;

        return m + enumeration_offsetof_int64(desc);
    }
    case SOL_MEMDESC_TYPE_PTR:
        return &desc->defcontent.p;
    case SOL_MEMDESC_TYPE_STRUCTURE:
    case SOL_MEMDESC_TYPE_ARRAY:
        return desc->defcontent.p;

    default:
        return NULL;
    }
}

static inline int
set_content(const struct sol_memdesc *desc, void *mem, const void *ptr_content)
{
    if (desc->ops && desc->ops->set_content)
        return desc->ops->set_content(desc, mem, ptr_content);

    if (desc->type == SOL_MEMDESC_TYPE_STRING) {
        const char *const *pv = ptr_content;
        int r = sol_util_replace_str_if_changed(mem, *pv);
        if (r >= 0)
            return 0;
        return r;
    } else if (desc->type == SOL_MEMDESC_TYPE_PTR && desc->pointed_item) {
        const void *const *pv = ptr_content;
        void **m = mem;

        if (!*m && *pv) {
            *m = sol_memdesc_new_with_defaults(desc->pointed_item);
            if (!*m)
                return -errno;
        } else if (*m && !*pv) {
            sol_memdesc_free(desc->pointed_item, *m);
            *m = NULL;
            return 0;
        } else if (!*pv)
            return 0;

        return set_content(desc->pointed_item, *m, *pv);

    } else if (desc->type == SOL_MEMDESC_TYPE_STRUCTURE) {
        if (!desc->structure_members) {
            SOL_WRN("desc=%p is SOL_MEMDESC_TYPE_STRUCTURE but does not provide structure_members", desc);
            return -EINVAL;
        }

        return copy_structure(desc, mem, ptr_content);
    } else if (desc->type == SOL_MEMDESC_TYPE_ARRAY) {
        if (!desc->array_item) {
            SOL_WRN("desc=%p is SOL_MEMDESC_TYPE_ARRAY but does not provide array_item", desc);
            return -EINVAL;
        }

        return copy_array(desc, mem, ptr_content);
    }

    memcpy(mem, ptr_content, sol_memdesc_get_size(desc));
    return 0;
}

static int
copy_content(const struct sol_memdesc *desc, const void *src_memory, void *dst_memory)
{
    if (desc->ops && desc->ops->copy)
        return desc->ops->copy(desc, src_memory, dst_memory);

    return set_content(desc, dst_memory, src_memory);
}

static int
copy_structure(const struct sol_memdesc *desc, void *memory, const void *ptr_content)
{
    const struct sol_memdesc_structure_member *itr;
    const void *src_mem;

    SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER_MEMORY(desc, itr, ptr_content, src_mem) {
        void *dst_mem;
        int r;

        dst_mem = sol_memdesc_get_structure_member_memory(desc, itr, memory);
        if (!dst_mem)
            return -errno;
        r = copy_content(&itr->base, src_mem, dst_mem);
        if (r < 0)
            return r;
    }

    return 0;
}

static int compare_content(const struct sol_memdesc *desc, const void *a_memory, const void *b_memory);

static int
compare_structure(const struct sol_memdesc *desc, const void *a_memory, const void *b_memory)
{
    const struct sol_memdesc_structure_member *itr;
    const void *a_mem;

    errno = 0;
    SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER_MEMORY(desc, itr, a_memory, a_mem) {
        const void **b_mem;
        int r;

        b_mem = sol_memdesc_get_structure_member_memory(desc, itr, b_memory);
        r = compare_content(&itr->base, a_mem, b_mem);
        if (r != 0 || errno)
            return r;
    }

    return 0;
}

static int
compare_array(const struct sol_memdesc *desc, const void *a_memory, const void *b_memory)
{
    const void *a_item, *b_item;
    ssize_t idx, len, a_len, b_len;

    a_len = sol_memdesc_get_array_length(desc, a_memory);
    if (a_len < 0) {
        errno = -a_len;
        return 0;
    }
    b_len = sol_memdesc_get_array_length(desc, b_memory);
    if (b_len < 0) {
        errno = -b_len;
        return 0;
    }

    len = sol_util_min(a_len, b_len);
    SOL_MEMDESC_FOREACH_ARRAY_ELEMENT_IN_RANGE(desc, a_memory, 0, len, idx, a_item) {
        int r;

        b_item = sol_memdesc_get_array_element(desc, b_memory, idx);
        if (!b_item)
            return 0;

        r = compare_content(desc->array_item, a_item, b_item);
        if (r != 0 || errno)
            return r;
    }

    if (idx < len) /* loop failed */
        return 0;

    if (a_len < b_len)
        return -1;
    if (a_len > b_len)
        return 1;

    return 0;
}

static int
copy_array(const struct sol_memdesc *desc, void *dst_memory, const void *src_memory)
{
    const void *src_item;
    ssize_t idx, len;
    int r;

    len = sol_memdesc_get_array_length(desc, src_memory);
    if (len < 0) {
        errno = -len;
        return 0;
    }

    r = sol_memdesc_resize_array(desc, dst_memory, len);
    if (r < 0)
        return r;

    SOL_MEMDESC_FOREACH_ARRAY_ELEMENT_IN_RANGE(desc, src_memory, 0, len, idx, src_item) {
        void *dst_item = sol_memdesc_get_array_element(desc, dst_memory, idx);

        if (!dst_item) {
            r = -errno;
            goto failure;
        }

        r = set_content(desc->array_item, dst_item, src_item);
        if (r < 0)
            goto failure;
    }

    if (idx < len) {
        r = errno ? -errno : -EINVAL;
        goto failure;
    }

    return 0;

failure:
    sol_memdesc_resize_array(desc, dst_memory, idx);
    return r;
}

static int
compare_content(const struct sol_memdesc *desc, const void *a_mem, const void *b_mem)
{
    if (desc->ops && desc->ops->compare)
        return desc->ops->compare(desc, a_mem, b_mem);

#define RET_CMP_INT(type) \
    do { \
        const type *a = a_mem; \
        const type *b = b_mem; \
        if (*a < *b) \
            return -1; \
        else if (*a > *b) \
            return 1; \
        else \
            return 0; \
    } while (0)

    switch (desc->type) {
    case SOL_MEMDESC_TYPE_UINT8:
        RET_CMP_INT(uint8_t);
    case SOL_MEMDESC_TYPE_UINT16:
        RET_CMP_INT(uint16_t);
    case SOL_MEMDESC_TYPE_UINT32:
        RET_CMP_INT(uint32_t);
    case SOL_MEMDESC_TYPE_UINT64:
        RET_CMP_INT(uint64_t);
    case SOL_MEMDESC_TYPE_ULONG:
        RET_CMP_INT(unsigned long);
    case SOL_MEMDESC_TYPE_SIZE:
        RET_CMP_INT(size_t);
    case SOL_MEMDESC_TYPE_INT8:
        RET_CMP_INT(int8_t);
    case SOL_MEMDESC_TYPE_INT16:
        RET_CMP_INT(int16_t);
    case SOL_MEMDESC_TYPE_INT32:
        RET_CMP_INT(int32_t);
    case SOL_MEMDESC_TYPE_INT64:
        RET_CMP_INT(int64_t);
    case SOL_MEMDESC_TYPE_LONG:
        RET_CMP_INT(long);
    case SOL_MEMDESC_TYPE_SSIZE:
        RET_CMP_INT(ssize_t);
    case SOL_MEMDESC_TYPE_BOOL: {
        const bool *a = a_mem;
        const bool *b = b_mem;

        if (!*a && *b)
            return -1;
        else if (*a && !*b)
            return 1;
        else
            return 0;
    }
    case SOL_MEMDESC_TYPE_DOUBLE: {
        const double *a = a_mem;
        const double *b = b_mem;

        if (sol_util_double_eq(*a, *b))
            return 0;
        else if (*a < *b)
            return -1;
        else
            return 1;
    }
    case SOL_MEMDESC_TYPE_CONST_STRING:
    case SOL_MEMDESC_TYPE_STRING: {
        const char *const *a = a_mem;
        const char *const *b = b_mem;

        if (!*a && *b)
            return -1;
        else if (*a && !*b)
            return 1;
        else if (*a == *b)
            return 0;
        else
            return strcmp(*a, *b);
    }
    case SOL_MEMDESC_TYPE_ENUMERATION: {
        int64_t a = enumeration_as_int64(desc, a_mem);
        int64_t b = enumeration_as_int64(desc, b_mem);

        if (a < b)
            return -1;
        else if (a > b)
            return 1;
        else
            return 0;
    }
    case SOL_MEMDESC_TYPE_PTR: {
        const void *const *a = a_mem;
        const void *const *b = b_mem;

        if (!*a && *b)
            return -1;
        else if (*a && !*b)
            return 1;
        else if (*a == *b)
            return 0;

        if (!desc->pointed_item) {
            SOL_WRN("desc=%p is SOL_MEMDESC_TYPE_PTR but does not provide pointed_item or ops->compare", desc);
            errno = -EINVAL;
            return 0;
        }

        return compare_content(desc->pointed_item, *a, *b);
    }
    case SOL_MEMDESC_TYPE_STRUCTURE: {
        if (!desc->structure_members) {
            SOL_WRN("desc=%p is SOL_MEMDESC_TYPE_STRUCTURE but does not provide structure_members", desc);
            errno = EINVAL;
            return 0;
        }
        return compare_structure(desc, a_mem, b_mem);
    }
    case SOL_MEMDESC_TYPE_ARRAY: {
        if (!desc->array_item) {
            SOL_WRN("desc=%p is SOL_MEMDESC_TYPE_ARRAY but does not provide array_item", desc);
            errno = EINVAL;
            return 0;
        }
        return compare_array(desc, a_mem, b_mem);
    }
    default:
        errno = EINVAL;
        return 0;
    }

#undef RET_CMP_INT
}

SOL_API int
sol_memdesc_init_defaults(const struct sol_memdesc *desc, void *mem)
{
    const void *defcontent;

    CHECK_MEMDESC(desc, -EINVAL);
    SOL_NULL_CHECK(mem, -EINVAL);

    memset(mem, 0, sol_memdesc_get_size(desc));

    if (desc->ops && desc->ops->init_defaults)
        return desc->ops->init_defaults(desc, mem);

    if (desc->type == SOL_MEMDESC_TYPE_STRUCTURE) {
        const struct sol_memdesc_structure_member *itr;
        void *itmem;

        if (!desc->structure_members) {
            SOL_WRN("desc=%p is SOL_MEMDESC_TYPE_STRUCTURE but does not provide structure_members", desc);
            return -EINVAL;
        }

        SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER_MEMORY(desc, itr, mem, itmem) {
            int r;

            r = sol_memdesc_init_defaults(&itr->base, itmem);
            if (r < 0)
                return r;
        }
    }

    defcontent = get_defcontent(desc);
    if (!defcontent)
        return 0;
    return set_content(desc, mem, defcontent);
}

SOL_API int
sol_memdesc_copy(const struct sol_memdesc *desc, const void *src_memory, void *dst_memory)
{
    CHECK_MEMDESC(desc, -EINVAL);
    SOL_NULL_CHECK(src_memory, -EINVAL);
    SOL_NULL_CHECK(dst_memory, -EINVAL);

    return copy_content(desc, src_memory, dst_memory);
}

SOL_API int
sol_memdesc_set_content(const struct sol_memdesc *desc, void *memory, const void *ptr_content)
{
    CHECK_MEMDESC(desc, -EINVAL);
    SOL_NULL_CHECK(memory, -EINVAL);
    SOL_NULL_CHECK(ptr_content, -EINVAL);

    return set_content(desc, memory, ptr_content);
}

SOL_API int
sol_memdesc_compare(const struct sol_memdesc *desc, const void *a_memory, const void *b_memory)
{
    errno = EINVAL;
    CHECK_MEMDESC(desc, 0);
    SOL_NULL_CHECK(a_memory, 0);
    SOL_NULL_CHECK(b_memory, 0);

    errno = 0;
    return compare_content(desc, a_memory, b_memory);
}

SOL_API int
sol_memdesc_free_content(const struct sol_memdesc *desc, void *mem)
{
    CHECK_MEMDESC(desc, -EINVAL);
    SOL_NULL_CHECK(mem, -EINVAL);

    if (desc->ops && desc->ops->free_content)
        return desc->ops->free_content(desc, mem);

    if (desc->type == SOL_MEMDESC_TYPE_STRING) {
        char **m = mem;
        free(*m);
        *m = NULL;
        return 0;
    } else if (desc->type == SOL_MEMDESC_TYPE_PTR && desc->pointed_item) {
        void **m = mem;

        if (*m) {
            sol_memdesc_free(desc->pointed_item, *m);
            *m = NULL;
        }
        return 0;
    } else if (desc->type == SOL_MEMDESC_TYPE_STRUCTURE) {
        const struct sol_memdesc_structure_member *itr;
        void *itmem;
        int ret = 0;

        if (!desc->structure_members) {
            SOL_WRN("desc=%p is SOL_MEMDESC_TYPE_STRUCTURE but does not provide structure_members", desc);
            return -EINVAL;
        }

        SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER_MEMORY(desc, itr, mem, itmem) {
            int r;

            r = sol_memdesc_free_content(&itr->base, itmem);
            if (r < 0 && ret == 0)
                ret = r;
        }
        return ret;
    } else if (desc->type == SOL_MEMDESC_TYPE_ARRAY)
        return sol_memdesc_resize_array(desc, mem, 0);

    memset(mem, 0, sol_memdesc_get_size(desc));
    return 0;
}

SOL_API ssize_t
sol_memdesc_get_array_length(const struct sol_memdesc *array, const void *memory)
{
    CHECK_MEMDESC(array, -EINVAL);
    SOL_NULL_CHECK(memory, -EINVAL);

    if (array->type != SOL_MEMDESC_TYPE_ARRAY) {
        SOL_WRN("array=%p is not SOL_MEMDESC_TYPE_ARRAY", array);
        return -EINVAL;
    } else if (!array->ops || !array->ops->array || !array->ops->array->get_length) {
        SOL_WRN("array=%p is SOL_MEMDESC_TYPE_ARRAY but does not provide ops->array->get_length", array);
        return -EINVAL;
    }

    return array->ops->array->get_length(array, memory);
}

SOL_API void *
sol_memdesc_get_array_element(const struct sol_memdesc *array, const void *memory, size_t idx)
{
    errno = EINVAL;
    CHECK_MEMDESC(array, NULL);
    SOL_NULL_CHECK(memory, NULL);

    if (array->type != SOL_MEMDESC_TYPE_ARRAY) {
        SOL_WRN("array=%p is not SOL_MEMDESC_TYPE_ARRAY", array);
        return NULL;
    } else if (!array->ops || !array->ops->array || !array->ops->array->get_element) {
        SOL_WRN("array=%p is SOL_MEMDESC_TYPE_ARRAY but does not provide ops->array->get_element", array);
        return NULL;
    }

    errno = 0;
    return array->ops->array->get_element(array, memory, idx);
}

SOL_API int
sol_memdesc_resize_array(const struct sol_memdesc *array, void *memory, size_t length)
{
    CHECK_MEMDESC(array, -EINVAL);
    SOL_NULL_CHECK(memory, -EINVAL);

    if (array->type != SOL_MEMDESC_TYPE_ARRAY) {
        SOL_WRN("array=%p is not SOL_MEMDESC_TYPE_ARRAY", array);
        return -EINVAL;
    } else if (!array->ops || !array->ops->array || !array->ops->array->resize) {
        SOL_WRN("array=%p is SOL_MEMDESC_TYPE_ARRAY but does not provide ops->array->resize", array);
        return -EINVAL;
    }

    return array->ops->array->resize(array, memory, length);
}

SOL_API const char *
sol_memdesc_enumeration_to_str(const struct sol_memdesc *enumeration, const void *memory)
{
    const struct sol_str_table_int64 *itr;
    int64_t v;

    errno = EINVAL;
    CHECK_MEMDESC(enumeration, NULL);
    SOL_NULL_CHECK(memory, NULL);

    if (enumeration->type != SOL_MEMDESC_TYPE_ENUMERATION) {
        SOL_WRN("enumeration=%p is not SOL_MEMDESC_TYPE_ENUMERATION", enumeration);
        return NULL;
    } else if (enumeration->ops && enumeration->ops->enumeration && enumeration->ops->enumeration->to_str) {
        errno = 0;
        return enumeration->ops->enumeration->to_str(enumeration, memory);
    } else if (!enumeration->enumeration_mapping) {
        SOL_WRN("enumeration=%p is SOL_MEMDESC_TYPE_ENUMERATION but does not provide enumeration_mapping or ops->enumeration->to_str", enumeration);
        return NULL;
    }

    v = enumeration_as_int64(enumeration, memory);
    for (itr = enumeration->enumeration_mapping; itr->key != NULL; itr++) {
        if (itr->val == v) {
            errno = 0;
            return itr->key;
        }
    }

    errno = ENOENT;
    return NULL;
}

SOL_API int
sol_memdesc_enumeration_from_str(const struct sol_memdesc *enumeration, void *ptr_return, const struct sol_str_slice str)
{
    const uint8_t *src;
    int64_t v;

    CHECK_MEMDESC(enumeration, -EINVAL);
    SOL_NULL_CHECK(ptr_return, -EINVAL);
    SOL_INT_CHECK(str.len, == 0, -EINVAL);

    if (enumeration->type != SOL_MEMDESC_TYPE_ENUMERATION) {
        SOL_WRN("enumeration=%p is not SOL_MEMDESC_TYPE_ENUMERATION", enumeration);
        return -EINVAL;
    } else if (enumeration->ops && enumeration->ops->enumeration && enumeration->ops->enumeration->from_str)
        return enumeration->ops->enumeration->from_str(enumeration, ptr_return, str);
    else if (!enumeration->enumeration_mapping) {
        SOL_WRN("enumeration=%p is SOL_MEMDESC_TYPE_ENUMERATION but does not provide enumeration_mapping or ops->enumeration->from_str", enumeration);
        return -EINVAL;
    }

    v = sol_str_table_int64_lookup_fallback(enumeration->enumeration_mapping, str, 0);
    if (errno)
        return -errno;

    src = (const uint8_t *)&v + enumeration_offsetof_int64(enumeration);

    return set_content(enumeration, ptr_return, src);
}

static int
vector_ops_init_defaults(const struct sol_memdesc *array, void *mem)
{
    struct sol_vector *v = mem;
    uint16_t item_size = sol_memdesc_get_size(array->array_item);

    SOL_INT_CHECK(item_size, == 0, -EINVAL);
    SOL_INT_CHECK(array->size, != sizeof(struct sol_vector), -EINVAL);

    sol_vector_init(v, item_size);

    if (array->defcontent.p)
        return sol_memdesc_set_content(array, mem, array->defcontent.p);

    return 0;
}

static ssize_t
vector_ops_get_array_length(const struct sol_memdesc *array, const void *memory)
{
    const struct sol_vector *v = memory;

    return v->len;
}

static void *
vector_ops_get_array_element(const struct sol_memdesc *array, const void *memory, size_t idx)
{
    const struct sol_vector *v = memory;

    errno = ERANGE;
    SOL_INT_CHECK(idx, > UINT16_MAX, NULL);
    errno = 0;

    return sol_vector_get(v, idx);
}

static int
vector_ops_resize_array(const struct sol_memdesc *array, void *memory, size_t len)
{
    struct sol_vector *v = memory;
    uint16_t oldlen;

    SOL_INT_CHECK(len, > UINT16_MAX, -ERANGE);

    oldlen = v->len;
    if (oldlen == len)
        return 0;

    if (oldlen < len) {
        void *m = sol_vector_append_n(v, len - oldlen);

        if (!m)
            return errno ? -errno : -ENOMEM;

        if (sol_memdesc_get_size(array->array_item)) {
            uint16_t idx;

            for (idx = oldlen; idx < len; idx++) {
                void *itmem;
                int r;

                itmem = sol_vector_get_no_check(v, idx);
                r = sol_memdesc_init_defaults(array->array_item, itmem);
                if (r < 0) {
                    sol_vector_del_range(v, idx, len - idx);
                    return r;
                }
            }
        }

        return 0;
    } else {
        if (sol_memdesc_get_size(array->array_item)) {
            uint16_t idx;

            for (idx = len; idx < oldlen; idx++) {
                void *itmem;

                itmem = sol_vector_get_no_check(v, idx);
                sol_memdesc_free_content(array->array_item, itmem);
            }
        }

        return sol_vector_del_range(v, len, oldlen - len);
    }
}

SOL_API const struct sol_memdesc_ops SOL_MEMDESC_OPS_VECTOR = {
    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_OPS_API_VERSION, )
    .init_defaults = vector_ops_init_defaults,
    .array = &(const struct sol_memdesc_ops_array){
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_OPS_ARRAY_API_VERSION, )
        .get_length = vector_ops_get_array_length,
        .get_element = vector_ops_get_array_element,
        .resize = vector_ops_resize_array,
    },
};


static int
ptr_vector_ops_init_defaults(const struct sol_memdesc *desc, void *mem)
{
    struct sol_ptr_vector *v = mem;

    if (desc->size != sizeof(struct sol_ptr_vector))
        return -EINVAL;

    if (desc->array_item && sol_memdesc_get_size(desc->array_item) != sizeof(void *))
        return -EINVAL;

    sol_ptr_vector_init(v);

    if (desc->defcontent.p)
        return sol_memdesc_set_content(desc, mem, desc->defcontent.p);

    return 0;
}

SOL_API const struct sol_memdesc_ops SOL_MEMDESC_OPS_PTR_VECTOR = {
    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_OPS_API_VERSION, )
    .init_defaults = ptr_vector_ops_init_defaults,
    .array = &(const struct sol_memdesc_ops_array){
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_OPS_ARRAY_API_VERSION, )
        .get_length = vector_ops_get_array_length,
        .get_element = vector_ops_get_array_element,
        .resize = vector_ops_resize_array,
    },
};

static int
serialize_indent(struct sol_buffer *buf, struct sol_buffer *prefix, const struct sol_str_slice str)
{
    int r;

    if (!str.len)
        return 0;

    r = sol_buffer_append_slice(prefix, str);
    if (r < 0)
        return r;

    return sol_buffer_append_buffer(buf, prefix);
}

static int
serialize_deindent(struct sol_buffer *buf, struct sol_buffer *prefix, const struct sol_str_slice str, bool output)
{
    int r;

    if (!str.len)
        return 0;

    SOL_INT_CHECK(prefix->used, < str.len, -EINVAL);

    r = sol_buffer_remove_data(prefix, prefix->used - str.len, str.len);
    if (r < 0)
        return r;

    if (output)
        return sol_buffer_append_buffer(buf, prefix);
    return 0;
}

static int
default_serialize_int64(const struct sol_memdesc *desc, int64_t value, struct sol_buffer *buffer)
{
    return sol_buffer_append_printf(buffer, "%" PRIi64, value);
}

static int
default_serialize_uint64(const struct sol_memdesc *desc, uint64_t value, struct sol_buffer *buffer)
{
    return sol_buffer_append_printf(buffer, "%" PRIu64, value);
}

static int
default_serialize_double(const struct sol_memdesc *desc, double value, struct sol_buffer *buffer)
{
    return sol_buffer_append_printf(buffer, "%g", value);
}

static int
default_serialize_bool(const struct sol_memdesc *desc, bool value, struct sol_buffer *buffer)
{
    if (value)
        return sol_buffer_append_slice(buffer, sol_str_slice_from_str("true"));
    else
        return sol_buffer_append_slice(buffer, sol_str_slice_from_str("false"));
}

static int
default_serialize_pointer(const struct sol_memdesc *desc, const void *value, struct sol_buffer *buffer)
{
    return sol_buffer_append_printf(buffer, "%p", value);
}

static int
default_serialize_string(const struct sol_memdesc *desc, const char *value, struct sol_buffer *buffer)
{
    const char *last = value;
    const char *itr;
    int r;

    if (!value)
        return sol_buffer_append_slice(buffer, sol_str_slice_from_str("NULL"));

    r = sol_buffer_append_char(buffer, '"');
    if (r < 0)
        return r;

    for (itr = value; *itr != '\0'; itr++) {
        if (!isprint((uint8_t)*itr) || *itr == '"') {
            r = sol_buffer_append_bytes(buffer, (const uint8_t *)last, itr - last);
            if (r < 0)
                return r;
            last = itr + 1;

            if (*itr == '"')
                r = sol_buffer_append_slice(buffer, sol_str_slice_from_str("\\\""));
            else if (*itr == '\t')
                r = sol_buffer_append_slice(buffer, sol_str_slice_from_str("\\t"));
            else if (*itr == '\n')
                r = sol_buffer_append_slice(buffer, sol_str_slice_from_str("\\n"));
            else if (*itr == '\r')
                r = sol_buffer_append_slice(buffer, sol_str_slice_from_str("\\r"));
            else if (*itr == '\f')
                r = sol_buffer_append_slice(buffer, sol_str_slice_from_str("\\f"));
            else if (*itr == '\v')
                r = sol_buffer_append_slice(buffer, sol_str_slice_from_str("\\v"));
            else
                r = sol_buffer_append_printf(buffer, "\\x%x", *itr);
            if (r < 0)
                return r;
        }
    }

    if (last < itr) {
        r = sol_buffer_append_bytes(buffer, (const uint8_t *)last, itr - last);
        if (r < 0)
            return r;
    }

    return sol_buffer_append_char(buffer, '"');
}

static int
default_serialize_enumeration(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buffer)
{
    const char *string = sol_memdesc_enumeration_to_str(desc, memory);

    if (string)
        return sol_buffer_append_slice(buffer, sol_str_slice_from_str(string));

    return sol_buffer_append_printf(buffer, "%" PRIi64, sol_memdesc_get_as_int64(desc, memory));
}

static int
default_serialize_structure_member_key(const struct sol_memdesc_structure_member *member, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix)
{
    int r;

    r = serialize_indent(buf, prefix, opts->structure.key.indent);
    if (r < 0)
        return r;

    if (opts->structure.key.start.len) {
        r = sol_buffer_append_slice(buf, opts->structure.key.start);
        if (r < 0)
            return r;
    }

    r = sol_buffer_append_slice(buf, sol_str_slice_from_str(member->name));
    if (r < 0)
        return r;

    if (opts->structure.key.end.len) {
        r = sol_buffer_append_slice(buf, opts->structure.key.end);
        if (r < 0)
            return r;
    }

    return 0;
}

#ifdef SOL_MEMDESC_DESCRIPTION
static int
default_serialize_structure_member_description(const struct sol_memdesc_structure_member *member, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix)
{
    int r;

    if (!member->description)
        return 0;

    r = serialize_indent(buf, prefix, opts->structure.description.indent);
    if (r < 0)
        return r;

    if (opts->structure.description.start.len) {
        r = sol_buffer_append_slice(buf, opts->structure.description.start);
        if (r < 0)
            return r;
    }

    r = sol_buffer_append_slice(buf, sol_str_slice_from_str(member->description));
    if (r < 0)
        return r;

    if (opts->structure.description.end.len) {
        r = sol_buffer_append_slice(buf, opts->structure.description.end);
        if (r < 0)
            return r;
    }

    return serialize_deindent(buf, prefix, opts->structure.description.indent, false);
}
#endif

static int
default_serialize_structure_member(const struct sol_memdesc *structure, const struct sol_memdesc_structure_member *member, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix, bool is_first)
{
    int r;

    if (!is_first) {
        if (opts->structure.separator.len) {
            r = sol_buffer_append_slice(buf, opts->structure.separator);
            if (r < 0)
                return r;
        }
    }

    if (opts->structure.show_key) {
        r = default_serialize_structure_member_key(member, buf, opts, prefix);
        if (r < 0)
            return r;
    }

    r = serialize_indent(buf, prefix, opts->structure.value.indent);
    if (r < 0)
        return r;

    if (opts->structure.value.start.len) {
        r = sol_buffer_append_slice(buf, opts->structure.value.start);
        if (r < 0)
            return r;
    }

    r = sol_memdesc_serialize(&member->base, memory, buf, opts, prefix);
    if (r < 0)
        return r;

    if (opts->structure.value.end.len) {
        r = sol_buffer_append_slice(buf, opts->structure.value.end);
        if (r < 0)
            return r;
    }

#ifdef SOL_MEMDESC_DESCRIPTION
    if (opts->structure.show_description) {
        r = default_serialize_structure_member_description(member, buf, opts, prefix);
        if (r < 0)
            return r;
    }
#endif

    r = serialize_deindent(buf, prefix, opts->structure.value.indent, false);
    if (r < 0)
        return r;

    if (opts->structure.show_key)
        return serialize_deindent(buf, prefix, opts->structure.key.indent, false);
    return 0;
}

static int
default_serialize_array_item_index(size_t idx, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix)
{
    int r;

    r = serialize_indent(buf, prefix, opts->array.index.indent);
    if (r < 0)
        return r;

    if (opts->array.index.start.len) {
        r = sol_buffer_append_slice(buf, opts->array.index.start);
        if (r < 0)
            return r;
    }

    r = sol_buffer_append_printf(buf, "%zu", idx);
    if (r < 0)
        return r;

    if (opts->array.index.end.len) {
        r = sol_buffer_append_slice(buf, opts->array.index.end);
        if (r < 0)
            return r;
    }

    return 0;
}

static int
default_serialize_array_item(const struct sol_memdesc *desc, size_t idx, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix)
{
    int r;

    if (idx > 0) {
        if (opts->array.separator.len) {
            r = sol_buffer_append_slice(buf, opts->array.separator);
            if (r < 0)
                return r;
        }
    }

    if (opts->array.show_index) {
        r = default_serialize_array_item_index(idx, buf, opts, prefix);
        if (r < 0)
            return r;
    }

    r = serialize_indent(buf, prefix, opts->array.value.indent);
    if (r < 0)
        return r;

    if (opts->array.value.start.len) {
        r = sol_buffer_append_slice(buf, opts->array.value.start);
        if (r < 0)
            return r;
    }

    r = sol_memdesc_serialize(desc->array_item, memory, buf, opts, prefix);
    if (r < 0)
        return r;

    if (opts->array.value.end.len) {
        r = sol_buffer_append_slice(buf, opts->array.value.end);
        if (r < 0)
            return r;
    }

    r = serialize_deindent(buf, prefix, opts->array.value.indent, false);
    if (r < 0)
        return r;

    if (opts->array.show_index)
        return serialize_deindent(buf, prefix, opts->array.index.indent, false);
    return 0;
}

SOL_API const struct sol_memdesc_serialize_options SOL_MEMDESC_SERIALIZE_OPTIONS_DEFAULT = {
    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_SERIALIZE_OPTIONS_API_VERSION, )
    .serialize_int64 = default_serialize_int64,
    .serialize_uint64 = default_serialize_uint64,
    .serialize_double = default_serialize_double,
    .serialize_bool = default_serialize_bool,
    .serialize_pointer = default_serialize_pointer,
    .serialize_string = default_serialize_string,
    .serialize_enumeration = default_serialize_enumeration,
    .serialize_structure_member = default_serialize_structure_member,
    .serialize_array_item = default_serialize_array_item,
    .structure = {
        .container = {
            .start = SOL_STR_SLICE_LITERAL("{\n"),
            .end = SOL_STR_SLICE_LITERAL("}"),
        },
        .key = {
            .start = SOL_STR_SLICE_LITERAL("."),
            .end = SOL_STR_SLICE_LITERAL(" = "),
            .indent = SOL_STR_SLICE_LITERAL("    "),
        },
        SOL_MEMDESC_SET_DESCRIPTION(.description = {
            .start = SOL_STR_SLICE_LITERAL(" /* "),
            .end = SOL_STR_SLICE_LITERAL(" */"),
        }, )
        .separator = SOL_STR_SLICE_LITERAL(",\n"),
        .show_key = true,
        .detailed = true,
        SOL_MEMDESC_SET_DESCRIPTION(.show_description = true, )
    },
    .array = {
        .container = {
            .start = SOL_STR_SLICE_LITERAL("{\n"),
            .end = SOL_STR_SLICE_LITERAL("}"),
        },
        .index = {
            .start = SOL_STR_SLICE_LITERAL("["),
            .end = SOL_STR_SLICE_LITERAL("] = "),
            .indent = SOL_STR_SLICE_LITERAL("    "),
        },
        .separator = SOL_STR_SLICE_LITERAL(",\n"),
        .show_index = true,
    },
};

static int
serialize_bool(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts)
{
    const bool *m = memory;

    if (opts->serialize_bool)
        return opts->serialize_bool(desc, *m, buf);
    else
        return default_serialize_bool(desc, *m, buf);
}

static int
serialize_double(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts)
{
    const double *m = memory;

    if (opts->serialize_double)
        return opts->serialize_double(desc, *m, buf);
    else
        return default_serialize_double(desc, *m, buf);
}

static int
serialize_int64(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts)
{
    int64_t m = sol_memdesc_get_as_int64(desc, memory);

    if (opts->serialize_int64)
        return opts->serialize_int64(desc, m, buf);
    else
        return default_serialize_int64(desc, m, buf);
}

static int
serialize_uint64(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts)
{
    uint64_t m = sol_memdesc_get_as_uint64(desc, memory);

    if (opts->serialize_uint64)
        return opts->serialize_uint64(desc, m, buf);
    else
        return default_serialize_uint64(desc, m, buf);
}

static int
serialize_string(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts)
{
    const char *const *m = memory;

    if (opts->serialize_string)
        return opts->serialize_string(desc, *m, buf);
    else
        return default_serialize_string(desc, *m, buf);
}

static int
serialize_enumeration(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts)
{
    if (opts->serialize_enumeration)
        return opts->serialize_enumeration(desc, memory, buf);
    else
        return default_serialize_enumeration(desc, memory, buf);
}

static int serialize(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix);

static int
serialize_pointer(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix)
{
    const void *const *m = memory;

    if (!*m || !desc->pointed_item) {
        if (opts->serialize_pointer)
            return opts->serialize_pointer(desc, *m, buf);
        else
            return default_serialize_pointer(desc, *m, buf);
    }

    CHECK_MEMDESC(desc->pointed_item, -EINVAL);
    return serialize(desc->pointed_item, *m, buf, opts, prefix);
}

static int
serialize_structure_member(const struct sol_memdesc *structure, const struct sol_memdesc_structure_member *member, const void *memory, struct sol_buffer *buffer, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix, bool is_first)
{
    if (opts->serialize_structure_member)
        return opts->serialize_structure_member(structure, member, memory, buffer, opts, prefix, is_first);
    else
        return default_serialize_structure_member(structure, member, memory, buffer, opts, prefix, is_first);
}

static int
serialize_structure(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix)
{
    const struct sol_memdesc_structure_member *itr;
    const void *itmem;
    bool is_first = true;
    int r;

    r = serialize_indent(buf, prefix, opts->structure.container.indent);
    if (r < 0)
        return r;

    if (opts->structure.container.start.len) {
        r = sol_buffer_append_slice(buf, opts->structure.container.start);
        if (r < 0)
            return r;
    }

    SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER_MEMORY(desc, itr, memory, itmem) {
        if (!opts->structure.detailed && itr->detail)
            continue;
        r = serialize_structure_member(desc, itr, itmem, buf, opts, prefix, is_first);
        if (r < 0)
            return r;
        if (is_first)
            is_first = false;
    }

    r = serialize_deindent(buf, prefix, opts->structure.container.indent, true);
    if (r < 0)
        return r;

    if (opts->structure.container.end.len) {
        r = sol_buffer_append_slice(buf, opts->structure.container.end);
        if (r < 0)
            return r;
    }

    return 0;
}

static int
serialize_array_item(const struct sol_memdesc *desc, size_t idx, const void *memory, struct sol_buffer *buffer, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix)
{
    if (opts->serialize_array_item)
        return opts->serialize_array_item(desc, idx, memory, buffer, opts, prefix);
    else
        return default_serialize_array_item(desc, idx, memory, buffer, opts, prefix);
}

static int
serialize_array(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix)
{
    const void *mem = memory;
    const void *item;
    ssize_t idx, len;
    int r;

    SOL_NULL_CHECK(mem, -EINVAL);

    len = sol_memdesc_get_array_length(desc, mem);
    if (len < 0)
        return len;

    r = serialize_indent(buf, prefix, opts->array.container.indent);
    if (r < 0)
        return r;

    if (opts->array.container.start.len) {
        r = sol_buffer_append_slice(buf, opts->array.container.start);
        if (r < 0)
            return r;
    }

    SOL_MEMDESC_FOREACH_ARRAY_ELEMENT_IN_RANGE(desc, mem, 0, len, idx, item) {
        r = serialize_array_item(desc, idx, item, buf, opts, prefix);
        if (r < 0)
            return r;
    }

    r = serialize_deindent(buf, prefix, opts->array.container.indent, true);
    if (r < 0)
        return r;

    if (opts->array.container.end.len) {
        r = sol_buffer_append_slice(buf, opts->array.container.end);
        if (r < 0)
            return r;
    }

    return 0;
}

static int
serialize(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buf, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix)
{
    if (desc->type == SOL_MEMDESC_TYPE_BOOL)
        return serialize_bool(desc, memory, buf, opts);
    else if (desc->type == SOL_MEMDESC_TYPE_DOUBLE)
        return serialize_double(desc, memory, buf, opts);
    else if (desc->type == SOL_MEMDESC_TYPE_STRING ||
        desc->type == SOL_MEMDESC_TYPE_CONST_STRING)
        return serialize_string(desc, memory, buf, opts);
    else if (desc->type == SOL_MEMDESC_TYPE_ENUMERATION)
        return serialize_enumeration(desc, memory, buf, opts);
    else if (sol_memdesc_is_unsigned_integer(desc))
        return serialize_uint64(desc, memory, buf, opts);
    else if (sol_memdesc_is_signed_integer(desc))
        return serialize_int64(desc, memory, buf, opts);
    else if (desc->type == SOL_MEMDESC_TYPE_PTR)
        return serialize_pointer(desc, memory, buf, opts, prefix);
    else if (desc->type == SOL_MEMDESC_TYPE_STRUCTURE)
        return serialize_structure(desc, memory, buf, opts, prefix);
    else if (desc->type == SOL_MEMDESC_TYPE_ARRAY)
        return serialize_array(desc, memory, buf, opts, prefix);
    else {
        SOL_WRN("unhandled type %d for desc=%p", desc->type, desc);
        return -EINVAL;
    }
}

SOL_API int
sol_memdesc_serialize(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buffer, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix)
{
    struct sol_buffer local_prefix = SOL_BUFFER_INIT_EMPTY;
    int r;

    CHECK_MEMDESC(desc, -EINVAL);
    SOL_NULL_CHECK(memory, -EINVAL);
    SOL_NULL_CHECK(buffer, -EINVAL);

    if (!opts)
        opts = &SOL_MEMDESC_SERIALIZE_OPTIONS_DEFAULT;
#ifndef SOL_NO_API_VERSION
    else {
        if (opts->api_version != SOL_MEMDESC_SERIALIZE_OPTIONS_API_VERSION) {
            SOL_WRN("opts(%p)->api_version(%" PRIu16 ") != SOL_MEMDESC_SERIALIZE_OPTIONS_API_VERSION(%" PRIu16 ")",
                opts, opts->api_version, SOL_MEMDESC_SERIALIZE_OPTIONS_API_VERSION);
            return -EINVAL;
        }
    }
#endif

    if (!prefix)
        prefix = &local_prefix;

    r = serialize(desc, memory, buffer, opts, prefix);
    sol_buffer_fini(&local_prefix);

    return r;
}
