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

#include <stdbool.h>
#include <glib-unix.h>

#include "sol-mainloop-impl.h"
#include "sol-util.h"

static GMainLoop *loop;

static gboolean
on_signal(gpointer ptr)
{
    SOL_DBG("got signal, quit main loop...");
    sol_quit();
    return true;
}

int
sol_mainloop_impl_init(void)
{
    loop = g_main_loop_new(NULL, false);
    if (!loop) {
        SOL_CRI("cannot create mainloop");
        return errno ? -errno : -ENOENT;
    }

    g_unix_signal_add(SIGINT, on_signal, loop);
    g_unix_signal_add(SIGTERM, on_signal, loop);
    return 0;
}

void
sol_mainloop_impl_run(void)
{
    g_main_loop_run(loop);
}

void
sol_mainloop_impl_quit(void)
{
    g_main_loop_quit(loop);
}

void
sol_mainloop_impl_shutdown(void)
{
    g_main_loop_unref(loop);
    loop = NULL;
}

struct gsourcefunc_wrap_data {
    bool (*cb)(void *data);
    const void *data;
};

static gboolean
gsource_wrap_func(gpointer data)
{
    struct gsourcefunc_wrap_data *wrap_data = data;

    return wrap_data->cb((void *)wrap_data->data);
}

void *
sol_mainloop_impl_timeout_add(unsigned int timeout_ms, bool (*cb)(void *data), const void *data)
{
    struct gsourcefunc_wrap_data *wrap_data = malloc(sizeof(*wrap_data));

    SOL_NULL_CHECK(wrap_data, NULL);

    wrap_data->cb = cb;
    wrap_data->data = data;
    return (void *)(long)g_timeout_add_full(0, timeout_ms,
        gsource_wrap_func, wrap_data, free);
}

bool
sol_mainloop_impl_timeout_del(void *handle)
{
    return g_source_remove((uintptr_t)handle);
}

void *
sol_mainloop_impl_idle_add(bool (*cb)(void *data), const void *data)
{
    struct gsourcefunc_wrap_data *wrap_data = malloc(sizeof(*wrap_data));

    SOL_NULL_CHECK(wrap_data, NULL);

    wrap_data->cb = cb;
    wrap_data->data = data;
    return (void *)(long)g_idle_add_full(0,
        gsource_wrap_func, wrap_data, free);
}

bool
sol_mainloop_impl_idle_del(void *handle)
{
    return g_source_remove((uintptr_t)handle);
}

struct sol_fd_glib {
    bool (*cb)(void *data, int fd, uint32_t active_flags);
    const void *data;
    int fd;
    uint32_t flags;
    gint id;

    int refcnt;
};

static GIOCondition
sol_to_glib_flags(uint32_t sol_flags)
{
    GIOCondition glib_flags = 0;

    if (sol_flags & SOL_FD_FLAGS_IN) glib_flags |= G_IO_IN;
    if (sol_flags & SOL_FD_FLAGS_OUT) glib_flags |= G_IO_OUT;
    if (sol_flags & SOL_FD_FLAGS_PRI) glib_flags |= G_IO_PRI;
    if (sol_flags & SOL_FD_FLAGS_ERR) glib_flags |= G_IO_ERR;
    if (sol_flags & SOL_FD_FLAGS_HUP) glib_flags |= G_IO_HUP;
    if (sol_flags & SOL_FD_FLAGS_NVAL) glib_flags |= G_IO_NVAL;

    return glib_flags;
}

static uint32_t
glib_to_sol_flags(GIOCondition glib_flags)
{
    uint32_t sol_flags = 0;

    if (glib_flags & G_IO_IN) sol_flags |= SOL_FD_FLAGS_IN;
    if (glib_flags & G_IO_OUT) sol_flags |= SOL_FD_FLAGS_OUT;
    if (glib_flags & G_IO_PRI) sol_flags |= SOL_FD_FLAGS_PRI;
    if (glib_flags & G_IO_ERR) sol_flags |= SOL_FD_FLAGS_ERR;
    if (glib_flags & G_IO_HUP) sol_flags |= SOL_FD_FLAGS_HUP;
    if (glib_flags & G_IO_NVAL) sol_flags |= SOL_FD_FLAGS_NVAL;

    return sol_flags;
}

static gboolean
on_fd(gint fd, GIOCondition cond, gpointer data)
{
    struct sol_fd_glib *fd_handle = data;

    return fd_handle->cb((void *)fd_handle->data, fd, glib_to_sol_flags(cond));
}

static void
unref_fd(void *data)
{
    struct sol_fd_glib *fd_handle = data;

    SOL_NULL_CHECK(fd_handle);

    fd_handle->refcnt--;

    if (fd_handle->refcnt <= 0)
        free(fd_handle);
}

void *
sol_mainloop_impl_fd_add(int fd, uint32_t flags, bool (*cb)(void *data, int fd, uint32_t active_flags), const void *data)
{
    struct sol_fd_glib *fd_handle = malloc(sizeof(*fd_handle));

    SOL_NULL_CHECK(fd_handle, NULL);

    fd_handle->refcnt = 1;

    fd_handle->cb = cb;
    fd_handle->data = data;
    fd_handle->fd = fd;
    fd_handle->flags = flags;

    fd_handle->id = g_unix_fd_add_full(0, fd, sol_to_glib_flags(flags), on_fd, fd_handle, unref_fd);

    return fd_handle;
}

