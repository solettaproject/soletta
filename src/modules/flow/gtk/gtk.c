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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "sol-flow/gtk.h"

SOL_LOG_INTERNAL_DECLARE(_gtk_log_domain, "flow-gtk");

#include "sol-glib-integration.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#include "byte-editor.h"
#include "label.h"
#include "led.h"
#include "led-7seg.h"
#include "pushbutton.h"
#include "pwm-editor.h"
#include "pwm-viewer.h"
#include "rgb-editor.h"
#include "float-editor.h"
#include "timestamp-editor.h"
#include "slider.h"
#include "spinbutton.h"
#include "toggle.h"
#include "window.h"

struct gtk_state {
    GList *nodes;
    window_t *window;
};

static struct gtk_state *gtk_state = NULL;
static struct gtk_state _gtk_state;

static void
init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;

    if (!sol_glib_integration())
        return;

    gtk_state = &_gtk_state;
    gtk_state->window = window_new();
}

static void
shutdown(void)
{
    window_free(gtk_state->window);
    gtk_state->nodes = NULL;
    gtk_state->window = NULL;
    gtk_state = NULL;
}

static char *
get_full_name(const struct sol_flow_node *node)
{
    const struct sol_flow_node *n;
    struct sol_ptr_vector parts = SOL_PTR_VECTOR_INIT;
    unsigned int total = 0;
    char *result = NULL, *part, *ptr;
    uint16_t i;

    if (!sol_flow_node_get_parent(node))
        return NULL;

    n = node;
    while (n != NULL) {
        const char *name;
        name = sol_flow_node_get_id(n);
        if (!name || !*name)
            break;

        n = sol_flow_node_get_parent(n);

        /* Don't use the toplevel id, since all nodes belong to it. */
        if (n == NULL)
            break;

        if (sol_ptr_vector_append(&parts, (char *)name) < 0)
            goto end;

        /* Extra one for separator (or NUL byte). */
        total += strlen(name) + 1;
    }

    if (total == 0)
        goto end;

    result = calloc(total, sizeof(char));
    if (!result)
        goto end;

    ptr = result;

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&parts, part, i) {
        size_t len = strlen(part);
        memcpy(ptr, part, len);
        ptr += len;
        if (i == 0)
            *ptr = '\0';
        else
            *ptr = '.';
        ptr++;
    }

end:
    sol_ptr_vector_clear(&parts);
    return result;
}

int
gtk_open(struct sol_flow_node *node,
    void *data,
    int (*setup_cb)(struct gtk_common_data *mdata,
    const struct sol_flow_node_options *options),
    const struct sol_flow_node_options *options)
{
    struct gtk_common_data *mdata = data;
    char *name;
    int r;

    SOL_NULL_CHECK(node, -EINVAL);
    SOL_NULL_CHECK(data, -EINVAL);
    SOL_NULL_CHECK(setup_cb, -EINVAL);

    mdata->node = node;

    if (!gtk_state)
        init();

    r = setup_cb(mdata, options);
    if (r < 0)
        return r;

    name = get_full_name(node);
    gtk_state->nodes = g_list_prepend(gtk_state->nodes, node);
    window_add_widget(gtk_state->window, mdata->widget,
        name ? : sol_flow_node_get_id(node));
    free(name);

    return 0;
}

void
gtk_close(struct sol_flow_node *node, void *data)
{
    struct gtk_common_data *mdata = data;

    gtk_state->nodes = g_list_remove(gtk_state->nodes, node);
    window_del_widget(gtk_state->window, mdata->widget);

    if (!gtk_state->nodes)
        shutdown();
}


#include "gtk-gen.c"
