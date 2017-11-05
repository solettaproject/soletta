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

#pragma once

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <sol-common-buildopts.h>
#include <sol-str-slice.h>
#include <sol-str-table.h>
#include <sol-macros.h>
#include <sol-buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These are routines that Soletta provides for its memory description (memdesc) implementation.
 */

/**
 * @defgroup MemDesc Memory Description
 * @ingroup Datatypes
 *
 * @brief A memory description (memdesc) allows code to know how to
 * handle it in runtime, such as decode/parse from some other
 * representation (text/json), or serialize/encode. It will, as well,
 * offer special handling such as memory being duplicated and freed
 * for strings, or defined per-description with
 * struct sol_memdesc::ops.
 *
 * @{
 */

struct sol_memdesc;

/**
 * @brief Designates the type of the memory description
 */
enum sol_memdesc_type {
    SOL_MEMDESC_TYPE_UNKNOWN = 0, /**< @brief not to be used. */
    /**
     * @brief uint8_t equivalent (one unsigned byte).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::u8.
     */
    SOL_MEMDESC_TYPE_UINT8,
    /**
     * @brief uint16_t equivalent (two unsigned bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::u16.
     */
    SOL_MEMDESC_TYPE_UINT16,
    /**
     * @brief uint32_t equivalent (four unsigned bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::u32.
     */
    SOL_MEMDESC_TYPE_UINT32,
    /**
     * @brief uint64_t equivalent (four unsigned bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::u64.
     */
    SOL_MEMDESC_TYPE_UINT64,
    /**
     * @brief unsigned long equivalent.
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::ul.
     */
    SOL_MEMDESC_TYPE_ULONG,
    /**
     * @brief size_t equivalent (four or eight unsigned bytes, depends on platform).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::sz.
     */
    SOL_MEMDESC_TYPE_SIZE,
    /**
     * @brief int8_t equivalent (one signed byte).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::i8.
     */
    SOL_MEMDESC_TYPE_INT8,
    /**
     * @brief int16_t equivalent (two signed bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::i16.
     */
    SOL_MEMDESC_TYPE_INT16,
    /**
     * @brief int32_t equivalent (four signed bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::i32.
     */
    SOL_MEMDESC_TYPE_INT32,
    /**
     * @brief int64_t equivalent (eight signed bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::i64.
     */
    SOL_MEMDESC_TYPE_INT64,
    /**
     * @brief signed long equivalent.
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::l.
     */
    SOL_MEMDESC_TYPE_LONG,
    /**
     * @brief ssize_t equivalent (four or eight signed bytes, depends on platform).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::ssz.
     */
    SOL_MEMDESC_TYPE_SSIZE,
    /**
     * @brief boolean equivalent.
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::b.
     */
    SOL_MEMDESC_TYPE_BOOL,
    /**
     * @brief double precision floating point equivalent.
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::d.
     */
    SOL_MEMDESC_TYPE_DOUBLE,
    /**
     * @brief null-terminated C-string (@c char*).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::s. It may be null.
     *
     * By default, strings are duplicated and freed as required.
     * @see SOL_MEMDESC_TYPE_CONST_STRING
     */
    SOL_MEMDESC_TYPE_STRING,
    /**
     * @brief null-terminated C-string (@c const char*).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::s. It may be null.
     *
     * By default, strings are NOT duplicated neither freed.
     * @see SOL_MEMDESC_TYPE_STRING
     */
    SOL_MEMDESC_TYPE_CONST_STRING,
    /**
     * @brief enumeration
     *
     * Enumerations assign an integer to some symbol, then we offer a
     * translation table in struct sol_memdesc::enumeration_mapping
     * using struct sol_str_table_int64. If that is not enough, then
     * provide your own
     * struct sol_memdesc::ops::enumeration::from_str and
     * struct sol_memdesc::ops::enumeration::to_str, these will
     * receive the actual pointer to memory and thus can work with any
     * precision.
     *
     * Since enumerations don' t have an implicit size, one @b must
     * define struct sol_memdesc::size, which is limited to 64-bits (8
     * bytes).
     *
     * By default the value is based on
     * struct sol_memdesc::defcontent::e (64-bit signed integer).
     * One can change the behavior by setting a custom
     * struct sol_memdesc::ops::init_defaults.
     */
    SOL_MEMDESC_TYPE_ENUMERATION,
    /**
     * @brief generic pointer (void *).
     *
     * If struct sol_memdesc::pointed_item is non-NULL, it will be
     * managed as such (malloc/free). Note that the initial value is
     * still defined as a pointer to the actual contents in struct
     * sol_memdesc::defcontent::p. If that is non-NULL, then the
     * pointer is allocated and that one will use defaults specified
     * in struct sol_memdesc::pointed_item::defcontent, then values
     * from struct sol_memdesc::defcontent::p is applied on top.
     *
     * By default the value is based on
     * struct sol_memdesc::defcontent::p.
     *
     * @see SOL_MEMDESC_TYPE_STRUCTURE
     */
    SOL_MEMDESC_TYPE_PTR,
    /**
     * @brief structure with internal members.
     *
     * This is a recursive type with children described in struct
     * sol_memdesc::structure_members, an array that is
     * null-terminated (all element members are zeroed).
     *
     * During initialization, each member will be considered according
     * to its initial value. Then, if
     * struct sol_memdesc::defcontent::p is non-NULL, it will be
     * applied on top.
     */
    SOL_MEMDESC_TYPE_STRUCTURE,
    /**
     * @brief an array with internal members.
     *
     * This is a pointer to an array of items that are defined in
     * struct sol_memdesc::array_item. It will not be touched, you
     * should manage it yourself with struct sol_memdesc::ops.
     *
     * To map a struct sol_vector, use
     * @c .size=sizeof(struct sol_vector),
     * And provide a @c .array_item with the description on what is to
     * be in the element, like a structure or a pointer to one, this
     * way sol_memdesc_init_defaults() will set
     * struct sol_vector::elem_size to size of array item.
     * Then you must provide the following struct sol_memdesc::ops:
     *
     *  @li @c init_defaults: set @c elem_size from
     *      @c sol_memdesc_get_size(desc->array_item).
     *  @li @c array.get_length: return @c len.
     *  @li @c array.get_element: proxy return of sol_vector_get().
     *  @li @c array.resize: if shrinking, remember to call
     *      @c sol_memdesc_free_content(desc->array_item, it) for
     *      every item that will be removed, then call
     *      sol_vector_del_range(). If growing, call
     *      sol_vector_append_n() and initialze items with
     *      @c sol_memdesc_init_defaults(desc->array_item, it).
     *
     * @see SOL_MEMDESC_OPS_VECTOR and SOL_MEMDESC_OPS_PTR_VECTOR.
     */
    SOL_MEMDESC_TYPE_ARRAY
};

/**
 * @brief Converts a Memdesc Type from string to sol_memdesc_type.
 *
 * @param str the string representing a valid type.
 * @return the type or SOL_MEMDESC_TYPE_UNKNOWN if invalid.
 */
enum sol_memdesc_type sol_memdesc_type_from_str(const char *str)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts a sol_memdesc_type to a string.
 *
 * @param type the type to be converted.
 * @return the string or NULL, if the type is invalid.
 */
const char *sol_memdesc_type_to_str(enum sol_memdesc_type type)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @def SOL_MEMDESC_DESCRIPTION
 *
 * This is selected at compile time to allow reducing binary size if
 * this cpp symbol is undefined.
 */
#ifndef SOL_MEMDESC_DESCRIPTION
/* keep doxygen happy */
#define SOL_MEMDESC_DESCRIPTION
#undef SOL_MEMDESC_DESCRIPTION
#endif

/**
 * @def SOL_MEMDESC_SET_DESCRIPTION()
 *
 * Helper to set the description member of struct sol_memdesc if that
 * is available (conditional to #SOL_MEMDESC_SET_DESCRIPTION).
 *
 * It takes C statements and conditionally uses or eliminates them.
 */
#ifdef SOL_MEMDESC_DESCRIPTION
#define SOL_MEMDESC_SET_DESCRIPTION(...) __VA_ARGS__
#else
#define SOL_MEMDESC_SET_DESCRIPTION(...)
#endif

#ifndef SOL_NO_API_VERSION
/**
 * @brief the SOL_MEMDESC_API_VERSION this soletta build used.
 *
 * This symbol is defined by soletta to match SOL_MEMDESC_API_VERSION,
 * but unlike that macro this symbol will be relative to soletta build
 * and is used in our macros and static-inline functions that must
 * check for valid handles.
 */
