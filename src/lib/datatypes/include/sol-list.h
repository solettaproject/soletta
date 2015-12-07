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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These are routines that Soletta provides for its list implementation.
 */

/**
 * @defgroup List List
 * @ingroup Datatypes
 *
 * @brief Doubly linked list, linked lists that can be iterated over in both directions.
 *
 * @{
 */

/**
 * @struct sol_list
 *
 * @brief Structure to add list support to a type.
 */
struct sol_list {
    struct sol_list *next; /**< @brief Link to the next node in the list */
    struct sol_list *prev; /**< @brief Link to the previous node in the list */
};

/**
 * @def SOL_LIST_INIT
 * @brief Helper macro to initialize a @c sol_list structure.
 */
#define SOL_LIST_INIT { NULL, NULL }

/**
 * @def SOL_LIST_GET_CONTAINER(list, type, member)
 *
 * @brief Helper macro to retrieve a pointer to the struct containing this list node.
 *
 * @param list The list node
 * @param type Container type
 * @param member Name of the @c sol_list member in the container struct
 */
#define SOL_LIST_GET_CONTAINER(list, type, member) (type *)((char *)(list) - offsetof(type, member))

/**
 * @def SOL_LIST_FOREACH(list, itr)
 * @brief Macro to iterate over a list easily.
 *
 * @param list The list to iterate over
 * @param itr Variable pointing to the current node on each iteration
 *
 * @see SOL_LIST_FOREACH_SAFE
 */
#define SOL_LIST_FOREACH(list, itr) for (itr = (list)->next; itr != (list); itr = itr->next)

/**
 * @def SOL_LIST_FOREACH_SAFE(list, itr, itr_next)
 * @brief Macro to iterate over a list with support for node deletion.
 *
 * This version allows nodes to be removed without breaking the iteration.
 *
 * @param list The list to iterate over
 * @param itr Variable pointing to the current node on each iteration
 * @param itr_next Variable pointing to the next node on each iteration
 *
 * @see SOL_LIST_FOREACH
 */
#define SOL_LIST_FOREACH_SAFE(list, itr, itr_next) for (itr = (list)->next, itr_next = itr->next; itr != (list); itr = itr_next, itr_next = itr_next->next)

/**
 * @brief Initializes a @c sol_list struct.
 *
 * @param list List pointer
 */
static inline void
sol_list_init(struct sol_list *list)
{
    list->next = list->prev = list;
}

/**
 * @brief Append an new node to the end of the list.
 *
 * @param list List pointer
 * @param new_l List node to be added
 */
static inline void
sol_list_append(struct sol_list *list, struct sol_list *new_l)
{
    new_l->next = list;
    new_l->prev = list->prev;
    list->prev->next = new_l;
    list->prev = new_l;
}

/**
 * @brief Attach an new node to the beginning of the list.
 *
 * @param list List pointer
 * @param new_l List node to be added
 */
static inline void
sol_list_prepend(struct sol_list *list, struct sol_list *new_l)
{
    new_l->prev = list;
    new_l->next = list->next;
    list->next->prev = new_l;
    list->next = new_l;
}

/**
 * @brief Remove the node pointed by @c list from the list.
 *
 * @param list Pointer to the node that will be removed from the list
 */
static inline void
sol_list_remove(struct sol_list *list)
{
    list->next->prev = list->prev;
    list->prev->next = list->next;
}

/**
 * @brief Convenience function to check if the list is empty.
 *
 * @param list List pointer
 *
 * @return @c true if the list is empty, @c false otherwise.
 */
static inline bool
sol_list_is_empty(struct sol_list *list)
{
    return list->next == list;
}

/**
 * @brief Steals the list nodes from a list.
 *
 * Steals the list nodes from a list by moving them to a new head
 * and reseting the original list to empty state.
 *
 * @param list Original list pointer
 * @param new_head Pointer to the list that will hold the stolen nodes
 */
static inline void
sol_list_steal(struct sol_list *list, struct sol_list *new_head)
{
    list->prev->next = new_head;
    list->next->prev = new_head;
    new_head->next = list->next;
    new_head->prev = list->prev;
    sol_list_init(list);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
