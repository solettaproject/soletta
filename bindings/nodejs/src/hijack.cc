/*
 * This file is part of the Soletta™ Project
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

    SOL_DBG("Entering");

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
