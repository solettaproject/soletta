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

#include <nan.h>
#include "../common.h"
#include "../data.h"
#include "network.h"

using namespace v8;

bool c_sol_network_link_addr(Local<Object> jsAddress,
    struct sol_network_link_addr *destination) {
    struct sol_network_link_addr local;

    Local<Value> jsBytesValue =
        Nan::Get(jsAddress, Nan::New("bytes").ToLocalChecked())
            .ToLocalChecked();
    VALIDATE_VALUE_TYPE(jsBytesValue, IsArray, "Network address bytes array",
        false);
    Local<Array> jsBytes = Local<Array>::Cast(jsBytesValue);
    if (!fillCArrayFromJSArray(local.addr.in6, 16 * sizeof(char), jsBytes)) {
        return false;
    }

    VALIDATE_AND_ASSIGN(local, family, enum sol_network_family, IsUint32,
        "Network address family", false, jsAddress, Uint32Value);
    VALIDATE_AND_ASSIGN(local, port, uint16_t, IsUint32,
        "Network address port", false, jsAddress, Uint32Value);

    *destination = local;
    return true;
}

Local<Value> js_sol_network_link_addr(
    const struct sol_network_link_addr *c_address) {

    if (!c_address) {
        return Nan::Null();
    }

    Local<Object> returnValue = Nan::New<Object>();

    Local<Array> bytes =
        jsArrayFromBytes((unsigned char *)(c_address->addr.in6),
            16 * sizeof(char));
    Nan::Set(returnValue, Nan::New("bytes").ToLocalChecked(), bytes);

    SET_VALUE_ON_OBJECT(returnValue, Uint32, c_address, family);
    SET_VALUE_ON_OBJECT(returnValue, Uint32, c_address, port);

    return returnValue;
}
