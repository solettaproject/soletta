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
