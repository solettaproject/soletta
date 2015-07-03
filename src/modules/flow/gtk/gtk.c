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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

// The gtk module is a bit of an alien WRT logging, as it has to share
// the domain symbol externally with the various .o objects that will
// be linked together. Let's redeclare it by hand, then, and before
// including gtk-gen.h. Also, log_init() is defined here instead.
#include "common.h"
SOL_LOG_INTERNAL_DECLARE(_gtk_log_domain, "flow-gtk");

#include "gtk-gen.h"

#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "byte-editor.h"
#include "label.h"
#include "led.h"
#include "pushbutton.h"
#include "pwm-editor.h"
#include "pwm-viewer.h"
#include "rgb-editor.h"
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
log_init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
}

static void
init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;

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
