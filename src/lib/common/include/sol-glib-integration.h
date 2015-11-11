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

/**
 * @file sol-glib-integration.h
 *
 * This file is to be included by GMainLoop users. It will make sure
 * Glib's main loop is usable with Soletta in the case of Soletta
 * being compiled with glib as mainloop or any other such as POSIX.
 *
 * Include this file and call sol_glib_integration().
 */

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <sol-mainloop.h>
#include <sol-log.h>
#include <sol-vector.h>

static gboolean
_sol_glib_integration_gsource_prepare(GSource *source, gint *timeout)
{
    return FALSE;
}

static gboolean
_sol_glib_integration_gsource_check(GSource *source)
{
    return FALSE;
}

static gboolean
_sol_glib_integration_gsource_dispatch(GSource *source, GSourceFunc cb, gpointer user_data)
{
    return TRUE;
}

static GSourceFuncs _sol_glib_integration_gsource_funcs = {
    .prepare = _sol_glib_integration_gsource_prepare,
    .check = _sol_glib_integration_gsource_check,
    .dispatch = _sol_glib_integration_gsource_dispatch,
};

static gboolean
_sol_glib_integration_gsource_cb(gpointer user_data)
{
    return TRUE;
}

struct _sol_glib_integration_fd_handler {
    struct sol_fd *watch;
    struct _sol_glib_integration_source_data *mdata;
    int fd;
    gushort events;
};

static void
_sol_glib_integration_fd_handler_del(struct _sol_glib_integration_fd_handler *h)
{
    sol_fd_del(h->watch);
    free(h);
}

struct _sol_glib_integration_source_data {
    GSource gsource;
    struct sol_ptr_vector handlers;
    GPollFD *fds;
    gint n_fds;
    gint n_poll;
    gint timeout;
    gint max_prio;
};

static uint32_t
_sol_glib_integration_gpoll_events_to_fd_flags(gushort gpoll_events)
{
    uint32_t sol_flags = 0;

    if (gpoll_events & G_IO_IN) sol_flags |= SOL_FD_FLAGS_IN;
    if (gpoll_events & G_IO_OUT) sol_flags |= SOL_FD_FLAGS_OUT;
    if (gpoll_events & G_IO_PRI) sol_flags |= SOL_FD_FLAGS_PRI;
    if (gpoll_events & G_IO_ERR) sol_flags |= SOL_FD_FLAGS_ERR;
    if (gpoll_events & G_IO_HUP) sol_flags |= SOL_FD_FLAGS_HUP;
    if (gpoll_events & G_IO_NVAL) sol_flags |= SOL_FD_FLAGS_NVAL;

    return sol_flags;
}

static gushort
_sol_glib_integration_fd_flags_to_gpoll_events(uint32_t sol_flags)
{
    gushort glib_flags = 0;

    if (sol_flags & SOL_FD_FLAGS_IN) glib_flags |= G_IO_IN;
    if (sol_flags & SOL_FD_FLAGS_OUT) glib_flags |= G_IO_OUT;
    if (sol_flags & SOL_FD_FLAGS_PRI) glib_flags |= G_IO_PRI;
    if (sol_flags & SOL_FD_FLAGS_ERR) glib_flags |= G_IO_ERR;
    if (sol_flags & SOL_FD_FLAGS_HUP) glib_flags |= G_IO_HUP;
    if (sol_flags & SOL_FD_FLAGS_NVAL) glib_flags |= G_IO_NVAL;

    return glib_flags;
}

static GPollFD *
_sol_glib_integration_source_gpollfd_find(struct _sol_glib_integration_source_data *mdata, int fd)
{
    gint i;

    for (i = 0; i < mdata->n_poll; i++) {
        GPollFD *gpfd = mdata->fds + i;
        if (gpfd->fd == fd)
            return gpfd;
    }

    return NULL;
}

static struct _sol_glib_integration_fd_handler *
_sol_glib_integration_source_fd_handler_data_find(struct _sol_glib_integration_source_data *mdata, int fd)
{
    uint16_t i;
    struct _sol_glib_integration_fd_handler *h;

    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->handlers, h, i) {
        if (h->fd == fd)
            return h;
    }

    return NULL;
}

static bool
_sol_glib_integration_on_source_fd(void *data, int fd, uint32_t active_flags)
{
    struct _sol_glib_integration_fd_handler *h = data;
    GPollFD *gpfd = _sol_glib_integration_source_gpollfd_find(h->mdata, fd);

    SOL_NULL_CHECK(gpfd, true);
    gpfd->revents = _sol_glib_integration_fd_flags_to_gpoll_events(active_flags);

    return true;
}

