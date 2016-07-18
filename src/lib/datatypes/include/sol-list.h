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

#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

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
 * This special data type is designed to store node information in the same memory as the data.
 *
 * See @ref sol_list for more info.
 *
 * @{
 */

/**
 * @brief Structure to add list support to a type.
 *
 * To make possible for instances of a given type to be part of a @c sol_list,
 * the type structure needs to begin with a @c sol_list field. Example:
 *
 * @code
 * struct example_s {
 *     struct sol_list list;
 *     int data;
 * }
 * @endcode
 */
typedef struct sol_list {
    struct sol_list *next; /**< @brief Link to the next node in the list */
    struct sol_list *prev; /**< @brief Link to the previous node in the list */
} sol_list;

/**
 * @def SOL_LIST_INIT
 * @brief Helper macro to initialize a @c sol_list structure. Example:
 *
 * @code
 * struct sol_list mylist = SOL_LIST_INIT(mylist);
 * @endcode
 *
 * @param name List to be initialized
 */
#define SOL_LIST_INIT(name) { &(name), &(name) }

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
