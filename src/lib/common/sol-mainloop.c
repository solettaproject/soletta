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

#include <stdlib.h>
#include <stdio.h>

#include "sol-mainloop-impl.h"
#include "sol-macros.h"
#include "sol-modules.h"
#include "sol-util-internal.h"

#include "sol-platform.h"

SOL_LOG_INTERNAL_DECLARE(_sol_mainloop_log_domain, "mainloop");

#ifdef SOL_LOG_ENABLED
extern int sol_log_init(void);
extern void sol_log_shutdown(void);
#else
static inline int
sol_log_init(void)
{
    return 0;
}
static inline void
sol_log_shutdown(void)
{
}
#endif

#ifdef USE_PIN_MUX
extern int sol_pin_mux_init(void);
extern void sol_pin_mux_shutdown(void);
#else
static inline int
sol_pin_mux_init(void)
{
    return 0;
}
static inline void
sol_pin_mux_shutdown(void)
{
}
#endif

extern int sol_platform_init(void);
extern void sol_platform_shutdown(void);
extern int sol_blob_init(void);
extern void sol_blob_shutdown(void);
extern int sol_crypto_init(void);
extern void sol_crypto_shutdown(void);
#ifdef FLOW_SUPPORT
extern int sol_flow_init(void);
extern void sol_flow_shutdown(void);
#endif
#ifdef NETWORK
extern int sol_comms_init(void);
extern void sol_comms_shutdown(void);
#endif
#ifdef USE_UPDATE
extern int sol_update_init(void);
extern void sol_update_shutdown(void);
#endif

static const struct sol_mainloop_implementation _sol_mainloop_implementation_default = {
    SOL_SET_API_VERSION(.api_version = SOL_MAINLOOP_IMPLEMENTATION_API_VERSION, )
    .init = sol_mainloop_impl_init,
    .shutdown =  sol_mainloop_impl_shutdown,
    .run =  sol_mainloop_impl_run,
    .quit =  sol_mainloop_impl_quit,
    .timeout_add =  sol_mainloop_impl_timeout_add,
    .timeout_del =  sol_mainloop_impl_timeout_del,
    .idle_add =  sol_mainloop_impl_idle_add,
    .idle_del =  sol_mainloop_impl_idle_del,

#ifdef SOL_MAINLOOP_FD_ENABLED
    .fd_add =  sol_mainloop_impl_fd_add,
    .fd_del =  sol_mainloop_impl_fd_del,
    .fd_set_flags =  sol_mainloop_impl_fd_set_flags,
    .fd_get_flags =  sol_mainloop_impl_fd_get_flags,
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
    .child_watch_add =  sol_mainloop_impl_child_watch_add,
    .child_watch_del =  sol_mainloop_impl_child_watch_del,
#endif

    .source_add =  sol_mainloop_impl_source_add,
    .source_del =  sol_mainloop_impl_source_del,
    .source_get_data =  sol_mainloop_impl_source_get_data,
};

static int _init_count;
static bool mainloop_running;
static int mainloop_return_code;
static int _argc;
static char **_argv;
static const struct sol_mainloop_implementation *mainloop_impl = &_sol_mainloop_implementation_default;

SOL_API const struct sol_mainloop_implementation *SOL_MAINLOOP_IMPLEMENTATION_DEFAULT = &_sol_mainloop_implementation_default;


SOL_API int
sol_init(void)
{
    int r;

    _init_count++;
    if (_init_count > 1)
        return 0;

    r = sol_log_init();
    if (r < 0)
        goto log_error;

    sol_log_domain_init_level(SOL_LOG_DOMAIN);

    r = mainloop_impl->init();
    if (r < 0)
        goto impl_error;

    r = sol_platform_init();
    if (r < 0)
        goto platform_error;

    r = sol_pin_mux_init();
    if (r < 0)
        goto pin_mux_error;

    r = sol_blob_init();
    if (r < 0)
        goto blob_error;

    r = sol_crypto_init();
    if (r < 0)
        goto crypto_error;

#ifdef FLOW_SUPPORT
    r = sol_flow_init();
    if (r < 0)
        goto flow_error;
#endif

#ifdef NETWORK
    r = sol_comms_init();
    if (r < 0)
        goto comms_error;
#endif

#ifdef USE_UPDATE
    r = sol_update_init();
    if (r < 0)
        goto update_error;
#endif

    SOL_DBG("Soletta %s on %s-%s initialized",
        sol_platform_get_sw_version(), BASE_OS,
        sol_platform_get_os_version());

    return 0;

#ifdef USE_UPDATE
update_error:
#endif
#ifdef NETWORK
comms_error:
#endif
#ifdef FLOW_SUPPORT
    sol_flow_shutdown();
flow_error:
#endif
    sol_crypto_shutdown();
crypto_error:
    sol_blob_shutdown();
blob_error:
    sol_pin_mux_shutdown();
pin_mux_error:
    sol_platform_shutdown();
platform_error:
    mainloop_impl->shutdown();
impl_error:
    sol_log_shutdown();
log_error:
    _init_count = 0;
    return r;
}