bool
sol_mainloop_impl_fd_del(void *handle)
{
    struct sol_fd_glib *fd_handle = handle;

    SOL_NULL_CHECK(fd_handle, false);

    return g_source_remove(fd_handle->id);
}

bool
sol_mainloop_impl_fd_set_flags(void *handle, uint32_t flags)
{
    struct sol_fd_glib *fd_handle = handle;

    SOL_NULL_CHECK(fd_handle, false);

    if (fd_handle->flags == flags)
        return true;

    /* remove the watch but keep the data */
    fd_handle->refcnt++;
    if (!g_source_remove(fd_handle->id))
        return false;

    fd_handle->flags = flags;
    fd_handle->id = g_unix_fd_add_full(0, fd_handle->fd, sol_to_glib_flags(flags), on_fd, fd_handle, unref_fd);
    if (fd_handle->id <= 0) {
        SOL_WRN("Error setting new flags");
        unref_fd(fd_handle);
        return false;
    }

    return true;
}

uint32_t
sol_mainloop_impl_fd_get_flags(const void *handle)
{
    const struct sol_fd_glib *fd_handle = handle;

    SOL_NULL_CHECK(fd_handle, 0);

    return fd_handle->flags;
}

struct child_wrap_data {
    const void *data;
    void (*cb)(void *data, uint64_t pid, int status);
};

static void
on_child(GPid pid, gint status, gpointer user_data)
{
    struct child_wrap_data *wrap_data = user_data;

    wrap_data->cb((void *)wrap_data->data, pid, status);
}

void *
sol_mainloop_impl_child_watch_add(uint64_t pid, void (*cb)(void *data, uint64_t pid, int status), const void *data)
{
    struct child_wrap_data *wrap_data = malloc(sizeof(*wrap_data));

    SOL_NULL_CHECK(wrap_data, NULL);

    wrap_data->cb = cb;
    wrap_data->data = data;

    return (void *)(long)g_child_watch_add_full(0, pid, on_child, wrap_data, free);
}

bool
sol_mainloop_impl_child_watch_del(void *handle)
{
    return g_source_remove((uintptr_t)handle);
}

struct source_wrap_data {
    GSource base;
    const struct sol_mainloop_source_type *type;
    const void *data;
};

static gboolean
source_prepare(GSource *source, gint *timeout)
{
    struct source_wrap_data *wrap_data = (struct source_wrap_data *)source;

    if (wrap_data->type->get_next_timeout) {
        struct timespec ts;
        if (wrap_data->type->get_next_timeout((void *)wrap_data->data, &ts)) {
            if (ts.tv_sec < 0) {
                ts.tv_sec = 0;
                ts.tv_nsec = 0;
            }
            *timeout = sol_util_msec_from_timespec(&ts);
        }
    }

    if (wrap_data->type->prepare)
        return wrap_data->type->prepare((void *)wrap_data->data);
    else
        return FALSE;
}

static gboolean
source_check(GSource *source)
{
    struct source_wrap_data *wrap_data = (struct source_wrap_data *)source;

    if (wrap_data->type->check)
        return wrap_data->type->check((void *)wrap_data->data);
    else
        return FALSE;
}

static gboolean
source_dispatch(GSource *source, GSourceFunc cb, gpointer user_data)
{
    struct source_wrap_data *wrap_data = (struct source_wrap_data *)source;

    wrap_data->type->dispatch((void *)wrap_data->data);
    return TRUE;
}

static void
source_finalize(GSource *source)
{
    struct source_wrap_data *wrap_data = (struct source_wrap_data *)source;

    if (wrap_data->type->dispose)
        wrap_data->type->dispose((void *)wrap_data->data);
}

static GSourceFuncs source_funcs = {
    .prepare = source_prepare,
    .check = source_check,
    .dispatch = source_dispatch,
    .finalize = source_finalize,
};

void *
sol_mainloop_impl_source_new(const struct sol_mainloop_source_type *type, const void *data)
{
    struct source_wrap_data *wrap_data;
    guint id;

    wrap_data = (struct source_wrap_data *)g_source_new(&source_funcs,
        sizeof(struct source_wrap_data));
    SOL_NULL_CHECK(wrap_data, NULL);

    wrap_data->type = type;
    wrap_data->data = data;

    id = g_source_attach(&wrap_data->base, NULL);
    SOL_INT_CHECK_GOTO(id, == 0, error);

    return wrap_data;

error:
    g_source_destroy(&wrap_data->base);
    g_source_unref(&wrap_data->base);
    return NULL;
}

void
sol_mainloop_impl_source_del(void *handle)
{
    struct source_wrap_data *wrap_data = handle;

    g_source_destroy(&wrap_data->base);
    g_source_unref(&wrap_data->base);
}

void *
sol_mainloop_impl_source_get_data(const void *handle)
{
    const struct source_wrap_data *wrap_data = handle;

    return (void *)wrap_data->data;
}
