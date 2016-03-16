/*
 * This file is part of the Soletta Project
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
