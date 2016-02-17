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

#include <stdio.h>
#include <uv.h>
#include <sol-mainloop.h>
#include <sol-log.h>

#define RESOLVE_MAINLOOP_STATE(state) \
    ( state == MAINLOOP_HIJACKING_STARTED ? "MAINLOOP_HIJACKING_STARTED" : \
    state == MAINLOOP_HIJACKED ? "MAINLOOP_HIJACKED" : \
    state == MAINLOOP_RELEASING_STARTED ? "MAINLOOP_RELEASING_STARTED" : \
    state == MAINLOOP_RELEASED ? "MAINLOOP_RELEASED" : "Unknown" )

enum MainloopState {
    MAINLOOP_HIJACKING_STARTED,
    MAINLOOP_HIJACKED,
    MAINLOOP_RELEASING_STARTED,
    MAINLOOP_RELEASED
};

static enum MainloopState mainloopState = MAINLOOP_RELEASED;
static uv_idle_t uv_idle;
static struct sol_mainloop_source *uv_loop_source = NULL;
static struct sol_fd *uv_loop_fd = NULL;

static void
uv_idle_callback()
{
    SOL_DBG("Entering with state %s", RESOLVE_MAINLOOP_STATE(mainloopState));
    if (mainloopState == MAINLOOP_HIJACKING_STARTED) {
        SOL_DBG("running sol_run()");
        mainloopState = MAINLOOP_HIJACKED;
        sol_run();
        SOL_DBG("sol_run() has returned. state is %s",
            RESOLVE_MAINLOOP_STATE(mainloopState));
        if (mainloopState == MAINLOOP_RELEASING_STARTED) {
            mainloopState = MAINLOOP_RELEASED;
        }
    } else if ( mainloopState == MAINLOOP_HIJACKED) {
        SOL_DBG("main loop already hijacked. Stopping idler");
        uv_idle_stop(&uv_idle);
    }
}

static bool
uv_loop_source_check(void *data)
{
    uv_loop_t *loop = (uv_loop_t *)data;

    uv_update_time(loop);

    SOL_DBG("Returning %s", uv_loop_alive(loop) ? "true" : "false");
    return uv_loop_alive(loop);
}

static bool
uv_loop_source_get_next_timeout(void *data,
    struct timespec *timeout)
{
    int t = uv_backend_timeout((uv_loop_t *)data);

    SOL_DBG("t = %d", t);

    timeout->tv_sec = (int)(t / 1000);
    timeout->tv_nsec = (t % 1000) * 1000000;

    return ( t >= 0 ) && uv_loop_source_check(data);
}

static void
uv_loop_source_dispatch(void *data)
{
    SOL_DBG("Running one uv loop iteration");
    uv_run((uv_loop_t *)data, UV_RUN_NOWAIT);
}

static const struct sol_mainloop_source_type uv_loop_source_funcs = {
#ifdef SOL_MAINLOOP_SOURCE_TYPE_API_VERSION
    .api_version = SOL_MAINLOOP_SOURCE_TYPE_API_VERSION,
#endif /* def SOL_MAINLOOP_SOURCE_TYPE_API_VERSION */
    .prepare = NULL,
    .get_next_timeout = uv_loop_source_get_next_timeout,
    .check = uv_loop_source_check,
    .dispatch = uv_loop_source_dispatch,
    .dispose = NULL
};

static bool
uv_loop_fd_changed(void *data, int fd, uint32_t active_flags)
{
    SOL_DBG("Running one uv loop iteration");
    uv_run((uv_loop_t *)data, UV_RUN_NOWAIT);
    return true;
}

static void
hijack_main_loop()
{
    SOL_DBG("Entering with state %s", RESOLVE_MAINLOOP_STATE(mainloopState));
    if (mainloopState == MAINLOOP_HIJACKED ||
        mainloopState == MAINLOOP_HIJACKING_STARTED) {
        return;
    }

    // The actual hijacking starts here, inspired by node-gtk. The plan:
    // 1. Attach a source to the soletta main loop which will run the uv main
    //    loop in a non-blocking fashion.
    // 2. Attach an idler to the uv main loop and call sol_run() from it when
    //    it first runs. This interrupts the uv main loop, because sol_run()
    //    doesn't return but, since we've already added the above source to the
    //    soletta main loop in the first step, the source will end up running
    //    one non-blocking iteration of the uv main loop which, in turn, will
    //    recursively call the idler we added. At that point, the ilder can
    //    remove itself from the uv main loop. After that, only the soletta
    //    main loop runs, but it runs an interation of the uv main loop in a
    //    non-blocking fashion whenever the uv main loop signals to the
    //    soletta main loop via the attached source.

    mainloopState = MAINLOOP_HIJACKING_STARTED;

    // We allocate a main loop and a source only once. After that, we simply
    // start/stop the loop.
    if (!uv_loop_source) {
        SOL_DBG("Allocating loop-related variables");
        uv_loop_t *uv_loop = uv_default_loop();
        uv_loop_source = sol_mainloop_source_add(&uv_loop_source_funcs,
            (const void *)uv_loop);
        uv_loop_fd = sol_fd_add(uv_backend_fd(uv_loop),
            SOL_FD_FLAGS_IN | SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR,
            uv_loop_fd_changed, (const void *)uv_loop);
        uv_idle_init(uv_loop, &uv_idle);
    }

    SOL_DBG("Starting idler");
    uv_idle_start(&uv_idle, uv_idle_callback);
}

static void
release_main_loop()
{
    SOL_DBG("Entering with state %s\n", RESOLVE_MAINLOOP_STATE(mainloopState));
    if (mainloopState == MAINLOOP_RELEASED ||
        mainloopState == MAINLOOP_RELEASING_STARTED) {
        return;
    }

    // hijack_main_loop() was called, but the idler has not yet run
    if (mainloopState == MAINLOOP_HIJACKING_STARTED) {
        SOL_DBG("idler has not yet run, so stopping it");
        uv_idle_stop(&uv_idle);
        mainloopState = MAINLOOP_RELEASED;
    } else {
        SOL_DBG("quitting main loop");
        mainloopState = MAINLOOP_RELEASING_STARTED;
        sol_quit();
    }
}

static int hijack_refcount = 0;

void
hijack_ref()
{
    SOL_DBG("Entering");
    if (hijack_refcount == 0) {
        SOL_DBG("hijacking main loop");
        hijack_main_loop();
    }
    hijack_refcount++;
}

void
hijack_unref()
{
    SOL_DBG("Entering\n");
    hijack_refcount--;
    if (hijack_refcount == 0) {
        SOL_DBG("releasing main loop");
        release_main_loop();
    }
}