extern const uint16_t SOL_MEMDESC_API_VERSION_COMPILED;
#endif

/**
 * @brief Operations specific to SOL_MEMDESC_TYPE_ARRAY.
 *
 * This provides array-specific operations to use when dealing with a
 * memory description.
 *
 * @see struct sol_memdesc_ops
 * @see struct sol_memdesc
 */
typedef struct sol_memdesc_ops_array {
#ifndef SOL_NO_API_VERSION
#define SOL_MEMDESC_OPS_ARRAY_API_VERSION (1) /**< API version to use in struct sol_memdesc_ops_array::api_version */
    uint16_t api_version; /**< @brief API version, must match SOL_MEMDESC_OPS_ARRAY_API_VERSION at runtime */
#endif
    /**
     * @brief calculate array length.
     *
     * Will be used to calculate the array
     * length. Return should be number of items, each defined in
     * struct sol_memdesc::array_item.
     *
     * @note must be provided if type is SOL_MEMDESC_TYPE_ARRAY.
     *
     * On error, negative errno is returned.
     *
     * @see sol_memdesc_get_array_length()
     */
    ssize_t (*get_length)(const struct sol_memdesc *desc, const void *memory);
    /**
     * @brief get memory of the given array item.
     *
     * Will be used to get the array element by its index.
     * Return should be the memory pointer or NULL on error (then set errno accordingly).
     *
     * @note must be provided if type is SOL_MEMDESC_TYPE_ARRAY.
     *
     * @see sol_memdesc_get_array_element()
     */
    void *(*get_element)(const struct sol_memdesc *desc, const void *memory, size_t idx);
    /**
     * @brief resize array length.
     *
     * Will be used to resize the array
     * length. The given size should be number of items, each defined in
     * struct sol_memdesc::array_item.
     *
     * When implementing, always remember to free the items that
     * are not needed anymore when the new length is smaller than
     * the old. Failing to do so will lead to memory leaks.
     *
     * @note must be provided if type is SOL_MEMDESC_TYPE_ARRAY.
     *
     * On error, negative errno is returned.
     *
     * @see sol_memdesc_resize_array()
     */
    int (*resize)(const struct sol_memdesc *desc, void *memory, size_t length);
} sol_memdesc_ops_array;

/**
 * @brief Operations specific to SOL_MEMDESC_TYPE_ENUMERATION.
 *
 * This provides enumeration-specific operations to use when dealing with a
 * memory description.
 *
 * @see struct sol_memdesc_ops
 * @see struct sol_memdesc
 */
typedef struct sol_memdesc_ops_enumeration {
#ifndef SOL_NO_API_VERSION
#define SOL_MEMDESC_OPS_ENUMERATION_API_VERSION (1) /**< API version to use in struct sol_memdesc_ops_enumeration::api_version */
    uint16_t api_version; /**< @brief API version, must match SOL_MEMDESC_OPS_ENUMERATION_API_VERSION at runtime */
#endif
    /**
     * @brief convert enumeration value to string.
     *
     * On error, NULL should be returned and errno set. On success non-NULL is returned.
     *
     * @see sol_memdesc_enumeration_to_str()
     */
    const char *(*to_str)(const struct sol_memdesc *desc, const void *memory);
    /**
     * @brief convert enumeration value from string.
     *
     * The return is stored in @c ptr_return, which must be the size stated in
     * struct sol_memdesc::size as returned by sol_memdesc_get_size().
     *
     * The string is given in the form of a slice so it doesn't need
     * to be null-terminated.
     *
     * On error, negative errno is returned. 0 on success.
     *
     * @see sol_memdesc_enumeration_from_str()
     */
    int (*from_str)(const struct sol_memdesc *desc, void *ptr_return, const struct sol_str_slice str);
} sol_memdesc_ops_enumeration;

/**
 * @brief override operations to be used in this memory description.
 *
 * By default the operations will be done in a fixed way unless
 * overriden by an @c ops structure, this may be used to correlate
 * members in a structure, such as struct sol_vector where length
 * is a member and the contents is another, with element_size
 * being specified in yet-another. Then things like "copy" will
 * not be a simple copy of each member.
 *
 * To map struct sol_vector, use SOL_MEMDESC_OPS_VECTOR. to map
 * struct sol_ptr_vector use SOL_MEMDESC_OPS_PTR_VECTOR.
 */
typedef struct sol_memdesc_ops {
#ifndef SOL_NO_API_VERSION
#define SOL_MEMDESC_OPS_API_VERSION (1) /**< API version to use in struct sol_memdesc_ops::api_version */
    uint16_t api_version; /**< @brief API version, must match SOL_MEMDESC_OPS_API_VERSION at runtime */
#endif
    /**
     * @brief initialize the defaults of memory.
     *
     * If provided, will be used to initialize the memory instead of
     * the traditional use of struct sol_memdesc::defcontent.
     *
     * Should return 0 on success, negative errno on errors.
     *
     * @see sol_memdesc_init_defaults()
     */
    int (*init_defaults)(const struct sol_memdesc *desc, void *memory);
    /**
     * @brief sets the content of a memory.
     *
     * If provided, will be used to set the memory instead of the
     * traditional code that will, for example, strdup() and free()
     * strings.
     *
     * The parameter @c ptr_content is a pointer to the actual content,
     * depends on the actual type. If a SOL_MEMDESC_TYPE_BOOL, for
     * example, it must be a @c bool*. For SOL_MEMDESC_TYPE_ENUMERATION,
     * the pointer will be memcpy() using the given sol_memdesc_get_size().
     *
     * Should return 0 on success, negative errno on errors.
     *
     * @see sol_memdesc_set_content()
     */
    int (*set_content)(const struct sol_memdesc *desc, void *memory, const void *ptr_content);
    /**
     * @brief copy the content from another memory.
     *
     * If provided, will be used to set the memory instead of the
     * traditional code that will, for example, strdup() and free()
     * strings.
     *
     * Should return 0 on success, negative errno on errors.
     *
     * @see sol_memdesc_copy()
     */
    int (*copy)(const struct sol_memdesc *desc, const void *src_memory, void *dst_memory);
    /**
     * @brief compare the content of two memories.
     *
     * If provided, will be used to compare the memory contents
     * instead of the traditional code that will, for example,
     * call strcmp() on strings.
     *
     * Should return 0 for equal, <0 if a_memory is smaller, >0 if b_memory is smaller.
     * On error, return 0 and set errno.
     *
     * @see sol_memdesc_compare()
     */
    int (*compare)(const struct sol_memdesc *desc, const void *a_memory, const void *b_memory);
    /**
     * @brief free the contents (internal memory) of a memory.
     *
     * If provided, will be used to free the contents of a memory
     * instead of the traditional code that will, for example, free()
     * strings.
     *
     * Should return 0 on success, negative errno on errors.
     *
     * @see sol_memdesc_free_content()
     */
    int (*free_content)(const struct sol_memdesc *desc, void *memory);
    union {
        const struct sol_memdesc_ops_array *array;
        const struct sol_memdesc_ops_enumeration *enumeration;
    };
} sol_memdesc_ops;

/**
 * @brief Data type to describe a memory region.
 */
