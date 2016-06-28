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

#include "test.h"
#include "sol-list.h"

struct list_element {
    struct sol_list list;
    int data;
};

DEFINE_TEST(test_list);

/**
 * Tests the entire sol_list API
 */
static void
test_list(void)
{
    int data = 0x80085;
    struct sol_list list = SOL_LIST_INIT(list), *it = NULL, *it_next = NULL;
    struct sol_list list2;
    struct list_element elem = { {}, data }, *entry = NULL;
    int i;

    ASSERT(sol_list_is_empty(&list));

    sol_list_append(&list, &elem.list);
    ASSERT(!sol_list_is_empty(&list));

    entry = SOL_LIST_GET_CONTAINER(list.next, struct list_element, list);
    ASSERT_INT_EQ(entry->data, data);

    /* Add 10 elements in front of the list from [9 ... 0] */
    for (i = 0; i < 10; i++) {
        entry = malloc(sizeof *entry);
        entry->data = i;
        sol_list_prepend(&list, &entry->list);
    }

    /* Check the elements were prepended correctly */
    i = 9;
    SOL_LIST_FOREACH (&list, it) {
        entry = SOL_LIST_GET_CONTAINER(it, struct list_element, list);
        ASSERT_INT_EQ(entry->data, i--);
        if (i == -1)
            break;
    }

    /* Add 10 elements at the end of the list [0 ... 9] */
    for (i = 0; i < 10; i++) {
        entry = malloc(sizeof *entry);
        entry->data = i;
        sol_list_append(&list, &entry->list);
    }

    /* Remove first 10 elements */
    i = 0;
    SOL_LIST_FOREACH_SAFE (&list, it, it_next) {
        sol_list_remove(it);

        entry = SOL_LIST_GET_CONTAINER(it, struct list_element, list);
        free(entry);

        if (i++ == 9)
            break;
    }

    /* Check the initial element is still there */
    entry = SOL_LIST_GET_CONTAINER(list.next, struct list_element, list);
    ASSERT_INT_EQ(entry->data, data);

    sol_list_remove(&elem.list);

    /* Move remaining elements to a new list */
    sol_list_steal(&list, &list2);
    ASSERT(sol_list_is_empty(&list));
    ASSERT(!sol_list_is_empty(&list2));

    /* Check that previously added elements were appended correctly */
    i = 0;
    SOL_LIST_FOREACH_SAFE (&list2, it, it_next) {
        entry = SOL_LIST_GET_CONTAINER(it, struct list_element, list);
        ASSERT_INT_EQ(entry->data, i++);

        sol_list_remove(it);
        free(entry);
    }

    ASSERT(sol_list_is_empty(&list2));
}

TEST_MAIN();
