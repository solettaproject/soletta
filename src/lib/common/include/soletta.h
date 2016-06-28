/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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
    void main(void) { \
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