typedef struct sol_memdesc {
#ifndef SOL_NO_API_VERSION
#define SOL_MEMDESC_API_VERSION (1) /**< @brief API version to use in struct sol_memdesc::api_version */
    uint16_t api_version; /**< @brief API version, must match SOL_MEMDESC_API_VERSION at runtime */
#endif
    /**
     * @brief size in bytes of the member memory.
     *
     * Usually this is @c sizeof(type), if a structure it will account
     * for all members plus paddings.
     *
     * This is only used for SOL_MEMDESC_TYPE_STRUCTURE and
     * SOL_MEMDESC_TYPE_ARRAY.
     */
    uint16_t size;
    /**
     * @brief basic type of the member memory.
     *
     * All handling of the memory depends on how it is to be
     * accessed. Like integers will have sign or not and a number of
     * bits. Strings will be duplicated with @c strdup() and then
     * released with @c free().
     */
    enum sol_memdesc_type type;
    /**
     * @brief default contents to be used if @c required == false.
     *
     * If struct sol_memdesc::required is false, then this content
     * can be used to provide defaults.
     *
     * Note that complex types SOL_MEMDESC_TYPE_STRUCTURE,
     * SOL_MEMDESC_TYPE_ARRAY and SOL_MEMDESC_TYPE_PTR have their
     * own handling with
     * struct sol_memdesc::structure_members,
     * struct sol_memdesc::array_item,
     * struct sol_memdesc::pointed_item.
     */
    union {
        uint8_t u8; /**< @brief use when SOL_MEMDESC_TYPE_UINT8 */
        uint16_t u16; /**< @brief use when SOL_MEMDESC_TYPE_UINT16 */
        uint32_t u32; /**< @brief use when SOL_MEMDESC_TYPE_UINT32 */
        uint64_t u64; /**< @brief use when SOL_MEMDESC_TYPE_UINT64 */
        unsigned long ul; /**< @brief use when SOL_MEMDESC_TYPE_ULONG */
        size_t sz; /**< @brief use when SOL_MEMDESC_TYPE_SIZE */
        int8_t i8; /**< @brief use when SOL_MEMDESC_TYPE_INT8 */
        int16_t i16; /**< @brief use when SOL_MEMDESC_TYPE_INT16 */
        int32_t i32; /**< @brief use when SOL_MEMDESC_TYPE_INT32 */
        int64_t i64; /**< @brief use when SOL_MEMDESC_TYPE_INT64 */
        long l; /**< @brief use when SOL_MEMDESC_TYPE_LONG */
        ssize_t ssz; /**< @brief use when SOL_MEMDESC_TYPE_SSIZE */
        bool b; /**< @brief use when SOL_MEMDESC_TYPE_BOOL */
        double d; /**< @brief use when SOL_MEMDESC_TYPE_DOUBLE */
        int64_t e; /**< @brief use when SOL_MEMDESC_TYPE_ENUMERATION */
        const char *s; /**< @brief use when SOL_MEMDESC_TYPE_STRING or SOL_MEMDESC_TYPE_CONST_STRING */
        const void *p; /**< @brief use when SOL_MEMDESC_TYPE_PTR, SOL_MEMDESC_TYPE_STRUCTURE or SOL_MEMDESC_TYPE_ARRAY */
    } defcontent;
    /**
     * @brief how to access complex types (structures and arrays).
     *
     * If the memory is complex, use a recursive description
     * specified here.
     */
    union {
        /**
         * @brief Type of a memory pointer.
         *
         * Only to be used in SOL_MEMDESC_TYPE_PTR
         */
        const struct sol_memdesc *pointed_item;
        /**
         * @brief Type of array item.
         *
         * Only to be used in SOL_MEMDESC_TYPE_ARRAY.
         */
        const struct sol_memdesc *array_item;
        /**
         * @brief null-terminated array of structure members.
         *
         * Only to be used in SOL_MEMDESC_TYPE_STRUCTURE.
         *
         * Loops should stop when type is SOL_MEMDESC_TYPE_UNKNOWN (0).
         */
        const struct sol_memdesc_structure_member *structure_members;
        /**
         * @brief null-terminated array of struct sol_str_table.
         *
         * Only to be used in SOL_MEMDESC_TYPE_ENUMERATION.
         *
         */
        const struct sol_str_table_int64 *enumeration_mapping;
    };

    /**
     * @brief Override operations to use when operating on the memory.
     *
     * May be NULL to use the default operations.
     */
    const struct sol_memdesc_ops *ops;
} sol_memdesc;

/**
 * @brief Description of a structure member.
 *
 * This description extends the base description and adds name, offset
 * and some flags.
 *
 * @see struct sol_memdesc
 */
typedef struct sol_memdesc_structure_member {
    struct sol_memdesc base;
    /**
     * @brief memory name, such as the member name in a structure.
     *
     * This may be used in serialization and parsing to provide a
     * descriptive identifier.
     */
    const char *name;
#ifdef SOL_MEMDESC_DESCRIPTION
    /**
     * @brief long description of the memory
     *
     * This may be used while presenting information to the user on
     * what's the purpose of the memory.
     *
     * It only exist if #SOL_MEMDESC_DESCRIPTION is defined, allowing
     * for footprint savings in constrained systems.
     */
    const char *description;
#endif
    /**
     * @brief offset in bytes relative to containing structure memory.
     *
     * If this is a member of a structure, then it's the
     * @c offsetof(struct, member). It is used to access the actual
     * memory.
     */
    uint16_t offset;
    /**
     * @brief whenever member is mandatory in serialization and parsing.
     *
     * If false, must exist when serializing/parsing. if true, then
     * defcontent could be used if missing from input.
     */
    bool optional : 1;
    /**
     * @brief whenever member is extended detail.
     *
     * If true, should only be included in serialization if detail is
     * wanted.
     */
    bool detail : 1;
} sol_memdesc_structure_member;

/**
 * @brief operations to handle struct sol_vector.
 *
 * If one wants to use SOL_MEMDESC_TYPE_ARRAY with a
 * struct sol_vector, then use this operations to
 * initialize, get length, get element and resize the array.
 */
extern const struct sol_memdesc_ops SOL_MEMDESC_OPS_VECTOR;
/**
 * @brief operations to handle struct sol_ptr_vector.
 *
 * If one wants to use SOL_MEMDESC_TYPE_ARRAY with a
 * struct sol_ptr_vector, then use this operations to
 * initialize, get length, get element and resize the array.
 */
extern const struct sol_memdesc_ops SOL_MEMDESC_OPS_PTR_VECTOR;

/**
 * @brief get the size in bytes of the memory description.
 *
 * This will use the intrinsic size of each type and for
 * SOL_MEMDESC_TYPE_STRUCTURE and SOL_MEMDESC_TYPE_ARRAY it will use
 * the explicit one at
 * struct sol_memdesc::size.
 *
 * @param desc the memory description.
 *
 * @return @c 0 on errors (and errno is set to EINVAL) or the size in bytes.
 */
static inline uint16_t
sol_memdesc_get_size(const struct sol_memdesc *desc)
{
    errno = EINVAL;
    if (!desc)
        return 0;

#ifndef SOL_NO_API_VERSION
    if (desc->api_version != SOL_MEMDESC_API_VERSION_COMPILED)
        return 0;
#endif

    errno = 0;
    switch (desc->type) {
    case SOL_MEMDESC_TYPE_UINT8:
        return sizeof(uint8_t);
    case SOL_MEMDESC_TYPE_UINT16:
        return sizeof(uint16_t);
    case SOL_MEMDESC_TYPE_UINT32:
        return sizeof(uint32_t);
    case SOL_MEMDESC_TYPE_UINT64:
        return sizeof(uint64_t);
    case SOL_MEMDESC_TYPE_ULONG:
        return sizeof(unsigned long);
    case SOL_MEMDESC_TYPE_SIZE:
        return sizeof(size_t);
    case SOL_MEMDESC_TYPE_INT8:
        return sizeof(int8_t);
    case SOL_MEMDESC_TYPE_INT16:
        return sizeof(int16_t);
    case SOL_MEMDESC_TYPE_INT32:
        return sizeof(int32_t);
    case SOL_MEMDESC_TYPE_INT64:
        return sizeof(int64_t);
    case SOL_MEMDESC_TYPE_LONG:
        return sizeof(long);
    case SOL_MEMDESC_TYPE_SSIZE:
        return sizeof(ssize_t);
    case SOL_MEMDESC_TYPE_BOOL:
        return sizeof(bool);
    case SOL_MEMDESC_TYPE_DOUBLE:
        return sizeof(double);
    case SOL_MEMDESC_TYPE_STRING:
        return sizeof(char *);
    case SOL_MEMDESC_TYPE_CONST_STRING:
        return sizeof(const char *);
    case SOL_MEMDESC_TYPE_PTR:
        return sizeof(void *);
    /* fall through */
    case SOL_MEMDESC_TYPE_STRUCTURE:
    case SOL_MEMDESC_TYPE_ARRAY:
    case SOL_MEMDESC_TYPE_ENUMERATION:
        if (desc->size)
            return desc->size;

    /* must provide size */
    /* fall through */
    default:
        errno = EINVAL;
        return 0;
    }
}

/**
 * @brief initialize the memory.
 *
 * This will use the default content specified in struct
 * sol_memdesc::defcontent according to the type spefified in
 * struct sol_memdesc::type.
 *
 * @param desc the memory description.
 * @param memory the memory to initialize.
 *
 * @return 0 on success, negative errno on failure.
 *
 * @see sol_memdesc_new_with_defaults()
 */
