/*
 * This file is part of the Soletta Project
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

#include <stdio.h>
#include <errno.h>
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
    uv_loop_t *loop = data;

    uv_update_time(loop);

    bool returnValue = uv_loop_alive(loop);
    SOL_DBG("Returning %s", returnValue ? "true" : "false");
    return returnValue;
}

static bool
uv_loop_source_get_next_timeout(void *data, struct timespec *timeout)
{
    int uvTimeout = uv_backend_timeout(data);
    bool returnValue = ( uvTimeout >= 0 ) && uv_loop_source_check(data);

    SOL_DBG("uvTimeout = %d", uvTimeout);

    if (returnValue) {
        timeout->tv_sec = (int)(uvTimeout / 1000);
        timeout->tv_nsec = (uvTimeout % 1000) * 1000000;
    }
    return returnValue;
}

static void
uv_loop_source_dispatch(void *data)
{
    SOL_DBG("Running one uv loop iteration");
    uv_run(data, UV_RUN_NOWAIT);
}

static const struct sol_mainloop_source_type uv_loop_source_funcs = {
    SOL_SET_API_VERSION(.api_version = SOL_MAINLOOP_SOURCE_TYPE_API_VERSION, )
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
    uv_run(data, UV_RUN_NOWAIT);
    return true;
}

int
hijack_main_loop()
{
    int returnValue;
    uv_loop_t *uv_loop = NULL;

    SOL_DBG("Entering with state %s", RESOLVE_MAINLOOP_STATE(mainloopState));
    if (mainloopState == MAINLOOP_HIJACKED ||
        mainloopState == MAINLOOP_HIJACKING_STARTED) {
        return 0;
    }

    uv_loop = uv_default_loop();

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
    //    main loop runs, but it runs an iteration of the uv main loop in a
    //    non-blocking fashion whenever the uv main loop signals to the
    //    soletta main loop via the attached source.

    // We allocate the various needed structures only once. After that, we
    // reuse them. We never free them, even if we release the uv main loop.
    if (!uv_loop_source) {
        uv_loop_source = sol_mainloop_add_source(&uv_loop_source_funcs,
            uv_loop);
        if (!uv_loop_source) {
            return -ENOMEM;
        }
    }

    if (!uv_loop_fd) {
        uv_loop_fd = sol_fd_add(uv_backend_fd(uv_loop),
            SOL_FD_FLAGS_IN | SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR,
            uv_loop_fd_changed, uv_loop);
        if (!uv_loop_fd) {
            return -ENOMEM;
        }
    }

    returnValue = uv_idle_init(uv_loop, &uv_idle);
    if (returnValue) {
        return returnValue;
    }

    SOL_DBG("Starting idler");
    returnValue = uv_idle_start(&uv_idle, uv_idle_callback);
    if (returnValue) {
        return returnValue;
    }

    mainloopState = MAINLOOP_HIJACKING_STARTED;
    return 0;
}

int
release_main_loop()
{
    int returnValue = 0;

    SOL_DBG("Entering with state %s", RESOLVE_MAINLOOP_STATE(mainloopState));
    if (mainloopState == MAINLOOP_RELEASED ||
        mainloopState == MAINLOOP_RELEASING_STARTED) {
        return returnValue;
    }

    // hijack_main_loop() was called, but the idler has not run yet
    if (mainloopState == MAINLOOP_HIJACKING_STARTED) {
        SOL_DBG("idler has not run yet, so stopping it");
        returnValue = uv_idle_stop(&uv_idle);
        if (!returnValue) {
            mainloopState = MAINLOOP_RELEASED;
        }
    } else {
        SOL_DBG("quitting main loop");
        mainloopState = MAINLOOP_RELEASING_STARTED;
        sol_quit();
    }
    return returnValue;
}
