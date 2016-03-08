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

#include "device-id.h"

#include <nan.h>

using namespace v8;

Local<Value> js_DeviceIdFromSlice(const struct sol_str_slice *slice) {
    char returnValue[37] = "";
    if (slice->len != 16) {
        Nan::ThrowRangeError("Data for deviceId is not 16 bytes long");
        return Nan::Null();
    }

    // Canonical uuid format
    int result = snprintf(returnValue, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        (unsigned char)(slice->data[0]), (unsigned char)(slice->data[1]),
        (unsigned char)(slice->data[2]), (unsigned char)(slice->data[3]),
        (unsigned char)(slice->data[4]), (unsigned char)(slice->data[5]),
        (unsigned char)(slice->data[6]), (unsigned char)(slice->data[7]),
        (unsigned char)(slice->data[8]), (unsigned char)(slice->data[9]),
        (unsigned char)(slice->data[10]), (unsigned char)(slice->data[11]),
        (unsigned char)(slice->data[12]), (unsigned char)(slice->data[13]),
        (unsigned char)(slice->data[14]), (unsigned char)(slice->data[15]));

    if (result != 36) {
        Nan::ThrowError("Failed to convert deviceId to string");
        return Nan::Null();
    }

    return Nan::New(returnValue).ToLocalChecked();
}