int sol_memdesc_init_defaults(const struct sol_memdesc *desc, void *memory);

/**
 * @brief copy the memory using the given description.
 *
 * This function will copy @a src_memory to @a dst_memory using
 * the given description, with that members that need special
 * treatment will have it, like strings will be duplicated.
 *
 * @param desc the memory description.
 * @param src_memory the source/origin memory.
 * @param dst_memory the destination/target memory.
 *
 * @return 0 on success, negative errno on failure.
 *
 * @see sol_memdesc_set_content()
 */
int sol_memdesc_copy(const struct sol_memdesc *desc, const void *src_memory, void *dst_memory);

/**
 * @brief set the content of this memory.
 *
 * This function take care to set the content, disposing of the previous
 * content if any and duplicating the new one as required, like for
 * strings.
 *
 * @param desc the memory description.
 * @param memory the memory to set content.
 * @param ptr_content a pointer to the given content, dependent on the
 *        type. If a SOL_MEMDESC_TYPE_BOOL, then it must be a
 *        pointer to a bool.
 *
 * @return 0 on success, negative errno on failure.
 */
int sol_memdesc_set_content(const struct sol_memdesc *desc, void *memory, const void *ptr_content);

/**
 * @brief compare two memories using the given description.
 *
 * This function will compare @a a_memory to @a b_memory using
 * the given description, with that members that need special
 * treatment will have it, like strings will be strcmp(). Operations
 * may be overriden per-memdesc as defined in
 * struct sol_memdesc::ops.
 *
 * @note SOL_MEMDESC_TYPE_PTR can only compare to NULL or same pointer
 *       unless struct sol_memdesc::pointed_item is provided, then the
 *       value of the pointed item is compared by recursively calling
 *       sol_memdesc_compare() on the pointed memories. @c NULL is
 *       always considered to be smaller than any value. This behavior
 *       can be changed with struct sol_memdesc::ops::compare.
 *
 * @param desc the memory description.
 * @param a_memory the first memory to compare.
 * @param b_memory the second memory to compare.
 *
 * @return On error, 0 and errno is set to non-zero. On success (errno
 *         == 0), 0 means equal, <0 means a_memory is smaller, >0
 *         means b_memory is smaller.
 */
int sol_memdesc_compare(const struct sol_memdesc *desc, const void *a_memory, const void *b_memory);

/**
 * @brief free the contents (internal memory) of a member.
 *
 * This function will take care of special handling needed for each
 * member, like strings that must be freed.
 *
 * @param desc the memory description.
 * @param memory the memory to free the internal contents.
 *
 * @return 0 on success, negative errno on failure.
 *
 * @see sol_memdesc_free()
 */
int sol_memdesc_free_content(const struct sol_memdesc *desc, void *memory);

/**
 * @brief Free the contents and the memory.
 *
 * @param desc the memory description.
 * @param memory the memory to free the contents and the memory itself.
 *
 * @see sol_memdesc_free_content()
 */
static inline void
sol_memdesc_free(const struct sol_memdesc *desc, void *memory)
{
    sol_memdesc_free_content(desc, memory);
    free(memory);
}

/**
 * @brief Allocate the memory required by this description and initialize it.
 *
 * This will allocate offset + size bytes, then fill these bytes with
 * the content defined in struct sol_memdesc::defcontent.
 *
 * @param desc the memory description.
 *
 * @return NULL on error, newly allocated memory on success. Free
 * using sol_memdesc_free().
 *
 * @see sol_memdesc_free()
 */
static inline void *
sol_memdesc_new_with_defaults(const struct sol_memdesc *desc)
{
    void *mem;
    uint16_t size;
    int r;

    size = sol_memdesc_get_size(desc);
    if (!size)
        return NULL;

    mem = malloc(size);
    if (!mem)
        return NULL;

    r = sol_memdesc_init_defaults(desc, mem);
    if (r < 0) {
        sol_memdesc_free(desc, mem);
        errno = -r;
        return NULL;
    }

    errno = 0;
    return mem;
}

/**
 * @brief Get the length of an array.
 *
 * This function must be applied to SOL_MEMDESC_TYPE_ARRAY and will
 * call struct sol_memdesc::ops::array::get_length.
 *
 * The returned value is about the number of items according to
 * struct sol_memdesc::array_item.
 *
 * @param array_desc the memory description of type SOL_MEMDESC_TYPE_ARRAY.
 * @param memory the memory holding the array.
 *
 * @return On error, negative errno is returned. Zero or more for success.
 */
ssize_t sol_memdesc_get_array_length(const struct sol_memdesc *array_desc, const void *memory);

/**
 * @brief Get the array element.
 *
 * This function must be applied to SOL_MEMDESC_TYPE_ARRAY and will
 * call struct sol_memdesc::ops::array::get_element.
 *
 * @note for speed purposes, this function will not guarantee
 * out-of-bounds checking, please ensure the index is less than
 * sol_memdesc_get_array_length() before calling it.
 *
 * @param array_desc the memory description of type SOL_MEMDESC_TYPE_ARRAY.
 * @param memory the memory holding the array.
 * @param idx the index of the element inside the array.
 *
 * @return On error NULL is returned and errno is set. On success the
 *         memory of the item is returned.
 *
 * @see sol_memdesc_get_array_length()
 */
void *sol_memdesc_get_array_element(const struct sol_memdesc *array_desc, const void *memory, size_t idx);

/**
 * @brief Resize the length of an array.
 *
 * This function must be applied to SOL_MEMDESC_TYPE_ARRAY and will
 * call struct sol_memdesc::ops::array::resize.
 *
 * @param array_desc the memory description of type SOL_MEMDESC_TYPE_ARRAY.
 * @param memory the memory holding the array.
 * @param length the new length.
 *
 * @return On error, negative errno is returned. 0 on success.
 */
int sol_memdesc_resize_array(const struct sol_memdesc *array_desc, void *memory, size_t length);

/**
 * @brief Append the array element.
 *
 * This function must be applied to SOL_MEMDESC_TYPE_ARRAY and will
 * call struct sol_memdesc::ops::array::get_element,
 * struct sol_memdesc::ops::array::get_length and
 * struct sol_memdesc::ops::array::resize to resize the array
 * and add one item at the end. Then sol_memdesc_set_content() is
 * called at the new element.
 *
 * @param array_desc the memory description of type SOL_MEMDESC_TYPE_ARRAY.
 * @param memory the memory holding the array.
 * @param ptr_content a pointer to the given content, dependent on the
 *        type of array_item. If a SOL_MEMDESC_TYPE_BOOL, then it must
 *        be a pointer to a bool.
 *
 * @return On error, negative errno is returned. 0 on success
 *
 * @see sol_memdesc_get_array_length()
 * @see sol_memdesc_get_array_element()
 * @see sol_memdesc_resize_array()
 * @see sol_memdesc_set_content()
 */
static inline int
sol_memdesc_append_array_element(const struct sol_memdesc *array_desc, void *memory, const void *ptr_content)
{
    void *element;
    ssize_t len;
    int r;

    len = sol_memdesc_get_array_length(array_desc, memory);
    if (len < 0)
        return len;

    if (!array_desc->array_item)
        return -EINVAL;

    r = sol_memdesc_resize_array(array_desc, memory, len + 1);
    if (r < 0)
        return r;

    element = sol_memdesc_get_array_element(array_desc, memory, len);
    if (!element)
        return -errno;

    r = sol_memdesc_set_content(array_desc->array_item, element, ptr_content);
    if (r < 0)
        sol_memdesc_resize_array(array_desc, memory, len);

    return r;
}

/**
 * @brief Macro to loop of array elements in a given range.
 *
 * @param desc the memory description of type SOL_MEMDESC_TYPE_ARRAY.
 * @param memory the memory holding the array.
 * @param start_idx the starting index (inclusive).
 * @param end_idx the ending index (non-inclusive, up to it).
 * @param itr_idx where to store the current iteration index.
 * @param element where to store the element or NULL on last iteration.
 */
#define SOL_MEMDESC_FOREACH_ARRAY_ELEMENT_IN_RANGE(desc, memory, start_idx, end_idx, itr_idx, element) \
    for (itr_idx = start_idx, \
        element = (itr_idx < end_idx) ? sol_memdesc_get_array_element((desc), (memory), itr_idx) : NULL; \
        itr_idx < end_idx && element; \
        itr_idx++, \
        element = (itr_idx < end_idx) ? sol_memdesc_get_array_element((desc), (memory), itr_idx) : NULL)

