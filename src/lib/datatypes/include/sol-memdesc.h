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

#pragma once

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sol-common-buildopts.h>
#include <sol-str-slice.h>
#include <sol-macros.h>

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

/**
 * @brief Designates the type of the memory description
 */
enum sol_memdesc_type {
    SOL_MEMDESC_TYPE_UNKNOWN = 0, /**< @brief not to be used. */
    /**
     * @brief uint8_t equivalent (one unsigned byte).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::u8 and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_UINT8,
    /**
     * @brief uint16_t equivalent (two unsigned bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::u16 and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_UINT16,
    /**
     * @brief uint32_t equivalent (four unsigned bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::u32 and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_UINT32,
    /**
     * @brief uint64_t equivalent (four unsigned bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::u64 and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_UINT64,
    /**
     * @brief unsigned long equivalent.
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::ul and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_ULONG,
    /**
     * @brief size_t equivalent (four or eight unsigned bytes, depends on platform).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::sz and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_SIZE,
    /**
     * @brief int8_t equivalent (one signed byte).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::i8 and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_INT8,
    /**
     * @brief int16_t equivalent (two signed bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::i16 and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_INT16,
    /**
     * @brief int32_t equivalent (four signed bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::i32 and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_INT32,
    /**
     * @brief int64_t equivalent (eight signed bytes).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::i64 and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_INT64,
    /**
     * @brief signed long equivalent.
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::l and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_LONG,
    /**
     * @brief ssize_t equivalent (four or eight signed bytes, depends on platform).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::ssz and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_SSIZE,
    /**
     * @brief boolean equivalent.
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::b and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_BOOLEAN,
    /**
     * @brief double precision floating point equivalent.
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::d and struct sol_memdesc::children
     * is not used.
     */
    SOL_MEMDESC_TYPE_DOUBLE,
    /**
     * @brief null-terminated C-string (@c char*).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::s and struct sol_memdesc::children
     * is not used. It may be null.
     *
     * By default, strings are duplicated and freed as required.
     * @see SOL_MEMDESC_TYPE_CONST_STRING
     */
    SOL_MEMDESC_TYPE_STRING,
    /**
     * @brief null-terminated C-string (@c const char*).
     *
     * Initial content is specified in
     * struct sol_memdesc::defcontent::s and struct sol_memdesc::children
     * is not used. It may be null.
     *
     * By default, strings are NOT duplicated neither freed.
     * @see SOL_MEMDESC_TYPE_STRING
     */
    SOL_MEMDESC_TYPE_CONST_STRING,
    /**
     * @brief generic pointer (void *).
     *
     * If struct sol_memdesc::children is non-NULL, it will be managed
     * as such (malloc/free). Note that the initial value is still
     * defined as a pointer to the actual contents in struct
     * sol_memdesc::defcontent::p. If that is non-NULL, then the
     * pointer is allocated and that one will use defaults specified
     * in struct sol_memdesc::children::defcontent, then values from
     * struct sol_memdesc::defcontent::p is applied on top.
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
     * This is a recursive type with children described in
     * struct sol_memdesc::children, an array that is null-terminated
     * (all element members are zeroed).
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
     * struct sol_memdesc::children. It will not be touched, you
     * should manage it yourself with struct sol_memdesc::ops.
     *
     * To map a struct sol_vector, use
     * @c .size=sizeof(struct sol_vector),
     * And provide a @c .children with the description on what is to
     * be in the element, like a structure or a pointer to one, this
     * way sol_memdesc_init_defaults() will set
     * struct sol_vector::elem_size to size of children. Then you must
     * provide the following struct sol_memdesc::ops:
     *
     *  @li @c init_defaults: set @c elem_size from
     *      @c sol_memdesc_get_size(desc->children).
     *  @li @c array.get_length: return @c len.
     *  @li @c array.get_element: proxy return of sol_vector_get().
     *  @li @c array.resize: if shrinking, remember to call
     *      @c sol_memdesc_free_content(desc->children, it) for
     *      every item that will be removed, then call
     *      sol_vector_del_range(). If growing, call
     *      sol_vector_append_n() and initialze children with
     *      @c sol_memdesc_init_defaults(desc->children, it).
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
enum sol_memdesc_type sol_memdesc_type_from_str(const char *str) SOL_ATTR_WARN_UNUSED_RESULT SOL_ATTR_NONNULL(1);

/**
 * @brief Converts a sol_memdesc_type to a string.
 *
 * @param type the type to be converted.
 * @return the string or NULL, if the type is invalid.
 */
const char *sol_memdesc_type_to_str(enum sol_memdesc_type type) SOL_ATTR_WARN_UNUSED_RESULT;

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
 * @def SOL_MEMDESC_SET_DESCRIPTION(text)
 *
 * Helper to set the description member of struct sol_memdesc if that
 * is available (conditional to #SOL_MEMDESC_SET_DESCRIPTION).
 */
#ifdef SOL_MEMDESC_DESCRIPTION
#define SOL_MEMDESC_SET_DESCRIPTION(text) .description = (text)
#else
#define SOL_MEMDESC_SET_DESCRIPTION(text)
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
 * @brief Data type to describe a memory region.
 */
struct sol_memdesc {
#ifndef SOL_NO_API_VERSION
#define SOL_MEMDESC_API_VERSION (1) /**< @brief API version to use in struct sol_memdesc::api_version */
    uint16_t api_version; /**< @brief API version, must match SOL_MEMDESC_API_VERSION at runtime */
#endif
    /**
     * @brief offset in bytes relative to containing memory.
     *
     * If this is a member of a structure, then it's the
     * @c offsetof(struct, member). It is used to access the actual
     * memory.
     */
    uint16_t offset;
    /**
     * @brief size in bytes of the member memory.
     *
     * Usually this is @c sizeof(type), if a structure it will account
     * for all members plus paddings.
     *
     * For strings, this will be the size of the pointer, not the
     * actual string length. Likewise, for arrays this is the size of
     * the pointer, not the length of the array.
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
     * @brief whenever memory is mandatory in serialization and parsing.
     *
     * If false, must exist when serializing/parsing. if true, then
     * defcontent could be used if missing from input.
     */
    bool optional : 1;
    /**
     * @brief whenever memory is extended detail.
     *
     * If true, should only be included in serialization if detail is
     * wanted.
     */
    bool detail : 1;
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
     * @brief default contents to be used if @c required == false.
     *
     * If struct sol_memdesc::required is false, then this content
     * can be used to provide defaults.
     *
     * Note that complex types SOL_MEMDESC_TYPE_STRUCTURE and
     * SOL_MEMDESC_TYPE_ARRAY have their own handling with struct
     * sol_memdesc::children.
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
        bool b; /**< @brief use when SOL_MEMDESC_TYPE_BOOLEAN */
        double d; /**< @brief use when SOL_MEMDESC_TYPE_DOUBLE */
        const char *s; /**< @brief use when SOL_MEMDESC_TYPE_STRING or SOL_MEMDESC_TYPE_CONST_STRING */
        const void *p; /**< @brief use when SOL_MEMDESC_TYPE_PTR, SOL_MEMDESC_TYPE_STRUCTURE or SOL_MEMDESC_TYPE_ARRAY */
    } defcontent;
    /**
     * @brief how to access complex types (structures and arrays).
     *
     * If the memory is complex, use a recursive description
     * specified here.
     */
    const struct sol_memdesc *children;

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
    const struct sol_memdesc_ops {
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
         */
        int (*init_defaults)(const struct sol_memdesc *desc, void *container);
        /**
         * @brief sets the content of a memory.
         *
         * If provided, will be used to set the memory instead of the
         * traditional code that will, for example, strdup() and free()
         * strings.
         *
         * The parameter @c ptr_content is a pointer to the actual content,
         * depends on the actual type. If a SOL_MEMDESC_TYPE_BOOLEAN, for
         * example, it must be a @c bool*.
         *
         * Should return 0 on success, negative errno on errors.
         */
        int (*set_content)(const struct sol_memdesc *desc, void *container, const void *ptr_content);
        /**
         * @brief copy the content from another memory.
         *
         * If provided, will be used to set the memory instead of the
         * traditional code that will, for example, strdup() and free()
         * strings.
         *
         * Should return 0 on success, negative errno on errors.
         */
        int (*copy)(const struct sol_memdesc *desc, const void *src_container, void *dst_container);
        /**
         * @brief compare the content of two memories.
         *
         * If provided, will be used to compare the memory contents
         * instead of the traditional code that will, for example,
         * call strcmp() on strings.
         *
         * Should return 0 for equal, <0 if a_container is smaller, >0 if b_container is smaller.
         * On error, return 0 and set errno.
         */
        int (*compare)(const struct sol_memdesc *desc, const void *a_container, const void *b_container);
        /**
         * @brief free the contents (internal memory) of a memory.
         *
         * If provided, will be used to free the contents of a memory
         * instead of the traditional code that will, for example, free()
         * strings.
         *
         * Should return 0 on success, negative errno on errors.
         */
        int (*free_content)(const struct sol_memdesc *desc, void *container);
        struct {
            /**
             * @brief calculate array length.
             *
             * Will be used to calculate the array
             * length. Return should be number of items, each defined in
             * struct sol_memdesc::children.
             *
             * @note must be provided if type is SOL_MEMDESC_TYPE_ARRAY.
             *
             * @note the given memory is already inside the container, do
             *       not use @c offset or sol_memdesc_get_memory().
             *
             * On error, negative errno is returned.
             */
            ssize_t (*get_length)(const struct sol_memdesc *desc, const void *memory);
            /**
             * @brief get memory of the given array item.
             *
             * Will be used to get the array element by its index.
             * Return should be the memory pointer or NULL on error (then set errno accordingly).
             *
             * @note the given memory is already inside the container, do
             *       not use @c offset or sol_memdesc_get_memory().
             *
             * @note must be provided if type is SOL_MEMDESC_TYPE_ARRAY.
             */
            void *(*get_element)(const struct sol_memdesc *desc, const void *memory, size_t idx);
            /**
             * @brief resize array length.
             *
             * Will be used to resize the array
             * length. The given size should be number of items, each defined in
             * struct sol_memdesc::children.
             *
             * When implementing, always remember to free the items that
             * are not needed anymore when the new length is smaller than
             * the old. Failing to do so will lead to memory leaks.
             *
             * @note must be provided if type is SOL_MEMDESC_TYPE_ARRAY.
             *
             * @note the given memory is already inside the container, do
             *       not use @c offset or sol_memdesc_get_memory().
             *
             * On error, negative errno is returned.
             */
            int (*resize)(const struct sol_memdesc *desc, void *memory, size_t length);
        } array;
    } *ops;
};

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
 * @brief get the pointer to the memory description inside the given container.
 *
 * This will use the struct sol_memdesc::offset to find the offset
 * inside the container.
 *
 * @param desc the memory description.
 * @param container the memory of the container.
 *
 * @return @c NULL on errors or the pointer inside @a container on success.
 */
static inline void *
sol_memdesc_get_memory(const struct sol_memdesc *desc, const void *container)
{
    errno = EINVAL;
    if (!desc || !container)
        return NULL;

#ifndef SOL_NO_API_VERSION
    if (desc->api_version != SOL_MEMDESC_API_VERSION_COMPILED)
        return NULL;
#endif

    errno = 0;
    return ((uint8_t *)container) + desc->offset;
}

/**
 * @brief get the size in bytes of the memory description.
 *
 * This will use the struct sol_memdesc::size or will assume a value
 * per type.
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
    if (desc->size)
        return desc->size;

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
    case SOL_MEMDESC_TYPE_BOOLEAN:
        return sizeof(bool);
    case SOL_MEMDESC_TYPE_DOUBLE:
        return sizeof(double);
    case SOL_MEMDESC_TYPE_STRING:
        return sizeof(char *);
    case SOL_MEMDESC_TYPE_CONST_STRING:
        return sizeof(const char *);
    case SOL_MEMDESC_TYPE_PTR:
        return sizeof(void *);
    case SOL_MEMDESC_TYPE_STRUCTURE:
    case SOL_MEMDESC_TYPE_ARRAY:
    /* must provide size */
    default:
        errno = EINVAL;
        return 0;
    }
}

