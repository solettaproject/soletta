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

#include <string>
#include <nan.h>
#include <sol-log.h>
#include "sol-uv-integration.h"
#include "hijack.h"

using namespace v8;

static uint16_t hijack_refcount = 0;

bool hijack_ref() {
    int result = 0;

    SOL_DBG("Entering");

    if (hijack_refcount == UINT16_MAX) {
        result = -ERANGE;
        goto error;
    }

    if (hijack_refcount == 0) {
        SOL_DBG("hijacking main loop");
        result = hijack_main_loop();
        if (result) {
            goto error;
        }
    }

    hijack_refcount++;
    return true;
error:
    Nan::ThrowError((std::string("Hijack main loop: ") +
        strerror(-result)).c_str());
    return false;
}

bool hijack_unref() {
    int result = 0;

    SOL_DBG("Entering\n");

    if (hijack_refcount == 0) {
        result = -ERANGE;
        goto error;
    }

    if (hijack_refcount == 1) {
        SOL_DBG("releasing main loop");
        result = release_main_loop();
        if (result) {
            goto error;
        }
    }

    hijack_refcount--;
    return true;
error:
    Nan::ThrowError((std::string("Release main loop: ") +
        strerror(-result)).c_str());
    return false;
}