/**
 * @def _SOL_MEMDESC_CHECK_API_VERSION(desc)
 *
 * Helper to check api-version if needed.
 * @internal
 */
#ifdef SOL_NO_API_VERSION
#define _SOL_MEMDESC_CHECK_API_VERSION(desc) 1
#else
#define _SOL_MEMDESC_CHECK_API_VERSION(desc) ((desc)->api_version == SOL_MEMDESC_API_VERSION_COMPILED)
#endif

/**
 * @def _SOL_MEMDESC_CHECK(desc)
 *
 * Helper to check for a valid struct sol_memdesc.
 *
 * @internal
 */
#define _SOL_MEMDESC_CHECK(desc) \
    ((desc) &&  _SOL_MEMDESC_CHECK_API_VERSION(desc) && (desc)->type != SOL_MEMDESC_TYPE_UNKNOWN)

/**
 * @def _SOL_MEMDESC_CHECK_STRUCTURE(structure_desc)
 *
 * Helper to check for a valid struct sol_memdesc of type SOL_MEMDESC_TYPE_STRUCTURE
 *
 * @internal
 */
#define _SOL_MEMDESC_CHECK_STRUCTURE(structure_desc) \
    (_SOL_MEMDESC_CHECK(structure_desc) && (structure_desc)->structure_members && _SOL_MEMDESC_CHECK(&((structure_desc)->structure_members->base)))

/**
 * @def _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER(structure_desc, member_desc)
 *
 * Helper to check for a valid struct sol_memdesc of type
 * SOL_MEMDESC_TYPE_STRUCTURE and if member is within structure
 * boundaries.
 *
 * @param structure_desc the memory description of type SOL_MEMDESC_TYPE_STRUCTURE
 * @param member_desc the struct sol_memdesc_structure_member to check.
 *
 * @internal
 */
#define _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER(structure_desc, member_desc) \
    (_SOL_MEMDESC_CHECK(&(member_desc)->base) && \
    ((member_desc)->offset + sol_memdesc_get_size(&(member_desc)->base) <= sol_memdesc_get_size((structure_desc))))

/**
 * @brief Macro to loop over all structure members.
 *
 * @param structure_desc the memory description of type SOL_MEMDESC_TYPE_STRUCTURE
 * @param member_desc where to store the struct sol_memdesc_structure_member.
 *        NULL when iteration ends.
 */
#define SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER(structure_desc, member_desc) \
    for (member_desc = (_SOL_MEMDESC_CHECK_STRUCTURE((structure_desc)) && _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER((structure_desc), (structure_desc)->structure_members)) ? (structure_desc)->structure_members : NULL; \
        _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER((structure_desc), member_desc); \
        member_desc = _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER((structure_desc), member_desc + 1) ? member_desc + 1 : NULL)

/**
 * @brief Macro to loop over all structure members and associated memory.
 *
 * @param structure_desc the memory description of type SOL_MEMDESC_TYPE_STRUCTURE
 * @param member_desc where to store the struct sol_memdesc_structure_member.
 *        NULL when iteration ends.
 * @param structure_memory the memory of the container structure.
 * @param member_memory where to store the element memory.
 */
#define SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER_MEMORY(structure_desc, member_desc, structure_memory, member_memory) \
    for (member_desc = (_SOL_MEMDESC_CHECK_STRUCTURE((structure_desc)) && _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER((structure_desc), (structure_desc)->structure_members)) ? (structure_desc)->structure_members : NULL, \
        member_memory = member_desc ? sol_memdesc_get_structure_member_memory((structure_desc), member_desc, (structure_memory)) : NULL; \
        _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER((structure_desc), member_desc) && member_memory; \
        member_desc = _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER((structure_desc), member_desc + 1) ? member_desc + 1 : NULL, \
        member_memory = member_desc ? sol_memdesc_get_structure_member_memory((structure_desc), member_desc, (structure_memory)) : NULL)

/**
 * @brief Find structure member given its name.
 *
 * The name is taken as a slice since sometimes it's not available as
 * a null-terminated strings (such as loading from other protocols
 * such as JSON).
 *
 * @param structure_desc a description of type SOL_MEMDESC_TYPE_STRUCTURE.
 * @param name the name to look for.
 *
 * @return pointer on success or NULL on errors (with errno set).
 *
 * @see sol_str_slice_from_str()
 * @see SOL_STR_SLICE_STR()
 * @see SOL_STR_SLICE_LITERAL()
 */
static inline const struct sol_memdesc_structure_member *
sol_memdesc_find_structure_member(const struct sol_memdesc *structure_desc, struct sol_str_slice name)
{
    const struct sol_memdesc_structure_member *itr;

    errno = EINVAL;
    if (!structure_desc || !name.len)
        return NULL;

    SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER(structure_desc, itr) {
        if (sol_str_slice_str_eq(name, itr->name)) {
            errno = 0;
            return itr;
        }
    }

    errno = ENOENT;
    return NULL;
}

/**
 * @brief get the pointer to the struct member memory description
 * inside the given container.
 *
 * This will use the struct sol_memdesc::offset to find the offset
 * inside the container.
 *
 * @param structure_desc the memory description of the structure.
 * @param member_desc the memory description of the structure member.
 * @param structure_memory the memory of the container (the pointer to the
 *        start of the structure that holds the member).
 *
 * @return @c NULL on errors or the pointer inside @a structure_memory on success.
 */
static inline void *
sol_memdesc_get_structure_member_memory(const struct sol_memdesc *structure_desc, const struct sol_memdesc_structure_member *member_desc, const void *structure_memory)
{
    errno = EINVAL;
    if (!structure_desc || !member_desc || !structure_memory)
        return NULL;

#ifndef SOL_NO_API_VERSION
    if (structure_desc->api_version != SOL_MEMDESC_API_VERSION_COMPILED)
        return NULL;
    if (member_desc->base.api_version != SOL_MEMDESC_API_VERSION_COMPILED)
        return NULL;
#endif

    if (member_desc->offset + sol_memdesc_get_size(&member_desc->base) > sol_memdesc_get_size(structure_desc)) {
        errno = EOVERFLOW;
        return NULL;
    }

    errno = 0;
    return ((uint8_t *)structure_memory) + member_desc->offset;
}

/**
 * @brief convert enumeration value to string.
 *
 * @param enumeration the memory description of the enumeration of
 *        type SOL_MEMDESC_TYPE_ENUMERATION.
 * @param memory the memory of the enumeration.
 *
 * @return On error, NULL should be returned and errno set. On success non-NULL is returned.
 */
const char *sol_memdesc_enumeration_to_str(const struct sol_memdesc *enumeration, const void *memory);

/**
 * @brief convert enumeration value from string.
 *
 * The return is stored in @c ptr_return, which must be the size stated in
 * struct sol_memdesc::size as returned by sol_memdesc_get_size().
 *
 * The string is given in the form of a slice so it doesn't need
 * to be null-terminated.
 *
 * @param enumeration the memory description of the enumeration of
 *        type SOL_MEMDESC_TYPE_ENUMERATION.
 * @param str the slice with the string to convert, doesn't need to be null-terminated.
 * @param ptr_return where to store the converted value. Must be a pointer to a memory
 *        of size sol_memdesc_get_size().
 *
 * @return On error, negative errno is returned. 0 on success.
 */
int sol_memdesc_enumeration_from_str(const struct sol_memdesc *enumeration, void *ptr_return, const struct sol_str_slice str);

/**
 * @brief Helper to fetch the memory as the largest supported unsigned integer.
 *
 * @param desc the memory description.
 * @param memory the memory to get content.
 *
 * @return the number as uint64_t. On errors, errno is set to non-zero.
 *
 * @see sol_memdesc_is_unsigned_integer().
 */
