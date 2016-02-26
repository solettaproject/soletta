/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#pragma once

#include <sol-log.h>
#include <sol-mainloop.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup Mainloop
 *
 * @{
 */

/**
 * @def SOL_MAIN
 *
 * @brief Convenience macro to declare the @c main function and properly
 * initialize and execute a Soletta Application.
 *
 * @warning Prefer to use @ref SOL_MAIN_DEFAULT since it handles different
 * platforms.
 */

/**
 * @def SOL_MAIN_DEFAULT(startup, shutdown)
 * Preferred entry point for Soletta applications.
 *
 * Different platforms may have different ways in which an application is
 * started, so in order to remain portable, Soletta applications should avoid
 * using platform specific main functions.
 *
 * SOL_MAIN_DEFAULT will be defined to something that makes sense for the
 * target platform, ensuring that Soletta is properly initialized before
 * calling the provided @a startup function, where all of the application
 * specific initialization must take place. If they make sense, command line
 * arguments will have been set and can be retrieved with sol_argc() and
 * sol_argv().
 *
 * After @a startup is called, the main loop will start, and once that finishes,
 * the @a shutdown function, if provided, will be called to perform any
 * necessary termination procedures by the application, before Soletta itself
 * is shutdown and the program terminated.
 */

#ifdef SOL_PLATFORM_CONTIKI
#include "sol-mainloop-contiki.h"

#define SOL_MAIN_DEFAULT(STARTUP, SHUTDOWN) \
    PROCESS(soletta_app_process, "soletta app process"); \
    AUTOSTART_PROCESSES(&soletta_app_process); \
    PROCESS_THREAD(soletta_app_process, ev, data) \
    { \
        SOL_LOG_LEVEL_INIT(); \
        SOL_LOG_LEVELS_INIT(); \
        sol_mainloop_contiki_event_set(ev, data); \
        PROCESS_BEGIN(); \
        if (sol_init() < 0) \
            return EXIT_FAILURE; \
        STARTUP(); \
        sol_run(); \
        while (sol_mainloop_contiki_iter()) \
            PROCESS_WAIT_EVENT(); \
        SHUTDOWN(); \
        sol_shutdown(); \
        PROCESS_END(); \
    }
#else
#ifdef SOL_PLATFORM_RIOT
#define SOL_MAIN(CALLBACKS) \
    int main(void) { \
        SOL_LOG_LEVEL_INIT(); \
        SOL_LOG_LEVELS_INIT(); \
        return sol_mainloop_default_main(&(CALLBACKS), 0, NULL); \
    }
#elif defined SOL_PLATFORM_ZEPHYR
#include <zephyr.h>

#define SOL_MAIN(CALLBACKS) \
    void main_task(void) { \
        SOL_LOG_LEVEL_INIT(); \
        SOL_LOG_LEVELS_INIT(); \
        sol_mainloop_default_main(&(CALLBACKS), 0, NULL); \
    }
#else
#define SOL_MAIN(CALLBACKS) \
    int main(int argc, char *argv[]) { \
        SOL_LOG_LEVEL_INIT(); \
        SOL_LOG_LEVELS_INIT(); \
        return sol_mainloop_default_main(&(CALLBACKS), argc, argv); \
    }
#endif /* SOL_PLATFORM_RIOT */

#ifdef __cplusplus
#define SOL_MAIN_DEFAULT(STARTUP, SHUTDOWN) \
    static const struct sol_main_callbacks sol_main_callbacks_instance { \
        SOL_SET_API_VERSION(.api_version = SOL_MAIN_CALLBACKS_API_VERSION, ) \
        .flags = 0, \
        .startup = (STARTUP), \
        .shutdown = (SHUTDOWN) \
    }; \
    SOL_MAIN(sol_main_callbacks_instance)
#else
#define SOL_MAIN_DEFAULT(STARTUP, SHUTDOWN) \
    static const struct sol_main_callbacks sol_main_callbacks_instance = { \
        SOL_SET_API_VERSION(.api_version = SOL_MAIN_CALLBACKS_API_VERSION, ) \
        .startup = (STARTUP), \
        .shutdown = (SHUTDOWN) \
    }; \
    SOL_MAIN(sol_main_callbacks_instance)
#endif /* __cplusplus */
#endif /* SOL_PLATFORM_CONTIKI */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
