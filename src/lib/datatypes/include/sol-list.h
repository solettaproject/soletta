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

struct sol_list {
    struct sol_list *next, *prev;
};

#define SOL_LIST_INIT { NULL, NULL }
#define SOL_LIST_GET_CONTAINER(list, type, member) (type *)((char *)(list) - offsetof(type, member))
#define SOL_LIST_FOREACH(list, itr) for (itr = (list)->next; itr != (list); itr = itr->next)
#define SOL_LIST_FOREACH_SAFE(list, itr, itr_next) for (itr = (list)->next, itr_next = itr->next; itr != (list); itr = itr_next, itr_next = itr_next->next)

static inline void
sol_list_init(struct sol_list *list)
{
    list->next = list->prev = list;
}

static inline void
sol_list_append(struct sol_list *list, struct sol_list *new_l)
{
    new_l->next = list;
    new_l->prev = list->prev;
    list->prev->next = new_l;
    list->prev = new_l;
}

static inline void
sol_list_prepend(struct sol_list *list, struct sol_list *new_l)
{
    new_l->prev = list;
    new_l->next = list->next;
    list->next->prev = new_l;
    list->next = new_l;
}

static inline void
sol_list_remove(struct sol_list *list)
{
    list->next->prev = list->prev;
    list->prev->next = list->next;
}

static inline bool
sol_list_is_empty(struct sol_list *list)
{
    return list->next == list;
}

static inline void
sol_list_steal(struct sol_list *list, struct sol_list *new_head)
{
    list->prev->next = new_head;
    list->next->prev = new_head;
    new_head->next = list->next;
    new_head->prev = list->prev;
    sol_list_init(list);
}

#ifdef __cplusplus
}
#endif