static inline uint64_t
sol_memdesc_get_as_uint64(const struct sol_memdesc *desc, const void *memory)
{
    int64_t i64;

    errno = EINVAL;
    if (!desc || !memory)
        return 0;

#ifndef SOL_NO_API_VERSION
    if (desc->api_version != SOL_MEMDESC_API_VERSION_COMPILED)
        return 0;
#endif

    errno = 0;
    switch (desc->type) {
    case SOL_MEMDESC_TYPE_UINT8:
        return *(const uint8_t *)memory;
    case SOL_MEMDESC_TYPE_UINT16:
        return *(const uint16_t *)memory;
    case SOL_MEMDESC_TYPE_UINT32:
        return *(const uint32_t *)memory;
    case SOL_MEMDESC_TYPE_UINT64:
        return *(const uint64_t *)memory;
    case SOL_MEMDESC_TYPE_ULONG:
        return *(const unsigned long *)memory;
    case SOL_MEMDESC_TYPE_SIZE:
        return *(const size_t *)memory;
    case SOL_MEMDESC_TYPE_INT8:
        i64 = *(const int8_t *)memory;
        goto check_signed;
    case SOL_MEMDESC_TYPE_INT16:
        i64 = *(const int16_t *)memory;
        goto check_signed;
    case SOL_MEMDESC_TYPE_INT32:
        i64 = *(const int32_t *)memory;
        goto check_signed;
    case SOL_MEMDESC_TYPE_INT64:
        i64 = *(const int64_t *)memory;
        goto check_signed;
    case SOL_MEMDESC_TYPE_LONG:
        i64 = *(const long *)memory;
        goto check_signed;
    case SOL_MEMDESC_TYPE_SSIZE:
        i64 = *(const ssize_t *)memory;
        goto check_signed;
    case SOL_MEMDESC_TYPE_BOOL:
        return *(const bool *)memory;
    case SOL_MEMDESC_TYPE_DOUBLE:
        i64 = *(const double *)memory;
        goto check_signed;
    case SOL_MEMDESC_TYPE_ENUMERATION: {
        uint8_t offset = 0;

        if (desc->size > sizeof(int64_t)) {
            errno = EINVAL;
            return 0;
        }
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        else if (desc->size < sizeof(int64_t))
            offset = sizeof(desc->defcontent.e) - desc->size;
#endif
        i64 = 0;
        memcpy((uint8_t *)&i64 + offset, memory, desc->size);
        goto check_signed;
    }
    case SOL_MEMDESC_TYPE_STRING:
    case SOL_MEMDESC_TYPE_CONST_STRING:
    case SOL_MEMDESC_TYPE_PTR:
    case SOL_MEMDESC_TYPE_STRUCTURE:
    case SOL_MEMDESC_TYPE_ARRAY:
    default:
        errno = EINVAL;
        return 0;
    }

check_signed:
    if (i64 < 0) {
        errno = ERANGE;
        return 0;
    }
    return i64;
}

/**
 * @brief Helper to fetch the memory as the largest supported signed integer.
 *
 * @param desc the memory description.
 * @param memory the memory to get content.
 *
 * @return the number as int64_t. On errors, errno is set to non-zero.
 *
 * @see sol_memdesc_is_signed_integer().
 */
static inline int64_t
sol_memdesc_get_as_int64(const struct sol_memdesc *desc, const void *memory)
{
    uint64_t u64;

    errno = EINVAL;
    if (!desc || !memory)
        return 0;

#ifndef SOL_NO_API_VERSION
    if (desc->api_version != SOL_MEMDESC_API_VERSION_COMPILED)
        return 0;
#endif

    errno = 0;
    switch (desc->type) {
    case SOL_MEMDESC_TYPE_UINT8:
        return *(const uint8_t *)memory;
    case SOL_MEMDESC_TYPE_UINT16:
        return *(const uint16_t *)memory;
    case SOL_MEMDESC_TYPE_UINT32:
        return *(const uint32_t *)memory;
    case SOL_MEMDESC_TYPE_UINT64:
        u64 = *(const uint64_t *)memory;
        goto check_overflow;
    case SOL_MEMDESC_TYPE_ULONG:
        u64 = *(const unsigned long *)memory;
        goto check_overflow;
    case SOL_MEMDESC_TYPE_SIZE:
        u64 = *(const size_t *)memory;
        goto check_overflow;
    case SOL_MEMDESC_TYPE_INT8:
        return *(const int8_t *)memory;
    case SOL_MEMDESC_TYPE_INT16:
        return *(const int16_t *)memory;
    case SOL_MEMDESC_TYPE_INT32:
        return *(const int32_t *)memory;
    case SOL_MEMDESC_TYPE_INT64:
        return *(const int64_t *)memory;
    case SOL_MEMDESC_TYPE_LONG:
        return *(const long *)memory;
    case SOL_MEMDESC_TYPE_SSIZE:
        return *(const ssize_t *)memory;
    case SOL_MEMDESC_TYPE_BOOL:
        return *(const bool *)memory;
    case SOL_MEMDESC_TYPE_DOUBLE:
        return *(const double *)memory;
    case SOL_MEMDESC_TYPE_ENUMERATION: {
        int64_t i64 = 0;
        uint8_t offset = 0;

        if (desc->size > sizeof(int64_t)) {
            errno = EINVAL;
            return 0;
        }
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        else if (desc->size < sizeof(int64_t))
            offset = sizeof(desc->defcontent.e) - desc->size;
#endif
        memcpy((uint8_t *)&i64 + offset, memory, desc->size);
        return i64;
    }
    case SOL_MEMDESC_TYPE_STRING:
    case SOL_MEMDESC_TYPE_CONST_STRING:
    case SOL_MEMDESC_TYPE_PTR:
    case SOL_MEMDESC_TYPE_STRUCTURE:
    case SOL_MEMDESC_TYPE_ARRAY:
    default:
        errno = EINVAL;
        return 0;
    }

check_overflow:
    if (u64 > INT64_MAX) {
        errno = ERANGE;
        return 0;
    }
    return u64;
}

/**
 * @brief Helper to set the memory as the largest supported unsigned integer.
 *
 * @param desc the memory description.
 * @param memory the memory to set content.
 * @param value the number as uint64_t.
 *
 * @return 0 on success, negative errno on errors.
 *
 * @see sol_memdesc_is_unsigned().
 */
