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

#pragma once

#include "sol-common-buildopts.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @mainpage Soletta Project Documentation
 *
 * @version 1
 * @date 2015
 *
 * Soletta project is a framework for making IoT devices. With Soletta
 * project's libraries developers can easily write software for
 * devices that control actuators/sensors and communicate using
 * standard technologies. It enables adding smartness even on the
 * smallest edge devices.
 *
 * Portable and scalable, it abstracts details of hardware and OS,
 * enabling developers to reuse their code and knowledge on different
 * targets.
 *
 * For a better reference, check the following groups:
 * @li @ref Comms
 * @li @ref Datatypes
 * @li @ref Flow
 * @li @ref IO
 * @li @ref Log
 * @li @ref Macros
 * @li @ref Mainloop
 * @li @ref Missing
 * @li @ref Parsers
 * @li @ref Platform
 * @li @ref Types
 *
 * Please see the @ref authors page for contact details.
 */

/**
 * @page authors Authors
 *
 * @author Anselmo L. S. Melo <anselmo.melo@intel.com>
 * @author Bruno Bottazzini <bruno.bottazzini@intel.com>
 * @author Bruno Dilly <bruno.dilly@intel.com>
 * @author Caio Marcelo de Oliveira Filho <caio.oliveira@intel.com>
 * @author Ederson de Souza <ederson.desouza@intel.com>
 * @author Flavio Ceolin <flavio.ceolin@intel.com>
 * @author Gustavo Lima Chaves <gustavo.lima.chaves@intel.com>
 * @author Gustavo Sverzut Barbieri <gustavo.barbieri@intel.com>
 * @author Iván Briano <ivan.briano@intel.com>
 * @author José Roberto de Souza <jose.souza@intel.com>
 * @author Leandro Dorileo <leandro.maciel.dorileo@intel.com>
 * @author Leandro Pereira <leandro.pereira@intel.com>
 * @author Lucas De Marchi <lucas.demarchi@intel.com>
 * @author Luis Felipe Strano Moraes <luis.strano@intel.com>
 * @author Luiz Ywata <luizg.ywata@intel.com>
 * @author Murilo Belluzzo <murilo.belluzzo@intel.com>
 * @author Ricardo de Almeida Gonzaga <ricardo.gonzaga@intel.com>
 * @author Rodrigo Chiossi <rodrigo.chiossi@intel.com>
 * @author Tomaz Canabrava <tomaz.canabrava@intel.com>
 * @author Ulisses Furquim <ulisses.furquim@intel.com>
 * @author Vinicius Costa Gomes <vinicius.gomes@intel.com>
 *
 * Please contact <soletta-dev@ml01.01.org> to get in contact with the
 * developers and maintainers.
 */

/**
 * @file
 * @brief These routines are used for Solleta's mainloop manipulation.
 */

/**
 * @defgroup Mainloop Mainloop
 *
 * @{
 */

int sol_init(void);
int sol_run(void);
void sol_quit(void);
void sol_quit_with_code(int return_code);
void sol_shutdown(void);

struct sol_timeout;
struct sol_timeout *sol_timeout_add(unsigned int timeout_ms, bool (*cb)(void *data), const void *data);
bool sol_timeout_del(struct sol_timeout *handle);

struct sol_idle;
struct sol_idle *sol_idle_add(bool (*cb)(void *data), const void *data);
bool sol_idle_del(struct sol_idle *handle);

#ifdef SOL_MAINLOOP_FD_ENABLED
enum sol_fd_flags {
    SOL_FD_FLAGS_NONE = 0,
    SOL_FD_FLAGS_IN   = (1 << 0),
    SOL_FD_FLAGS_OUT  = (1 << 1),
    SOL_FD_FLAGS_PRI  = (1 << 2),
    SOL_FD_FLAGS_ERR  = (1 << 3),
    SOL_FD_FLAGS_HUP  = (1 << 4),
    SOL_FD_FLAGS_NVAL = (1 << 5)
};

