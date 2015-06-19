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

#include <stdbool.h>

#include <contiki.h>

#ifdef __cplusplus
extern "C" {
#endif

void sol_mainloop_contiki_event_set(process_event_t ev, process_data_t data);
bool sol_mainloop_contiki_loop(void);

#define SOL_MAIN_PROCESS(name, strname, setup_func, teardown_func) \
    PROCESS(name, strname);                                        \
    AUTOSTART_PROCESSES(&name);                                    \
    PROCESS_THREAD(name, ev, data)                                 \
    {                                                              \
        sol_mainloop_contiki_event_set(ev, data);                  \
        PROCESS_BEGIN();                                           \
        if (sol_init() < 0)                                        \
            return EXIT_FAILURE;                                   \
        setup_func();                                              \
        sol_run();                                                 \
        while (sol_mainloop_contiki_loop())                        \
            PROCESS_WAIT_EVENT();                                  \
        teardown_func();                                           \
        sol_shutdown();                                            \
        PROCESS_END();                                             \
    }

#ifdef __cplusplus
}
#endif
