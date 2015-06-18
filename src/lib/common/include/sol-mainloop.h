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

int sol_argc(void);
char **sol_argv(void);
void sol_args_set(int argc, char *argv[]);

#ifdef SOL_PLATFORM_LINUX
#include "sol-mainloop-linux.h"
#endif

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