struct sol_fd;
struct sol_fd *sol_fd_add(int fd, unsigned int flags, bool (*cb)(void *data, int fd, unsigned int active_flags), const void *data);
bool sol_fd_del(struct sol_fd *handle);
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
struct sol_child_watch;
struct sol_child_watch *sol_child_watch_add(uint64_t pid, void (*cb)(void *data, uint64_t pid, int status), const void *data);
bool sol_child_watch_del(struct sol_child_watch *handle);
#endif

struct sol_mainloop_source_type {
#define SOL_MAINLOOP_SOURCE_TYPE_API_VERSION (1)  /**< compile time API version to be checked during runtime */
    /**
     * must match #SOL_MAINLOOP_SOURCE_TYPE_API_VERSION at runtime.
     */
    uint16_t api_version;

    /**
     * Function to be called to prepare to check for events.
     *
     * This function will be called before Soletta's main loop query
     * for its own events. In Linux/POSIX, it will be called before
     * poll(). A source may convert its internal monitored resources
     * to Soletta's at this moment, such as issuing sol_timeout_add()
     * or sol_fd_add().
     *
     * If returns @c true, then there are events to be dispatched
     * right away. This will have @c dispatch() to be called, even if
     * @c check() returns false. This will not let the main loop to
     * sleep or block while it wait for events, as the source's events
     * must be dispatched as soon as possible.
     *
     * A source can implement the traditional main loop primitives:
     * @li @c idler: return @c true.
     * @li @c timeout: return @c false and implement @c get_next_timeout().
     * @li @c fd: return @c false.
     *
     * May be NULL, in this case return @c false is assumed.
     */
    bool (*prepare)(void *data);

    /**
     * Function to be called to query the next timeout for the next
     * event in this source.
     *
     * If returns @c true, then @c timeout must be set to the next
     * timeout to expire in this source. Soletta's main loop will
     * gather all timeouts from external sources and internal
     * (registered with sol_timeout_add()) and will schedule a timer
     * interruption to the one that happens sooner, sleeping until
     * then if there are no known events such as in @c prepare(), or
     * idlers registered with sol_idle_add().
     *
     * If returns false, then may sleep forever as no events are
     * expected.
     *
     * A source can implement the traditional main loop primitives:
     * @li @c idler: return @c false.
     * @li @c timeout: return @c true and set @c timeout.
     * @li @c fd: return @c false.
     *
     * May be NULL, in this case return @c false is assumed.
     */
    bool (*get_next_timeout)(void *data, struct timespec *timeout);

    /**
     * Function to be called to check if there are events to be dispatched.
     *
     * If returns @c true, then there are events to be dispatched and
     * @c dispatch() should be called.
     *
     * A source can implement the traditional main loop primitives:
     * @li @c idler: return @c true.
     * @li @c timeout: return @c true if timeouts expired.
     * @li @c fd: return @c true if fds are ready.
     *
     * Must @b not be NULL.
     */
    bool (*check)(void *data);

    /**
     * Function to be called during main loop iterations if @c
     * prepare() or @c check() returns @c true.
     *
     * Must @b not be NULL.
     */
    void (*dispatch)(void *data);

    /**
     * Function to be called when the source is deleted.
     *
     * It is called when the source is explicitly deleted using
     * sol_mainloop_source_del() or when sol_shutdown() is called.
     *
     * May be NULL.
     */
    void (*dispose)(void *data);
};

struct sol_mainloop_source;