static void
_sol_glib_integration_source_fd_handlers_adjust(struct _sol_glib_integration_source_data *mdata)
{
    uint16_t i;
    struct _sol_glib_integration_fd_handler *h;

    // 1 - cleanup fd handlers that are not needed or changed
    // Walk backwards so deletion doesn't impact the indices.
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&mdata->handlers, h, i) {
        GPollFD *gpfd = _sol_glib_integration_source_gpollfd_find(mdata, h->fd);

        if (gpfd && gpfd->events == h->events)
            continue;

        if (gpfd)
            SOL_DBG("glib fd=%d, changed events %#x -> %#x", h->fd, h->events, gpfd->events);
        else
            SOL_DBG("glib fd=%d is not needed anymore", h->fd);
        _sol_glib_integration_fd_handler_del(h);
        sol_ptr_vector_del(&mdata->handlers, i);
    }

    // 2 - create fd handlers for new or changed fds
    for (i = 0; i < (uint16_t)mdata->n_poll; i++) {
        const GPollFD *gpfd = mdata->fds + i;
        uint32_t flags;
        int r;

        h = _sol_glib_integration_source_fd_handler_data_find(mdata, gpfd->fd);
        if (h)
            continue;

        h = malloc(sizeof(struct _sol_glib_integration_fd_handler));
        SOL_NULL_CHECK(h);

        h->fd = gpfd->fd;
        h->events = gpfd->events;
        h->mdata = mdata;

        flags = _sol_glib_integration_gpoll_events_to_fd_flags(gpfd->events);

        h->watch = sol_fd_add(gpfd->fd,
            flags, _sol_glib_integration_on_source_fd, h);
        SOL_NULL_CHECK_GOTO(h->watch, watch_failed);

        r = sol_ptr_vector_append(&mdata->handlers, h);
        SOL_INT_CHECK_GOTO(r, < 0, append_failed);

        SOL_DBG("glib fd=%d monitoring events %#x", h->fd, h->events);
        continue;

append_failed:
        sol_fd_del(h->watch);
watch_failed:
        free(h);
        return;
    }
}

static bool
_sol_glib_integration_source_acquire(struct _sol_glib_integration_source_data *mdata)
{
    GMainContext *ctx = g_source_get_context(&mdata->gsource);
    gboolean r = g_main_context_acquire(ctx);

    /* NOTE: not doing wait() here, should we? */
    if (!r)
        SOL_WRN("couldn't acquire glib's context.");

    return r;
}

static void
_sol_glib_integration_source_release(struct _sol_glib_integration_source_data *mdata)
{
    GMainContext *ctx = g_source_get_context(&mdata->gsource);

    g_main_context_release(ctx);
}

static inline unsigned int
_sol_glib_integration_align_power2(unsigned int u)
{
    unsigned int left_zeros;

    if (u == 1)
        return 1;
    if ((left_zeros = __builtin_clz(u - 1)) < 1)
        return 0;
    return 1 << ((sizeof(u) * 8) - left_zeros);
}

static bool
_sol_glib_integration_source_prepare(void *data)
{
    struct _sol_glib_integration_source_data *mdata = data;
    GMainContext *ctx = g_source_get_context(&mdata->gsource);
    bool ready;
    gint req_n_fds;

    if (!_sol_glib_integration_source_acquire(mdata))
        return false;

    ready = g_main_context_prepare(ctx, &mdata->max_prio);

    /* NOTE: this shouldn't require a loop, but gmain.c does it, so we mimic
     * such behavior here.
     */
    do {
        size_t byte_size;
        void *tmp;

        mdata->n_poll = g_main_context_query(ctx,
            mdata->max_prio, &mdata->timeout, mdata->fds, mdata->n_fds);
        req_n_fds = _sol_glib_integration_align_power2(mdata->n_poll);

        if (req_n_fds == mdata->n_fds)
            break;

        /* NOTE: not using sol_util_size_mul() since sol-util.h is not
         * installed and this file is to be compiled by soletta's
         * users.
         */
        byte_size = req_n_fds * sizeof(GPollFD);
        tmp = realloc(mdata->fds, byte_size);
        if (byte_size > 0) {
            SOL_NULL_CHECK_GOTO(tmp, failed);
            memset(tmp, 0, byte_size);
        }

        mdata->fds = tmp;
        mdata->n_fds = req_n_fds;
    } while (1);

    _sol_glib_integration_source_release(mdata);

    _sol_glib_integration_source_fd_handlers_adjust(mdata);

    return ready;

failed:
    _sol_glib_integration_source_release(mdata);
    return false;
}