/**
 * @brief initialize the memory inside the container.
 *
 * This will use the default content specified in struct
 * sol_memdesc::defcontent according to the type spefified in
 * struct sol_memdesc::type.
 *
 * @param desc the memory description.
 * @param container the memory of the container.
 *
 * @return 0 on success, negative errno on failure.
 *
 * @see sol_memdesc_new_with_defaults()
 */
int sol_memdesc_init_defaults(const struct sol_memdesc *desc, void *container);

/**
 * @brief copy the memory using the given description.
 *
 * This function will copy @a src_container to @a dst_container using
 * the given description, with that members that need special
 * treatment will have it, like strings will be duplicated.
 *
 * @param desc the memory description.
 * @param src_container the memory of the source/origin container.
 * @param dst_container the memory of the destination/target container.
 *
 * @return 0 on success, negative errno on failure.
 *
 * @see sol_memdesc_set_content()
 */
int sol_memdesc_copy(const struct sol_memdesc *desc, const void *src_container, void *dst_container);

/**
 * @brief set the content of this memory.
 *
 * This function take care to set the content, disposing of the previous
 * content if any and duplicating the new one as required, like for
 * strings.
 *
 * @param desc the memory description.
 * @param container the memory of the container.
 * @param ptr_content a pointer to the given content, dependent on the
 *        type. If a SOL_MEMDESC_TYPE_BOOLEAN, then it must be a
 *        pointer to a bool.
 *
 * @return 0 on success, negative errno on failure.
 */
