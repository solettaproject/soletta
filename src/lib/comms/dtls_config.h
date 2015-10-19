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
#include "sol_config.h"

/* This file is required to build TinyDLS, and uses the constants defined
 * by Soletta's build system to create one that TinyDTLS will be happy
 * with, without actually running or generating its configure script. */

#define NDEBUG

#define DTLSv12 1

#define DTLS_ECC 1
#define DTLS_PSK 1

#define SHA2_USE_INTTYPES_H 1
#define WITH_SHA256 1

#ifdef SOL_PLATFORM_CONTIKI
/* Enabling WITH_CONTIKI will generate Contiki-only code paths, including
 * generating code that does not depend on pthreads.  */
#define WITH_CONTIKI 1
#elif defined(PTHREAD) && PTHREAD == 1
/* Pthread implementation exists, so use it.  */
#else
/* For TinyDTLS, if platform is not Contiki, it requires pthread because of
 * a mutex in crypto.c.  Provide stub versions of pthread_mutex_t in this
 * case, as Soletta is single-threaded.
 *
 * This is not optimal, though. Some cleanup must be performed in TinyDTLS
 * in order to make it not assume Contiki when working with small embedded
 * systems.  */
typedef char pthread_mutex_t;

static inline void
pthread_mutex_lock(pthread_mutex_t *mtx)
{
    (void)mtx;
}

static inline void
pthread_mutex_unlock(pthread_mutex_t *mtx)
{
    (void)mtx;
}
#endif

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define WORDS_BIGENDIAN 1
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#undef WORDS_BIGENDIAN
#else
#error "Unknown byte order"
#endif

#define HAVE_ASSERT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_TIME_H 1