static bool
_sol_glib_integration_source_get_next_timeout(void *data, struct timespec *ts)
{
    struct _sol_glib_integration_source_data *mdata = data;

    if (mdata->timeout < 0)
        return false;

    /* NOTE: not using sol_util_timespec_from_msec() since sol-util.h
     * is not installed and this file is to be compiled by soletta's
     * users.
     */
    ts->tv_sec = mdata->timeout / 1000ULL;
    ts->tv_nsec = (mdata->timeout % 1000ULL) * 1000000ULL;
    return true;
}

static bool
_sol_glib_integration_source_check(void *data)
{
    struct _sol_glib_integration_source_data *mdata = data;
    GMainContext *ctx = g_source_get_context(&mdata->gsource);
    bool ready;

    if (!_sol_glib_integration_source_acquire(mdata))
        return false;

    ready = g_main_context_check(ctx,
        mdata->max_prio, mdata->fds, mdata->n_poll);

    _sol_glib_integration_source_release(mdata);

    return ready;
}

static void
_sol_glib_integration_source_dispatch(void *data)
{
    struct _sol_glib_integration_source_data *mdata = data;
    GMainContext *ctx = g_source_get_context(&mdata->gsource);

    if (!_sol_glib_integration_source_acquire(mdata))
        return;

    g_main_context_dispatch(ctx);

    _sol_glib_integration_source_release(mdata);
}

static void
_sol_glib_integration_source_dispose(void *data)
{
    struct _sol_glib_integration_source_data *mdata = data;
    GMainContext *ctx = g_source_get_context(&mdata->gsource);
    uint16_t i;
    struct _sol_glib_integration_fd_handler *h;

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&mdata->handlers, h, i) {
        _sol_glib_integration_fd_handler_del(h);
    }
    sol_ptr_vector_clear(&mdata->handlers);

    free(mdata->fds);

    g_source_destroy(&mdata->gsource);
    g_source_unref(&mdata->gsource);

    g_main_context_unref(ctx);
}

static const struct sol_mainloop_source_type _sol_glib_integration_source_type = {
#ifndef SOL_NO_API_VERSION
    .api_version = SOL_MAINLOOP_SOURCE_TYPE_API_VERSION,
#endif
    .prepare = _sol_glib_integration_source_prepare,
    .get_next_timeout = _sol_glib_integration_source_get_next_timeout,
    .check = _sol_glib_integration_source_check,
    .dispatch = _sol_glib_integration_source_dispatch,
    .dispose = _sol_glib_integration_source_dispose
};

static bool
sol_glib_integration(void)
{
    GMainContext *ctx;
    GSource *gsource;
    struct sol_mainloop_source *msource;
    struct _sol_glib_integration_source_data *mdata;
    guint id;

    /* no need to integrate if we're called from glib */
    if (g_main_depth()) {
        SOL_DBG("already running with glib");
        return true;
    }

    ctx = g_main_context_default();

    /* convention: we add a GSource with 'sol_init' as user_data to
     * mark Soletta was integrated with Glib.
     *
     * it is a dummy gsource that does nothing other than exist to hold
     * this mark.
     */
    gsource = g_main_context_find_source_by_user_data(ctx, sol_init);
    if (gsource)
        return true;

    gsource = g_source_new(&_sol_glib_integration_gsource_funcs,
        sizeof(struct _sol_glib_integration_source_data));
    SOL_NULL_CHECK(gsource, false);
    g_source_set_callback(gsource,
        _sol_glib_integration_gsource_cb, sol_init, NULL);

    id = g_source_attach(gsource, ctx);
    SOL_INT_CHECK_GOTO(id, == 0, failed_gsource);

    mdata = (struct _sol_glib_integration_source_data *)gsource;
    sol_ptr_vector_init(&mdata->handlers);
    mdata->fds = NULL;
    mdata->n_fds = 0;
    mdata->n_poll = 0;
    mdata->timeout = -1;
    mdata->max_prio = 0;

    msource = sol_mainloop_source_new(&_sol_glib_integration_source_type, gsource);
    SOL_NULL_CHECK_GOTO(msource, failed_mainloop_source);

    g_main_context_ref(ctx);
    SOL_DBG("glib's mainloop is now integrated");

    return true;

failed_mainloop_source:
    g_source_destroy(gsource);
failed_gsource:
    g_source_unref(gsource);
    SOL_WRN("failed to integrate glib's mainloop");
    return false;
}