int sol_memdesc_set_content(const struct sol_memdesc *desc, void *container, const void *ptr_content);

/**
 * @brief compare two memories using the given description.
 *
 * This function will compare @a a_container to @a b_container using
 * the given description, with that members that need special
 * treatment will have it, like strings will be strcmp(). Operations
 * may be overriden per-memdesc as defined in
 * struct sol_memdesc::ops.
 *
 * @param desc the memory description.
 * @param a_container the first memory to compare.
 * @param b_container the second memory to compare.
 *
 * @return On error, 0 and errno is set to non-zero. On success (errno
 * == 0), 0 means equal, <0 means a_container is smaller, >0 means
 * b_container is smaller.
 */
int sol_memdesc_compare(const struct sol_memdesc *desc, const void *a_container, const void *b_container);

/**
 * @brief Get the length of an array.
 *
 * This function must be applied to SOL_MEMDESC_TYPE_ARRAY and will
 * call struct sol_memdesc::ops::array::get_length.
 *
 * The returned value is about the number of items according to
 * struct sol_memdesc::children.
 *
 * @param desc the memory description.
 * @param memory the memory holding the array. Use sol_memdesc_get_memory()
 *        if it's inside a container.
 *
 * @return On error, negative errno is returned. Zero or more for success.
 */
