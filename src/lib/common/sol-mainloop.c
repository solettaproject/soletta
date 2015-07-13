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

#include "sol-mainloop-impl.h"
#include "sol-macros.h"
#include "sol-util.h"

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

#ifdef HAVE_PIN_MUX
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
#ifdef FLOW
extern int sol_flow_init(void);
extern void sol_flow_shutdown(void);
#endif

static int _init_count;
static bool mainloop_running;
static int mainloop_return_code;
static int _argc;
static char **_argv;

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

    r = sol_mainloop_impl_init();
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

#ifdef FLOW
    r = sol_flow_init();
    if (r < 0)
        goto flow_error;
#endif

    SOL_DBG("initialized");

    return 0;

flow_error:
    sol_blob_shutdown();
blob_error:
    sol_pin_mux_shutdown();
pin_mux_error:
    sol_platform_shutdown();
platform_error:
    sol_mainloop_impl_shutdown();
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
    sol_mainloop_impl_run();
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
    sol_mainloop_impl_quit();
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
#ifdef FLOW
    sol_flow_shutdown();
#endif
    sol_blob_shutdown();
    sol_pin_mux_shutdown();
    sol_platform_shutdown();
    sol_mainloop_impl_shutdown();
    sol_log_shutdown();
}

SOL_API struct sol_timeout *
sol_timeout_add(unsigned int timeout_ms, bool (*cb)(void *data), const void *data)
{
    SOL_NULL_CHECK(cb, NULL);
    return sol_mainloop_impl_timeout_add(timeout_ms, cb, data);
}

SOL_API bool
sol_timeout_del(struct sol_timeout *handle)
{
    SOL_NULL_CHECK(handle, false);
    return sol_mainloop_impl_timeout_del(handle);
}

SOL_API struct sol_idle *
sol_idle_add(bool (*cb)(void *data), const void *data)
{
    SOL_NULL_CHECK(cb, NULL);
    return sol_mainloop_impl_idle_add(cb, data);
}

SOL_API bool
sol_idle_del(struct sol_idle *handle)
{
    SOL_NULL_CHECK(handle, false);
    return sol_mainloop_impl_idle_del(handle);
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

    if (unlikely(!callbacks || !callbacks->startup)) {
        SOL_CRI("Missing startup function.");
        return EXIT_FAILURE;
    }

    if (unlikely(sol_init() < 0)) {
        SOL_CRI("Cannot initialize soletta.");
        return EXIT_FAILURE;
    }

    _argc = argc;
    _argv = argv;
    sol_idle_add(idle_startup, (void *)callbacks);

    r = sol_run();

    if (callbacks->shutdown)
        callbacks->shutdown();

    sol_shutdown();

    return r;
}
