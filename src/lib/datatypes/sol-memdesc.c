/*
 * This file is part of the Soletta Project
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
#define _CHECK_API(name, h, b, on_failure) \
    do { \
        if ((h)->api_version != (b)) { \
            SOL_WRN("%s%s%s" # h "(%p)->api_version(%" PRIu16 ") != " # b "(%" PRIu16 ")", \
                (name) ? "name='" : "", (name) ? (name) : "", (name) ? "' " : "", \
                (h), (h)->api_version, b); \
            on_failure; \
        } \
    } while (0)

#else
#define _CHECK_API(name, h, b, on_failure)
#endif

#define CHECK_API(name, h, b, ...) _CHECK_API(name, h, b, return __VA_ARGS__)
#define CHECK_API_GOTO(name, h, b, label) _CHECK_API(name, h, b, goto label)

#define VALIDATE_MEMDESC(memdesc, on_failure) \
    do { \
        _CHECK_API(NULL, memdesc, SOL_MEMDESC_API_VERSION, on_failure); \
        if (memdesc->ops) { \
            _CHECK_API(memdesc->name, memdesc->ops, SOL_MEMDESC_OPS_API_VERSION, on_failure); \
        } \
        if (memdesc->type == SOL_MEMDESC_TYPE_ARRAY || memdesc->type == SOL_MEMDESC_TYPE_STRUCTURE) { \
            if (!memdesc->size) { \
                SOL_WRN("name='%s' " # memdesc "(%p)->size cannot be zero for array or structure.", \
                    memdesc->name, memdesc); \
                on_failure; \
            } \
            if (memdesc->children) { \
                _CHECK_API(memdesc->name, memdesc->children, SOL_MEMDESC_API_VERSION, on_failure); \
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
        SOL_STR_TABLE_ITEM("boolean", SOL_MEMDESC_TYPE_BOOLEAN),
        SOL_STR_TABLE_ITEM("double", SOL_MEMDESC_TYPE_DOUBLE),
        SOL_STR_TABLE_ITEM("string", SOL_MEMDESC_TYPE_STRING),
        SOL_STR_TABLE_ITEM("const string", SOL_MEMDESC_TYPE_CONST_STRING),
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
        [SOL_MEMDESC_TYPE_BOOLEAN] = "boolean",
        [SOL_MEMDESC_TYPE_DOUBLE] = "double",
        [SOL_MEMDESC_TYPE_STRING] = "string",
        [SOL_MEMDESC_TYPE_CONST_STRING] = "const string",
        [SOL_MEMDESC_TYPE_PTR] = "pointer",
        [SOL_MEMDESC_TYPE_STRUCTURE] = "structure",
        [SOL_MEMDESC_TYPE_ARRAY] = "array",
    };

    if (type < SOL_UTIL_ARRAY_SIZE(strs))
        return strs[type];

    return NULL;
}

static int copy_structure(const struct sol_memdesc *desc, void *container, const void *ptr_content);
static int copy_array(const struct sol_memdesc *desc, void *dst_memory, const void *src_memory);

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
    case SOL_MEMDESC_TYPE_BOOLEAN:
        return &desc->defcontent.b;
    case SOL_MEMDESC_TYPE_DOUBLE:
        return &desc->defcontent.d;
    case SOL_MEMDESC_TYPE_STRING:
    case SOL_MEMDESC_TYPE_CONST_STRING:
        return &desc->defcontent.s;
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
set_content(const struct sol_memdesc *desc, void *container, const void *ptr_content)
{
    void *mem;

    if (desc->ops && desc->ops->set_content)
        return desc->ops->set_content(desc, container, ptr_content);

    mem = sol_memdesc_get_memory(desc, container);

    if (desc->type == SOL_MEMDESC_TYPE_STRING) {
        const char *const *pv = ptr_content;
        int r = sol_util_replace_str_if_changed(mem, *pv);
        if (r >= 0)
            return 0;
        return r;
    } else if (desc->type == SOL_MEMDESC_TYPE_PTR && desc->children) {
        const void *const *pv = ptr_content;
        void **m = mem;

        if (!*m && *pv) {
            *m = sol_memdesc_new_with_defaults(desc->children);
            if (!*m)
                return -errno;
        } else if (*m && !*pv) {
            sol_memdesc_free(desc->children, *m);
            *m = NULL;
            return 0;
        } else if (!*pv)
            return 0;

        return set_content(desc->children, *m, *pv);

    } else if (desc->type == SOL_MEMDESC_TYPE_STRUCTURE) {
        if (!desc->children) {
            SOL_WRN("desc=%p (%s) is SOL_MEMDESC_TYPE_STRUCTURE but does not provide children", desc, desc->name);
            return -EINVAL;
        }

        return copy_structure(desc, mem, ptr_content);
    } else if (desc->type == SOL_MEMDESC_TYPE_ARRAY) {
        if (!desc->children) {
            SOL_WRN("desc=%p (%s) is SOL_MEMDESC_TYPE_ARRAY but does not provide children", desc, desc->name);
            return -EINVAL;
        }

        return copy_array(desc, mem, ptr_content);
    }

    memcpy(mem, ptr_content, sol_memdesc_get_size(desc));
    return 0;
}

static int
copy_content(const struct sol_memdesc *desc, const void *src_container, void *dst_container)
{
    const void *src_mem;

    if (desc->ops && desc->ops->copy)
        return desc->ops->copy(desc, src_container, dst_container);

    src_mem = sol_memdesc_get_memory(desc, src_container);
    return set_content(desc, dst_container, src_mem);
}

static int
copy_structure(const struct sol_memdesc *desc, void *container, const void *ptr_content)
{
    const struct sol_memdesc *itr;

    SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER(desc, itr) {
        int r;

        r = copy_content(itr, ptr_content, container);
        if (r < 0)
            return r;
    }

    return 0;
}

static int compare_content(const struct sol_memdesc *desc, const void *a_container, const void *b_container);

static int
compare_structure(const struct sol_memdesc *desc, const void *a_container, const void *b_container)
{
    const struct sol_memdesc *itr;

    errno = 0;
    SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER(desc, itr) {
        int r;

        r = compare_content(itr, a_container, b_container);
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

        r = compare_content(desc->children, a_item, b_item);
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

        r = set_content(desc->children, dst_item, src_item);
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
compare_content(const struct sol_memdesc *desc, const void *a_container, const void *b_container)
{
    const void *a_mem, *b_mem;

    if (desc->ops && desc->ops->compare)
        return desc->ops->compare(desc, a_container, b_container);

    a_mem = sol_memdesc_get_memory(desc, a_container);
    b_mem = sol_memdesc_get_memory(desc, b_container);

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
    case SOL_MEMDESC_TYPE_BOOLEAN: {
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

        if (sol_util_double_equal(*a, *b))
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
    case SOL_MEMDESC_TYPE_PTR: {
        const void *const *a = a_mem;
        const void *const *b = b_mem;

        if (!desc->children || !*a || !*b) {
            if (*a < *b)
                return -1;
            else if (*a > *b)
                return 1;
            else
                return 0;
        }

        return compare_content(desc->children, *a, *b);
    }
    case SOL_MEMDESC_TYPE_STRUCTURE: {
        if (!desc->children) {
            SOL_WRN("desc=%p (%s) is SOL_MEMDESC_TYPE_STRUCTURE but does not provide children", desc, desc->name);
            errno = EINVAL;
            return 0;
        }
        return compare_structure(desc, a_mem, b_mem);
    }
    case SOL_MEMDESC_TYPE_ARRAY: {
        if (!desc->children) {
            SOL_WRN("desc=%p (%s) is SOL_MEMDESC_TYPE_ARRAY but does not provide children", desc, desc->name);
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
sol_memdesc_init_defaults(const struct sol_memdesc *desc, void *container)
{
    const void *defcontent;
    void *mem;

    CHECK_MEMDESC(desc, -EINVAL);
    SOL_NULL_CHECK(container, -EINVAL);

    mem = sol_memdesc_get_memory(desc, container);
    memset(mem, 0, sol_memdesc_get_size(desc));

    if (desc->ops && desc->ops->init_defaults)
        return desc->ops->init_defaults(desc, container);

    if (desc->type == SOL_MEMDESC_TYPE_STRUCTURE) {
        const struct sol_memdesc *itr;

        if (!desc->children) {
            SOL_WRN("desc=%p (%s) is SOL_MEMDESC_TYPE_STRUCTURE but does not provide children", desc, desc->name);
            return -EINVAL;
        }

        SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER(desc, itr) {
            int r;

            r = sol_memdesc_init_defaults(itr, mem);
            if (r < 0)
                return r;
        }

        if (desc->defcontent.p)
            return set_content(desc, container, desc->defcontent.p);

        return 0;
    } else if (desc->type == SOL_MEMDESC_TYPE_ARRAY) {
        if (desc->defcontent.p)
            return set_content(desc, container, desc->defcontent.p);

        return 0;
    }

    defcontent = get_defcontent(desc);
    return set_content(desc, container, defcontent);
}

SOL_API int
sol_memdesc_copy(const struct sol_memdesc *desc, const void *src_container, void *dst_container)
{
    CHECK_MEMDESC(desc, -EINVAL);
    SOL_NULL_CHECK(src_container, -EINVAL);
    SOL_NULL_CHECK(dst_container, -EINVAL);

    return copy_content(desc, src_container, dst_container);
}

SOL_API int
sol_memdesc_set_content(const struct sol_memdesc *desc, void *container, const void *ptr_content)
{
    CHECK_MEMDESC(desc, -EINVAL);
    SOL_NULL_CHECK(container, -EINVAL);
    SOL_NULL_CHECK(ptr_content, -EINVAL);

    return set_content(desc, container, ptr_content);
}

SOL_API int
sol_memdesc_compare(const struct sol_memdesc *desc, const void *a_container, const void *b_container)
{
    errno = EINVAL;
    CHECK_MEMDESC(desc, 0);
    SOL_NULL_CHECK(a_container, 0);
    SOL_NULL_CHECK(b_container, 0);

    errno = 0;
    return compare_content(desc, a_container, b_container);
}

SOL_API int
sol_memdesc_free_content(const struct sol_memdesc *desc, void *container)
{
    void *mem;

    CHECK_MEMDESC(desc, -EINVAL);
    SOL_NULL_CHECK(container, -EINVAL);

    if (desc->ops && desc->ops->free_content)
        return desc->ops->free_content(desc, container);

    mem = sol_memdesc_get_memory(desc, container);

    if (desc->type == SOL_MEMDESC_TYPE_STRING) {
        char **m = mem;
        free(*m);
        *m = NULL;
        return 0;
    } else if (desc->type == SOL_MEMDESC_TYPE_PTR && desc->children) {
        void **m = mem;

        if (*m) {
            sol_memdesc_free(desc->children, *m);
            *m = NULL;
        }
        return 0;
    } else if (desc->type == SOL_MEMDESC_TYPE_STRUCTURE) {
        const struct sol_memdesc *itr;
        int ret = 0;

        if (!desc->children) {
            SOL_WRN("desc=%p (%s) is SOL_MEMDESC_TYPE_STRUCTURE but does not provide children", desc, desc->name);
            return -EINVAL;
        }

        SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER(desc, itr) {
            int r;

            r = sol_memdesc_free_content(itr, mem);
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
sol_memdesc_get_array_length(const struct sol_memdesc *desc, const void *memory)
{
    CHECK_MEMDESC(desc, -EINVAL);
    SOL_NULL_CHECK(memory, -EINVAL);

    if (desc->type != SOL_MEMDESC_TYPE_ARRAY) {
        SOL_WRN("desc=%p (%s) is not SOL_MEMDESC_TYPE_ARRAY", desc, desc->name);
        return -EINVAL;
    } else if (!desc->ops || !desc->ops->array.get_length) {
        SOL_WRN("desc=%p (%s) is SOL_MEMDESC_TYPE_ARRAY but does not provide ops->array.get_length", desc, desc->name);
        return -EINVAL;
    }

    return desc->ops->array.get_length(desc, memory);
}

SOL_API void *
sol_memdesc_get_array_element(const struct sol_memdesc *desc, const void *memory, size_t idx)
{
    errno = EINVAL;
    CHECK_MEMDESC(desc, NULL);
    SOL_NULL_CHECK(memory, NULL);

    if (desc->type != SOL_MEMDESC_TYPE_ARRAY) {
        SOL_WRN("desc=%p (%s) is not SOL_MEMDESC_TYPE_ARRAY", desc, desc->name);
        return NULL;
    } else if (!desc->ops || !desc->ops->array.get_element) {
        SOL_WRN("desc=%p (%s) is SOL_MEMDESC_TYPE_ARRAY but does not provide ops->array.get_element", desc, desc->name);
        return NULL;
    }

    errno = 0;
    return desc->ops->array.get_element(desc, memory, idx);
}

SOL_API int
sol_memdesc_resize_array(const struct sol_memdesc *desc, void *memory, size_t length)
{
    CHECK_MEMDESC(desc, -EINVAL);
    SOL_NULL_CHECK(memory, -EINVAL);

    if (desc->type != SOL_MEMDESC_TYPE_ARRAY) {
        SOL_WRN("desc=%p (%s) is not SOL_MEMDESC_TYPE_ARRAY", desc, desc->name);
        return -EINVAL;
    } else if (!desc->ops || !desc->ops->array.resize) {
        SOL_WRN("desc=%p (%s) is SOL_MEMDESC_TYPE_ARRAY but does not provide ops->array.resize", desc, desc->name);
        return -EINVAL;
    }

    return desc->ops->array.resize(desc, memory, length);
}

static int
vector_ops_init_defaults(const struct sol_memdesc *desc, void *container)
{
    struct sol_vector *v = sol_memdesc_get_memory(desc, container);
    uint16_t child_size = sol_memdesc_get_size(desc->children);

    SOL_INT_CHECK(child_size, == 0, -EINVAL);
    SOL_INT_CHECK(desc->children->offset, != 0, -EINVAL);
    SOL_INT_CHECK(desc->size, != sizeof(struct sol_vector), -EINVAL);

    sol_vector_init(v, child_size);

    if (desc->defcontent.p)
        return sol_memdesc_set_content(desc, container, desc->defcontent.p);

    return 0;
}

static ssize_t
vector_ops_get_array_length(const struct sol_memdesc *desc, const void *memory)
{
    const struct sol_vector *v = memory;

    return v->len;
}

static void *
vector_ops_get_array_element(const struct sol_memdesc *desc, const void *memory, size_t idx)
{
    const struct sol_vector *v = memory;

    errno = ERANGE;
    SOL_INT_CHECK(idx, > UINT16_MAX, NULL);
    errno = 0;

    return sol_vector_get(v, idx);
}

static int
vector_ops_resize_array(const struct sol_memdesc *desc, void *memory, size_t len)
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

        if (sol_memdesc_get_size(desc->children)) {
            uint16_t idx;

            for (idx = oldlen; idx < len; idx++) {
                void *itmem;
                int r;

                itmem = sol_vector_get_nocheck(v, idx);
                r = sol_memdesc_init_defaults(desc->children, itmem);
                if (r < 0) {
                    sol_vector_del_range(v, idx, len - idx);
                    return r;
                }
            }
        }

        return 0;
    } else {
        if (sol_memdesc_get_size(desc->children)) {
            uint16_t idx;

            for (idx = len; idx < oldlen; idx++) {
                void *itmem;

                itmem = sol_vector_get_nocheck(v, idx);
                sol_memdesc_free_content(desc->children, itmem);
            }
        }

        return sol_vector_del_range(v, len, oldlen - len);
    }
}

SOL_API const struct sol_memdesc_ops SOL_MEMDESC_OPS_VECTOR = {
    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_OPS_API_VERSION, )
    .init_defaults = vector_ops_init_defaults,
    .array = {
        .get_length = vector_ops_get_array_length,
        .get_element = vector_ops_get_array_element,
        .resize = vector_ops_resize_array,
    },
};


static int
ptr_vector_ops_init_defaults(const struct sol_memdesc *desc, void *container)
{
    struct sol_ptr_vector *v = sol_memdesc_get_memory(desc, container);

    if (desc->size != sizeof(struct sol_ptr_vector))
        return -EINVAL;

    if (desc->children && sol_memdesc_get_size(desc->children) != sizeof(void *))
        return -EINVAL;

    sol_ptr_vector_init(v);

    if (desc->defcontent.p)
        return sol_memdesc_set_content(desc, container, desc->defcontent.p);

    return 0;
}

SOL_API const struct sol_memdesc_ops SOL_MEMDESC_OPS_PTR_VECTOR = {
    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_OPS_API_VERSION, )
    .init_defaults = ptr_vector_ops_init_defaults,
    .array = {
        .get_length = vector_ops_get_array_length,
        .get_element = vector_ops_get_array_element,
        .resize = vector_ops_resize_array,
    },
};
