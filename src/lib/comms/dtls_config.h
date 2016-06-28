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

#pragma once

#include "sol-common-buildopts.h"

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
#define HAVE_VPRINTF 1