ssize_t sol_memdesc_get_array_length(const struct sol_memdesc *desc, const void *memory);

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
 * @param desc the memory description.
 * @param memory the memory holding the array. Use sol_memdesc_get_memory()
 *        if it's inside a container.
 * @param idx the index of the element inside the array.
 *
 * @return On error NULL is returned and errno is set. On success the
 * memory of the item is returned.
 *
 * @see sol_memdesc_get_array_length()
 */
void *sol_memdesc_get_array_element(const struct sol_memdesc *desc, const void *memory, size_t idx);

/**
 * @brief Resize the length of an array.
 *
 * This function must be applied to SOL_MEMDESC_TYPE_ARRAY and will
 * call struct sol_memdesc::ops::array::resize.
 *
 * The returned value is about the number of items according to
 * struct sol_memdesc::children.
 *
 * @param desc the memory description.
 * @param memory the memory holding the array. Use sol_memdesc_get_memory()
 *        if it's inside a container.
 * @param length the new length.
 *
 * @return On error, negative errno is returned. 0 on success.
 */
int sol_memdesc_resize_array(const struct sol_memdesc *desc, void *memory, size_t length);

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
 * @param desc the memory description.
 * @param memory the memory holding the array. Use sol_memdesc_get_memory()
 *        if it's inside a container.
 * @param ptr_content a pointer to the given content, dependent on the
 *        type of children. If a SOL_MEMDESC_TYPE_BOOLEAN, then it must
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
sol_memdesc_append_array_element(const struct sol_memdesc *desc, void *memory, const void *ptr_content)
{
    void *element;
    ssize_t len;
    int r;

    len = sol_memdesc_get_array_length(desc, memory);
    if (len < 0)
        return len;

    if (!desc->children)
        return -EINVAL;

    r = sol_memdesc_resize_array(desc, memory, len + 1);
    if (r < 0)
        return r;

    element = sol_memdesc_get_array_element(desc, memory, len);
    if (!element)
        return -errno;

    r = sol_memdesc_set_content(desc->children, element, ptr_content);
    if (r < 0)
        sol_memdesc_resize_array(desc, memory, len);

    return r;
}