/**
 * Create a new source of events to the main loop.
 *
 * Some libraries will have their own internal main loop, in the case
 * we should integrate them with Soletta's we do so by adding a new
 * source of events to Soletta's main loop.
 *
 * The source is described by its @a type, a set of functions to be
 * called back at various phases:
 *
 * @li @b prepare called before all other callbacks at each main loop
 *     iteration.
 *
 * @li @b get_next_timeout called before configuring the maximum
 *     timeout to wait for events. The smallest value of all sources
 *     and the first timeout of Soletta's will be used to determine
 *     the value to use (ie: the one to give to poll(2) if using posix
 *     main loop). If returns @c false the main loop can sleep
 *     forever. If not provided, then return @c false is assumed.
 *
 * @li @b check called after all event sources and Soletta's internal
 *     events are polled and are ready to be dispatched. If it returns
 *     @c true, then there are events to be dispatched in this
 *     source.
 *
 * @li @b dispatch called if @c check() returns @c true.
 *
 * @li @c dispose called when the source is deleted. This will
 *     happen at sol_shutdown() if the source is not manually deleted.
 *
 * If a source doesn't know the time for the next event (say an
 * interruption service handler or an internal file descriptor), then
 * further integration work needs to be done. The interruption service
 * handler can use sol_timeout_add() from a thread to schedule a main
 * loop wake, that in turn will run the source's prepare()
 * automatically. Analogously, the internal file descriptors can be
 * added to soletta's at prepare(). If the main loop uses epoll(),
 * that fd can be added to chain the monitoring.
 *
 * @param type the description of the source of main loop events. This
 *        pointer is not modified and is @b not copied, thus it @b
 *        must exist during the lifetime of the source.
 * @param data the user data (context) to give to callbacks in @a type.
 *
 * @return the new main loop source instance or @c NULL on failure.
 *
 * @see sol_mainloop_source_del()
 */
struct sol_mainloop_source *sol_mainloop_source_new(const struct sol_mainloop_source_type *type, const void *data);

/**
 * Destroy a source of main loop events.
 *
 * @param handle a valid handle previously created with
 *        sol_mainloop_source_new().
 *
 * @see sol_mainloop_source_new()
 */
void sol_mainloop_source_del(struct sol_mainloop_source *handle);

/**
 * Retrieve the user data (context) given to the source at creation time.
 *
 * @param handle a valid handle previously created with
 *        sol_mainloop_source_new().
 *
 * @return whatever was given to sol_mainloop_source_new() as second
 *         parameter. NULL is a valid return.
 *
 * @see sol_mainloop_source_new()
 */
void *sol_mainloop_source_get_data(const struct sol_mainloop_source *handle);

int sol_argc(void);
char **sol_argv(void);
void sol_args_set(int argc, char *argv[]);

struct sol_main_callbacks {
#define SOL_MAIN_CALLBACKS_API_VERSION (1)
    uint16_t api_version;
    uint16_t flags;
    void (*startup)(void);
    void (*shutdown)(void);
};

#ifdef SOL_PLATFORM_CONTIKI
#include "sol-mainloop-contiki.h"

#define SOL_MAIN_DEFAULT(STARTUP, SHUTDOWN)               \
    PROCESS(soletta_app_process, "soletta app process");  \
    AUTOSTART_PROCESSES(&soletta_app_process);            \
    PROCESS_THREAD(soletta_app_process, ev, data)         \
    {                                                     \
        sol_mainloop_contiki_event_set(ev, data);         \
        PROCESS_BEGIN();                                  \
        if (sol_init() < 0)                               \
            return EXIT_FAILURE;                          \
        STARTUP();                                        \
        sol_run();                                        \
        while (sol_mainloop_contiki_iter())               \
            PROCESS_WAIT_EVENT();                         \
        SHUTDOWN();                                       \
        sol_shutdown();                                   \
        PROCESS_END();                                    \
    }
#else
#define SOL_MAIN(CALLBACKS)                                          \
    int main(int argc, char *argv[]) {                              \
        return sol_mainloop_default_main(&(CALLBACKS), argc, argv);  \
    }

#define SOL_MAIN_DEFAULT(STARTUP, SHUTDOWN)                              \
    static const struct sol_main_callbacks sol_main_callbacks_instance = { \
        .api_version = SOL_MAIN_CALLBACKS_API_VERSION,                   \
        .startup = (STARTUP),                                           \
        .shutdown = (SHUTDOWN),                                         \
    };                                                                  \
    SOL_MAIN(sol_main_callbacks_instance)
#endif

/* Internal. */
int sol_mainloop_default_main(const struct sol_main_callbacks *callbacks, int argc, char *argv[]);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