SOL_API int
sol_run(void)
{
    if (_init_count == 0) {
        SOL_CRI("sol_init() was not called");
        return EXIT_FAILURE;
    }
    if (mainloop_running) {
        SOL_CRI("Mainloop already running");
        return EXIT_FAILURE;
    }

    SOL_DBG("run");
    mainloop_running = true;
    mainloop_impl->run();
    return mainloop_return_code;
}

SOL_API void
sol_quit(void)
{
    sol_quit_with_code(EXIT_SUCCESS);
}

SOL_API void
sol_quit_with_code(int return_code)
{
    if (_init_count == 0) {
        SOL_CRI("sol_init() was not called");
        return;
    }
    if (!mainloop_running) {
        SOL_DBG("Mainloop was not running");
        return;
    }

    SOL_DBG("quit with code %d", return_code);
    mainloop_return_code = return_code;
    mainloop_running = false;
    mainloop_impl->quit();
}

SOL_API void
sol_shutdown(void)
{
    if (_init_count == 0) {
        SOL_CRI("sol_init() was not called");
        return;
    }
    _init_count--;
    if (_init_count > 0)
        return;

    SOL_DBG("shutdown");
#ifdef NETWORK
    sol_comms_shutdown();
#endif
#ifdef FLOW_SUPPORT
    sol_flow_shutdown();
#endif
    sol_crypto_shutdown();
    sol_blob_shutdown();
    sol_pin_mux_shutdown();
    sol_platform_shutdown();
    mainloop_impl->shutdown();
    sol_modules_clear_cache();
#ifdef USE_UPDATE
    sol_update_shutdown();
#endif

    sol_log_shutdown();
}

SOL_API struct sol_timeout *
sol_timeout_add(uint32_t timeout_ms, bool (*cb)(void *data), const void *data)
{
    SOL_NULL_CHECK(cb, NULL);
    return mainloop_impl->timeout_add(timeout_ms, cb, data);
}

SOL_API bool
sol_timeout_del(struct sol_timeout *handle)
{
    SOL_NULL_CHECK(handle, false);
    return mainloop_impl->timeout_del(handle);
}

SOL_API struct sol_idle *
sol_idle_add(bool (*cb)(void *data), const void *data)
{
    SOL_NULL_CHECK(cb, NULL);
    return mainloop_impl->idle_add(cb, data);
}

SOL_API bool
sol_idle_del(struct sol_idle *handle)
{
    SOL_NULL_CHECK(handle, false);
    return mainloop_impl->idle_del(handle);
}

#ifdef SOL_MAINLOOP_FD_ENABLED
SOL_API struct sol_fd *
sol_fd_add(int fd, uint32_t flags, bool (*cb)(void *data, int fd, uint32_t active_flags), const void *data)
{
    SOL_NULL_CHECK(cb, NULL);
    return mainloop_impl->fd_add(fd, flags, cb, data);
}

SOL_API bool
sol_fd_del(struct sol_fd *handle)
{
    SOL_NULL_CHECK(handle, false);
    return mainloop_impl->fd_del(handle);
}

SOL_API bool
sol_fd_set_flags(struct sol_fd *handle, uint32_t flags)
{
    SOL_NULL_CHECK(handle, false);
    return mainloop_impl->fd_set_flags(handle, flags);
}

SOL_API uint32_t
sol_fd_get_flags(const struct sol_fd *handle)
{
    SOL_NULL_CHECK(handle, false);
    return mainloop_impl->fd_get_flags(handle);
}

SOL_API bool
sol_fd_unset_flags(struct sol_fd *handle, uint32_t flags)
{
    SOL_NULL_CHECK(handle, false);
    return mainloop_impl->fd_set_flags(handle, mainloop_impl->fd_get_flags(handle) & ~flags);
}
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
SOL_API struct sol_child_watch *
sol_child_watch_add(uint64_t pid, void (*cb)(void *data, uint64_t pid, int status), const void *data)
{
    SOL_INT_CHECK(pid, < 1, NULL);
    SOL_NULL_CHECK(cb, NULL);
    return mainloop_impl->child_watch_add(pid, cb, data);
}