static inline int
sol_memdesc_set_as_uint64(const struct sol_memdesc *desc, void *memory, uint64_t value)
{
    if (!desc || !memory)
        return -EINVAL;

#ifndef SOL_NO_API_VERSION
    if (desc->api_version != SOL_MEMDESC_API_VERSION_COMPILED)
        return -EINVAL;
#endif

    switch (desc->type) {
    case SOL_MEMDESC_TYPE_UINT8:
        if (value > UINT8_MAX)
            return -EOVERFLOW;
        *(uint8_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_UINT16:
        if (value > UINT16_MAX)
            return -EOVERFLOW;
        *(uint16_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_UINT32:
        if (value > UINT32_MAX)
            return -EOVERFLOW;
        *(uint32_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_UINT64:
        *(uint64_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_ULONG:
        if (value > ULONG_MAX)
            return -EOVERFLOW;
        *(unsigned long *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_SIZE:
        if (value > SIZE_MAX)
            return -EOVERFLOW;
        *(size_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_INT8:
        if (value > INT8_MAX)
            return -EOVERFLOW;
        *(int8_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_INT16:
        if (value > INT16_MAX)
            return -EOVERFLOW;
        *(int16_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_INT32:
        if (value > INT32_MAX)
            return -EOVERFLOW;
        *(int32_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_INT64:
        if (value > INT64_MAX)
            return -EOVERFLOW;
        *(int64_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_LONG:
        if (value > LONG_MAX)
            return -EOVERFLOW;
        *(long *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_SSIZE:
        if (value > SSIZE_MAX)
            return -EOVERFLOW;
        *(ssize_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_BOOL:
        *(bool *)memory = !!value;
        return 0;
    case SOL_MEMDESC_TYPE_DOUBLE:
        *(double *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_ENUMERATION: {
        uint8_t offset = 0;

        if (desc->size > sizeof(int64_t))
            return -EINVAL;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        else if (desc->size < sizeof(int64_t))
            offset = sizeof(desc->defcontent.e) - desc->size;
#endif

        if (value > (((uint64_t)1 << (desc->size * 8 - 1)) - 1))
            return -EOVERFLOW;

        memcpy(memory, (uint8_t *)&value + offset, desc->size);
        return 0;
    }
    case SOL_MEMDESC_TYPE_STRING:
    case SOL_MEMDESC_TYPE_CONST_STRING:
    case SOL_MEMDESC_TYPE_PTR:
    case SOL_MEMDESC_TYPE_STRUCTURE:
    case SOL_MEMDESC_TYPE_ARRAY:
    default:
        return -EINVAL;
    }
}

/**
 * @brief Helper to set the memory as the largest supported signed integer.
 *
 * @param desc the memory description.
 * @param memory the memory to set content.
 * @param value the number as int64_t.
 *
 * @return 0 on success, negative errno on errors.
 *
 * @see sol_memdesc_is_signed().
 */
static inline int64_t
sol_memdesc_set_as_int64(const struct sol_memdesc *desc, void *memory, int64_t value)
{
    if (!desc || !memory)
        return -EINVAL;

#ifndef SOL_NO_API_VERSION
    if (desc->api_version != SOL_MEMDESC_API_VERSION_COMPILED)
        return -EINVAL;
#endif

    switch (desc->type) {
    case SOL_MEMDESC_TYPE_UINT8:
        if (value < 0 || value > UINT8_MAX)
            return -EOVERFLOW;
        *(uint8_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_UINT16:
        if (value < 0 || value > UINT16_MAX)
            return -EOVERFLOW;
        *(uint16_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_UINT32:
        if (value < 0 || value > UINT32_MAX)
            return -EOVERFLOW;
        *(uint32_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_UINT64:
        if (value < 0)
            return -EOVERFLOW;
        *(uint64_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_ULONG:
        if (value < 0 || (uint64_t)value > ULONG_MAX)
            return -EOVERFLOW;
        *(unsigned long *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_SIZE:
        if (value < 0 || (uint64_t)value > SIZE_MAX)
            return -EOVERFLOW;
        *(size_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_INT8:
        if (value < INT8_MIN || value > INT8_MAX)
            return -EOVERFLOW;
        *(int8_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_INT16:
        if (value < INT16_MIN || value > INT16_MAX)
            return -EOVERFLOW;
        *(int16_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_INT32:
        if (value < INT32_MIN || value > INT32_MAX)
            return -EOVERFLOW;
        *(int32_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_INT64:
        *(int64_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_LONG:
        if (value < LONG_MIN || value > LONG_MAX)
            return -EOVERFLOW;
        *(long *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_SSIZE:
        if (value < SSIZE_MIN || value > SSIZE_MAX)
            return -EOVERFLOW;
        *(ssize_t *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_BOOL:
        *(bool *)memory = !!value;
        return 0;
    case SOL_MEMDESC_TYPE_DOUBLE:
        *(double *)memory = value;
        return 0;
    case SOL_MEMDESC_TYPE_ENUMERATION: {
        uint8_t offset = 0;

        if (desc->size > sizeof(int64_t))
            return -EINVAL;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        else if (desc->size < sizeof(int64_t))
            offset = sizeof(desc->defcontent.e) - desc->size;
#endif

        if (desc->size < sizeof(int64_t)) {
            if (value > (((int64_t)1 << (desc->size * 8 - 1)) - 1))
                return -EOVERFLOW;
            if (value < -((int64_t)1 << (desc->size * 8 - 1)))
                return -EOVERFLOW;
        }

        memcpy(memory, (uint8_t *)&value + offset, desc->size);
        return 0;
    }
    case SOL_MEMDESC_TYPE_STRING:
    case SOL_MEMDESC_TYPE_CONST_STRING:
    case SOL_MEMDESC_TYPE_PTR:
    case SOL_MEMDESC_TYPE_STRUCTURE:
    case SOL_MEMDESC_TYPE_ARRAY:
    default:
        return -EINVAL;
    }
}

/**
 * @brief Helper to check if type is unsigned integer-compatible.
 *
 * @param desc the memory description.
 *
 * @return true if it is unsigned integer (uint8_t, uint16_t, uint32_t, uint64_t,
 *         unsigned long or size_t).
 */
static inline bool
sol_memdesc_is_unsigned_integer(const struct sol_memdesc *desc)
{
    errno = EINVAL;
    if (!desc)
        return false;

#ifndef SOL_NO_API_VERSION
    if (desc->api_version != SOL_MEMDESC_API_VERSION_COMPILED)
        return false;
#endif

    errno = 0;
    switch (desc->type) {
    case SOL_MEMDESC_TYPE_UINT8:
    case SOL_MEMDESC_TYPE_UINT16:
    case SOL_MEMDESC_TYPE_UINT32:
    case SOL_MEMDESC_TYPE_UINT64:
    case SOL_MEMDESC_TYPE_ULONG:
    case SOL_MEMDESC_TYPE_SIZE:
        return true;
    case SOL_MEMDESC_TYPE_INT8:
    case SOL_MEMDESC_TYPE_INT16:
    case SOL_MEMDESC_TYPE_INT32:
    case SOL_MEMDESC_TYPE_INT64:
    case SOL_MEMDESC_TYPE_LONG:
    case SOL_MEMDESC_TYPE_SSIZE:
    case SOL_MEMDESC_TYPE_BOOL:
    case SOL_MEMDESC_TYPE_DOUBLE:
    case SOL_MEMDESC_TYPE_STRING:
    case SOL_MEMDESC_TYPE_CONST_STRING:
    case SOL_MEMDESC_TYPE_PTR:
    case SOL_MEMDESC_TYPE_STRUCTURE:
    case SOL_MEMDESC_TYPE_ARRAY:
    case SOL_MEMDESC_TYPE_ENUMERATION:
    default:
        return false;
    }
}

/**
 * @brief Helper to check if type is signed integer-compatible.
 *
 * @param desc the memory description.
 *
 * @return true if it is signed integer (int8_t, int16_t, int32_t, int64_t,
 *         long, ssize_t or enumeration).
 */
static inline bool
sol_memdesc_is_signed_integer(const struct sol_memdesc *desc)
{
    errno = EINVAL;
    if (!desc)
        return false;

#ifndef SOL_NO_API_VERSION
    if (desc->api_version != SOL_MEMDESC_API_VERSION_COMPILED)
        return false;
#endif

    errno = 0;
    switch (desc->type) {
    case SOL_MEMDESC_TYPE_UINT8:
    case SOL_MEMDESC_TYPE_UINT16:
    case SOL_MEMDESC_TYPE_UINT32:
    case SOL_MEMDESC_TYPE_UINT64:
    case SOL_MEMDESC_TYPE_ULONG:
    case SOL_MEMDESC_TYPE_SIZE:
        return false;
    case SOL_MEMDESC_TYPE_INT8:
    case SOL_MEMDESC_TYPE_INT16:
    case SOL_MEMDESC_TYPE_INT32:
    case SOL_MEMDESC_TYPE_INT64:
    case SOL_MEMDESC_TYPE_LONG:
    case SOL_MEMDESC_TYPE_SSIZE:
    case SOL_MEMDESC_TYPE_ENUMERATION:
        return true;
    case SOL_MEMDESC_TYPE_BOOL:
    case SOL_MEMDESC_TYPE_DOUBLE:
    case SOL_MEMDESC_TYPE_STRING:
    case SOL_MEMDESC_TYPE_CONST_STRING:
    case SOL_MEMDESC_TYPE_PTR:
    case SOL_MEMDESC_TYPE_STRUCTURE:
    case SOL_MEMDESC_TYPE_ARRAY:
    default:
        return false;
    }
}

/**
 * @brief Options on how to serialize a memory given its description.
 */
typedef struct sol_memdesc_serialize_options {
#ifndef SOL_NO_API_VERSION
#define SOL_MEMDESC_SERIALIZE_OPTIONS_API_VERSION (1) /**< @brief API version to use in struct sol_memdesc_serialize_options::api_version */
    uint16_t api_version; /**< @brief API version, must match SOL_MEMDESC_SERIALIZE_OPTIONS_API_VERSION at runtime */
#endif
    /**
     * @brief function used to format a signed integer.
     *
     * If not provided printf() will be used using the current locale.
     *
     * Should return 0 on success, negative errno on failure.
     */
    int (*serialize_int64)(const struct sol_memdesc *desc, int64_t value, struct sol_buffer *buffer);
    /**
     * @brief function used to format an unsigned integer.
     *
     * If not provided printf() will be used using the current locale.
     *
     * Should return 0 on success, negative errno on failure.
     */
    int (*serialize_uint64)(const struct sol_memdesc *desc, uint64_t value, struct sol_buffer *buffer);
    /**
     * @brief function used to format a double precision floating point number.
     *
     * If not provided printf() with @c "%g" will be used using the current locale.
     *
     * Should return 0 on success, negative errno on failure.
     */
    int (*serialize_double)(const struct sol_memdesc *desc, double value, struct sol_buffer *buffer);
    /**
     * @brief function used to format a boolean.
     *
     * If not provided "true"  or "false" will be used.
     *
     * Should return 0 on success, negative errno on failure.
     */
    int (*serialize_bool)(const struct sol_memdesc *desc, bool value, struct sol_buffer *buffer);
    /**
     * @brief function used to format a pointer.
     *
     * If not provided printf() with @c "%p" will be used using the
     * current locale.
     *
     * Often this is used for NULL or when the desc->children is
     * empty, otherwise the code will handle SOL_MEMDESC_TYPE_PTR as a
     * structure, accessing the pointed memory.
     *
     * Should return 0 on success, negative errno on failure.
     */
    int (*serialize_pointer)(const struct sol_memdesc *desc, const void *value, struct sol_buffer *buffer);
    /**
     * @brief function used to format a string.
     *
     * If not provided will place the string inside double-quotes and
     * inner quotes and non-printable chars will be escaped.
     *
     * @note the given value may be @c NULL!
     *
     * Should return 0 on success, negative errno on failure.
     */
    int (*serialize_string)(const struct sol_memdesc *desc, const char *value, struct sol_buffer *buffer);
    /**
     * @brief function used to format an enumeration.
     *
     * If not provided will place the enumeration as string (if available)
     * or integer if not.
     *
     * For ease of use one can use sol_memdesc_enumeration_to_str()
     * and sol_memdesc_get_as_int64() on @c memory.
     *
     * @note the given value may be @c NULL!
     *
     * Should return 0 on success, negative errno on failure.
     */
    int (*serialize_enumeration)(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buffer);
    /**
     * @brief function used to format a struct member.
     *
     * If not provided the function will print use the
     * struct sol_str_slice under
     * struct sol_memdesc_serialize_options::structure. If
     * struct sol_memdesc_serialize_options::structure::show_key, then
     * struct sol_memdesc_serialize_options::structure::key::start and
     * struct sol_memdesc_serialize_options::structure::key::end are used around the
     * struct sol_memdesc::name that is dumped as-is. If SOL_MEMDESC_DESCRIPTION
     * is enabled, then struct sol_memdesc_serialize_options::structure::show_description
     * is true, then the description is printed after value surrounded by
     * struct sol_memdesc_serialize_options::structure::description::start and
     * struct sol_memdesc_serialize_options::structure::description::end.
     *
     * If multiple members exist, they will be separated with
     * struct sol_memdesc_serialize_options::structure::separator.
     *
     * Should return 0 on success, negative errno on failure.
     */
    int (*serialize_structure_member)(const struct sol_memdesc *structure, const struct sol_memdesc_structure_member *member, const void *memory, struct sol_buffer *buffer, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix, bool is_first);
    /**
     * @brief function used to format an array item.
     *
     * If not provided the function will print use the
     * struct sol_str_slice under
     * struct sol_memdesc_serialize_options::array. If
     * struct sol_memdesc_serialize_options::array::show_index, then
     * struct sol_memdesc_serialize_options::array::index::start and
     * struct sol_memdesc_serialize_options::array::index::end are used around the
     * @c idx.
     *
     * If multiple items exist, they will be separated with
     * struct sol_memdesc_serialize_options::array::separator.
     *
     * Should return 0 on success, negative errno on failure.
     */
    int (*serialize_array_item)(const struct sol_memdesc *desc, size_t idx, const void *memory, struct sol_buffer *buffer, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix);
    /**
     * @brief options used by serialize_structure_member.
     *
     * These options control the behavior of
     * struct sol_memdesc_serialize_options::serialize_structure_member.
     */
    struct {
        struct {
            /**
             * Used when starting a new structure.
             */
            const struct sol_str_slice start;
            /**
             * Used when finishing a structure.
             */
            const struct sol_str_slice end;
            /**
             * Used to indent a new container.
             */
            const struct sol_str_slice indent;
        } container;
        struct {
            /**
             * Used when starting a new structure member.
             *
             * This is only to be used if
             * struct sol_memdesc_serialize_options::structure::show_key.
             */
            const struct sol_str_slice start;
            /**
             * Used when finishing a structure member.
             *
             * This is only to be used if
             * struct sol_memdesc_serialize_options::structure::show_key.
             */
            const struct sol_str_slice end;
            /**
             * Used to indent a new key.
             */
            const struct sol_str_slice indent;
        } key;
        struct {
            /**
             * Used when starting a new structure value.
             */
            const struct sol_str_slice start;
            /**
             * Used when finishing structure value.
             */
            const struct sol_str_slice end;
            /**
             * Used to indent a new value.
             */
            const struct sol_str_slice indent;
        } value;
#ifdef SOL_MEMDESC_DESCRIPTION
        struct {
            /**
             * Used when starting a new structure member description.
             */
            const struct sol_str_slice start;
            /**
             * Used when finishing structure member description.
             */
            const struct sol_str_slice end;
            /**
             * Used to indent a new member description.
             */
            const struct sol_str_slice indent;
        } description;
#endif
        /**
         * Used if multiple members exist.
         */
        struct sol_str_slice separator;
        /**
         * Controls whenever the key is to be serialized.
         */
        bool show_key;
        /**
         * Controls whenever struct sol_memdesc_structure_member::detail
         * is to be printed.
         *
         * If true, detail members will be printed. If false, only
         * non-detail members will.
         */
        bool detailed;
#ifdef SOL_MEMDESC_DESCRIPTION
        /**
         * Controls whenever the description is to be serialized.
         */
        bool show_description;
#endif
    } structure;
    /**
     * @brief options used by serialize_array_item
     *
     * These options control the behavior of
     * struct sol_memdesc_serialize_options::serialize_array_item.
     */
    struct {
        struct {
            /**
             * Used when starting a new array.
             */
            const struct sol_str_slice start;
            /**
             * Used when finishing a array.
             */
            const struct sol_str_slice end;
            /**
             * Used to indent a new array.
             */
            const struct sol_str_slice indent;
        } container;
        struct {
            /**
             * Used when starting a new array item.
             *
             * This is only to be used if
             * struct sol_memdesc_serialize_options::array::show_index.
             */
            const struct sol_str_slice start;
            /**
             * Used when finishing a array item.
             *
             * This is only to be used if
             * struct sol_memdesc_serialize_options::array::show_index.
             */
            const struct sol_str_slice end;
            /**
             * Used to indent a new index.
             */
            const struct sol_str_slice indent;
        } index;
        struct {
            /**
             * Used when starting a new array item value.
             */
            const struct sol_str_slice start;
            /**
             * Used when finishing array item value.
             */
            const struct sol_str_slice end;
            /**
             * Used to indent a new value.
             */
            const struct sol_str_slice indent;
        } value;
        /**
         * Used if multiple items exist.
         */
        struct sol_str_slice separator;
        /**
         * Controls whenever the index is to be serialized.
         */
        bool show_index;
    } array;
} sol_memdesc_serialize_options;

/**
 * @brief the default struct sol_memdesc_serialize_options.
 *
 * This symbol defines the original serialize options used by Soletta,
 * it can be used to get default slices as well as serialization functions.
 *
 * For instance, if you want to customize the serialization of a given
 * structure member but not others, then you can override
 * struct sol_memdesc_serialize_options::serialize_structure_member and
 * call SOL_MEMDESC_SERIALIZE_OPTIONS::serialize_structure_member whenever
 * to use the standard output.
 */
extern const struct sol_memdesc_serialize_options SOL_MEMDESC_SERIALIZE_OPTIONS_DEFAULT;

/**
 * @brief Serialize a memory to a buffer using a description.
 *
 * If no options are provided, then it will serialize in a C-like
 * pattern, however if struct member names are not valid C symbols, it
 * will not be a valid C.
 *
 * @param desc the memory description.
 * @param memory the memory to serialize.
 * @param buffer where to serialize the memory. Must be pre-initialized,
 *        contents will be appended.
 * @param opts if provided will modify how to serialize the memory.
 * @param prefix some prefix to be added to lines, it will be modified
 *        during iteration to contain new indent strings. May be null
 *        so a local buffer is automatically created and destroyed.
 *
 * @return 0 on success, negative errno otherwise.
 */
int sol_memdesc_serialize(const struct sol_memdesc *desc, const void *memory, struct sol_buffer *buffer, const struct sol_memdesc_serialize_options *opts, struct sol_buffer *prefix);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
