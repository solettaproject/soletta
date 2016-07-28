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

#include <stdlib.h>
#include <stdio.h>

#include "sol-mainloop-impl.h"
#include "sol-macros.h"
#include "sol-modules.h"
#include "sol-util-internal.h"

#include "sol-platform.h"

SOL_LOG_INTERNAL_DECLARE(_sol_mainloop_log_domain, "mainloop");

extern int sol_platform_init(void);
extern void sol_platform_shutdown(void);
extern int sol_blob_init(void);
extern void sol_blob_shutdown(void);
extern int sol_crypto_init(void);
extern void sol_crypto_shutdown(void);

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

#ifdef USE_FLOW
extern int sol_flow_init(void);
extern void sol_flow_shutdown(void);
#else
static inline int
sol_flow_init(void)
{
    return 0;
}

static inline void
sol_flow_shutdown(void)
{
}
#endif

#ifdef NETWORK
extern int sol_comms_init(void);
extern void sol_comms_shutdown(void);
#else
static inline int
sol_comms_init(void)
{
    return 0;
}

static inline void
sol_comms_shutdown(void)
{
}
#endif

#ifdef USE_UPDATE
extern int sol_update_init(void);
extern void sol_update_shutdown(void);
#else
static inline int
sol_update_init(void)
{
    return 0;
}

static inline void
sol_update_shutdown(void)
{
}
#endif

#ifdef USE_IPM
extern int sol_ipm_init(void);
extern void sol_ipm_shutdown(void);
#else
static inline int
sol_ipm_init(void)
{
    return 0;
}

static inline void
sol_ipm_shutdown(void)
{
}
#endif

#ifdef LWM2M
extern int sol_lwm2m_common_init(void);
extern void sol_lwm2m_common_shutdown(void);
#else
static inline int
sol_lwm2m_common_init(void)
{
    return 0;
}

static inline void
sol_lwm2m_common_shutdown(void)
{
}
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

    r = sol_flow_init();
    if (r < 0)
        goto flow_error;

    r = sol_comms_init();
    if (r < 0)
        goto comms_error;

    r = sol_update_init();
    if (r < 0)
        goto update_error;

    r = sol_ipm_init();
    if (r < 0)
        goto ipm_error;

    r = sol_lwm2m_common_init();
    if (r < 0)
        goto lwm2m_common_error;

    SOL_DBG("Soletta %s on %s-%s initialized",
        sol_platform_get_sw_version(), BASE_OS,
        sol_platform_get_os_version());

    return 0;

lwm2m_common_error:
    sol_ipm_shutdown();
ipm_error:
    sol_update_shutdown();
update_error:
    sol_comms_shutdown();
comms_error:
    sol_flow_shutdown();
flow_error:
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
    sol_lwm2m_common_shutdown();
    sol_ipm_shutdown();
    sol_update_shutdown();
    sol_comms_shutdown();
    sol_flow_shutdown();
    sol_crypto_shutdown();
    sol_blob_shutdown();
    sol_pin_mux_shutdown();
    sol_platform_shutdown();
    mainloop_impl->shutdown();
    sol_modules_clear_cache();

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
    SOL_NULL_CHECK_ERRNO(cb, EINVAL, NULL);
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
sol_fd_add_flags(struct sol_fd *handle, uint32_t flags)
{
    uint32_t f;

    SOL_NULL_CHECK(handle, false);

    f = mainloop_impl->fd_get_flags(handle);

    if (flags & f)
        return true;

    return mainloop_impl->fd_set_flags(handle, mainloop_impl->fd_get_flags(handle) | flags);
}

SOL_API bool
sol_fd_remove_flags(struct sol_fd *handle, uint32_t flags)
{
    uint32_t f;

    SOL_NULL_CHECK(handle, false);

    f = mainloop_impl->fd_get_flags(handle);
    if (flags & f)
        return mainloop_impl->fd_set_flags(handle, mainloop_impl->fd_get_flags(handle) & ~flags);
    return true;
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
sol_mainloop_add_source(const struct sol_mainloop_source_type *type, const void *data)
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
sol_mainloop_del_source(struct sol_mainloop_source *handle)
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
    /* Note: can't use SOL_WRN or SOL_NULL_CHECK from this function since
     * it is to be called before sol_init(), thus before log is initialized.
     */
#define NULL_CHECK(ptr) if (!ptr) return false
    NULL_CHECK(impl);

#ifndef SOL_NO_API_VERSION
    if (impl->api_version != SOL_MAINLOOP_IMPLEMENTATION_API_VERSION) {
        return false;
    }
#endif

    NULL_CHECK(impl->init);
    NULL_CHECK(impl->shutdown);
    NULL_CHECK(impl->run);
    NULL_CHECK(impl->quit);
    NULL_CHECK(impl->timeout_add);
    NULL_CHECK(impl->timeout_del);
    NULL_CHECK(impl->idle_add);
    NULL_CHECK(impl->idle_del);

#ifdef SOL_MAINLOOP_FD_ENABLED
    NULL_CHECK(impl->fd_add);
    NULL_CHECK(impl->fd_del);
    NULL_CHECK(impl->fd_set_flags);
    NULL_CHECK(impl->fd_get_flags);
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
    NULL_CHECK(impl->child_watch_add);
    NULL_CHECK(impl->child_watch_del);
#endif

    NULL_CHECK(impl->source_add);
    NULL_CHECK(impl->source_del);
    NULL_CHECK(impl->source_get_data);

#undef NULL_CHECK

    if (_init_count > 0)
        return false;

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
sol_set_args(int argc, char *argv[])
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