SOL_API bool
sol_child_watch_del(struct sol_child_watch *handle)
{
    SOL_NULL_CHECK(handle, false);
    return mainloop_impl->child_watch_del(handle);
}
#endif

SOL_API struct sol_mainloop_source *
sol_mainloop_source_add(const struct sol_mainloop_source_type *type, const void *data)
{
    SOL_NULL_CHECK(type, NULL);

#ifndef SOL_NO_API_VERSION
    if (type->api_version != SOL_MAINLOOP_SOURCE_TYPE_API_VERSION) {
        SOL_WRN("type(%p)->api_version(%hu) != "
            "SOL_MAINLOOP_SOURCE_TYPE_API_VERSION(%hu)",
            type, type->api_version,
            SOL_MAINLOOP_SOURCE_TYPE_API_VERSION);
        return NULL;
    }
#endif

    SOL_NULL_CHECK(type->check, NULL);
    SOL_NULL_CHECK(type->dispatch, NULL);

    return mainloop_impl->source_add(type, data);
}

SOL_API void
sol_mainloop_source_del(struct sol_mainloop_source *handle)
{
    SOL_NULL_CHECK(handle);
    mainloop_impl->source_del(handle);
}

SOL_API void *
sol_mainloop_source_get_data(const struct sol_mainloop_source *handle)
{
    SOL_NULL_CHECK(handle, NULL);
    return mainloop_impl->source_get_data(handle);
}

SOL_API const struct sol_mainloop_implementation *
sol_mainloop_get_implementation(void)
{
    return mainloop_impl;
}

SOL_API bool
sol_mainloop_set_implementation(const struct sol_mainloop_implementation *impl)
{
    SOL_NULL_CHECK(impl, false);

#ifndef SOL_NO_API_VERSION
    if (impl->api_version != SOL_MAINLOOP_IMPLEMENTATION_API_VERSION) {
        SOL_WRN("impl(%p)->api_version(%hu) != "
            "SOL_MAINLOOP_IMPLEMENTATION_API_VERSION(%hu)",
            impl, impl->api_version,
            SOL_MAINLOOP_IMPLEMENTATION_API_VERSION);
        return false;
    }
#endif

    SOL_NULL_CHECK(impl->init, false);
    SOL_NULL_CHECK(impl->shutdown, false);
    SOL_NULL_CHECK(impl->run, false);
    SOL_NULL_CHECK(impl->quit, false);
    SOL_NULL_CHECK(impl->timeout_add, false);
    SOL_NULL_CHECK(impl->timeout_del, false);
    SOL_NULL_CHECK(impl->idle_add, false);
    SOL_NULL_CHECK(impl->idle_del, false);

#ifdef SOL_MAINLOOP_FD_ENABLED
    SOL_NULL_CHECK(impl->fd_add, false);
    SOL_NULL_CHECK(impl->fd_del, false);
    SOL_NULL_CHECK(impl->fd_set_flags, false);
    SOL_NULL_CHECK(impl->fd_get_flags, false);
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
    SOL_NULL_CHECK(impl->child_watch_add, false);
    SOL_NULL_CHECK(impl->child_watch_del, false);
#endif

    SOL_NULL_CHECK(impl->source_add, false);
    SOL_NULL_CHECK(impl->source_del, false);
    SOL_NULL_CHECK(impl->source_get_data, false);

    SOL_INT_CHECK(_init_count, > 0, false);

    mainloop_impl = impl;
    return true;
}

SOL_API int
sol_argc(void)
{
    return _argc;
}

SOL_API char **
sol_argv(void)
{
    return _argv;
}

SOL_API void
sol_args_set(int argc, char *argv[])
{
    _argc = argc;
    _argv = argv;
}

static bool
idle_startup(void *data)
{
    const struct sol_main_callbacks *callbacks = data;

    callbacks->startup();
    return false;
}

SOL_API int
sol_mainloop_default_main(const struct sol_main_callbacks *callbacks, int argc, char *argv[])
{
    int r;

    _argc = argc;
    _argv = argv;

    if (SOL_UNLIKELY(!callbacks || !callbacks->startup)) {
        fprintf(stderr, "Missing startup function.\n");
        return EXIT_FAILURE;
    }

    if (SOL_UNLIKELY(sol_init() < 0)) {
        return EXIT_FAILURE;
    }

    sol_idle_add(idle_startup, (void *)callbacks);

    r = sol_run();

    if (callbacks->shutdown)
        callbacks->shutdown();

    sol_shutdown();

    return r;
}