/**
 * @brief Macro to loop of array elements in a given range.
 *
 * @param desc the memory description of type SOL_MEMDESC_TYPE_ARRAY.
 * @param memory the memory holding the array. Use sol_memdesc_get_memory()
 *        if it's inside a container.
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
    ((desc) &&  _SOL_MEMDESC_CHECK_API_VERSION(desc) && (desc)->name)

/**
 * @def _SOL_MEMDESC_CHECK_STRUCTURE(desc)
 *
 * Helper to check for a valid struct sol_memdesc of type SOL_MEMDESC_TYPE_STRUCTURE
 *
 * @internal
 */
#define _SOL_MEMDESC_CHECK_STRUCTURE(desc) \
    (_SOL_MEMDESC_CHECK(desc) && (desc)->children && _SOL_MEMDESC_CHECK((desc)->children))

/**
 * @def _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER(desc, member)
 *
 * Helper to check for a valid struct sol_memdesc of type
 * SOL_MEMDESC_TYPE_STRUCTURE and if member is within structure
 * boundaries.
 *
 * @internal
 */
#define _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER(desc, member) \
    (_SOL_MEMDESC_CHECK(member) && \
    ((member)->offset + sol_memdesc_get_size((member)) <= sol_memdesc_get_size((desc))))

/**
 * @brief Macro to loop over all structure members.
 *
 * @param desc the memory description of type SOL_MEMDESC_TYPE_STRUCTURE
 * @param element where to store the member (child) memdesc. NULL when iteration ends.
 */
#define SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER(desc, element) \
    for (element = (_SOL_MEMDESC_CHECK_STRUCTURE(desc) && _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER(desc, desc->children)) ? desc->children : NULL; \
        _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER(desc, element); \
        element = _SOL_MEMDESC_CHECK_STRUCTURE_MEMBER(desc, element + 1) ? element + 1 : NULL)

/**
 * @brief Find structure member (child) given its name.
 *
 * The name is taken as a slice since sometimes it's not available as
 * a null-terminated strings (such as loading from other protocols
 * such as JSON).
 *
 * @param desc a description of type SOL_MEMDESC_TYPE_STRUCTURE.
 * @param name the name to look for.
 *
 * @return pointer on success or NULL on errors (with errno set).
 *
 * @see sol_str_slice_from_str()
 * @see SOL_STR_SLICE_STR()
 * @see SOL_STR_SLICE_LITERAL()
 */
static inline const struct sol_memdesc *
sol_memdesc_find_structure_member(const struct sol_memdesc *desc, struct sol_str_slice name)
{
    const struct sol_memdesc *itr;

    errno = EINVAL;
    if (!desc || !name.len)
        return NULL;

    SOL_MEMDESC_FOREACH_STRUCTURE_MEMBER(desc, itr) {
        if (sol_str_slice_str_eq(name, itr->name)) {
            errno = 0;
            return itr;
        }
    }

    errno = ENOENT;
    return NULL;
}

/**
 * @brief free the contents (internal memory) of a member.
 *
 * This function will take care of special handling needed for each
 * member, like strings that must be freed.
 *
 * @param desc the memory description.
 * @param container the memory of the container to free the internal memory.
 *
 * @return 0 on success, negative errno on failure.
 *
 * @see sol_memdesc_free()
 */
int sol_memdesc_free_content(const struct sol_memdesc *desc, void *container);

/**
 * @brief Free the contents and the memory.
 *
 * @param desc the memory description.
 * @param container the memory of the container to free the  memory.
 *
 * @see sol_memdesc_free_content()
 */
static inline void
sol_memdesc_free(const struct sol_memdesc *desc, void *container)
{
    sol_memdesc_free_content(desc, container);
    free(container);
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

    mem = malloc(desc->offset + size);
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
 * @}
 */

#ifdef __cplusplus
}
#endif
