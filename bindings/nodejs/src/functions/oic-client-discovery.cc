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

#include <string.h>
#include <nan.h>
#include <sol-oic-client.h>

#include "../common.h"
#include "../structures/network.h"
#include "../structures/oic-client.h"

using namespace v8;

static bool resourceFound(struct sol_oic_client *client,
    struct sol_oic_resource *resource, void *data) {
    Nan::HandleScope scope;

    // If Soletta tells us there are no more resources, we detach this callback
    // no matter what the JS callback returns
    bool keepDiscovering = !!resource;
    OicCallbackData *callbackData = (OicCallbackData *)data;

    // Call the JS callback
    Local<Value> arguments[2] = {
        Nan::New(*(callbackData->jsClient)),
        Nan::Null()
    };
    if (resource) {
        arguments[1] = SolOicClientResource::New(resource);
    }
    Local<Value> jsResult = callbackData->callback->Call(2, arguments);

    // Determine whether we should keep discovering
    if (!jsResult->IsBoolean()) {
        Nan::ThrowTypeError(
            "Resource discovery callback return value is not boolean");
    } else {
        keepDiscovering = keepDiscovering &&
            Nan::To<bool>(jsResult).FromJust();
    }

    // Tear down if discovery is done
    if (!keepDiscovering) {
        delete callbackData;
    }

    return keepDiscovering;
}

NAN_METHOD(bind_sol_oic_client_find_resource) {
    VALIDATE_ARGUMENT_COUNT(info, 4);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 2, IsString);
    VALIDATE_ARGUMENT_TYPE(info, 3, IsFunction);

    struct sol_network_link_addr theAddress;
    if (!c_sol_network_link_addr(Nan::To<Object>(info[1]).ToLocalChecked(),
        &theAddress)) {
        return;
    }

    Local<Object> jsClient = Nan::To<Object>(info[0]).ToLocalChecked();
    struct sol_oic_client *client = (struct sol_oic_client *)
        SolOicClient::Resolve(jsClient);
    if (!client) {
        return;
    }

    OicCallbackData *callbackData =
        OicCallbackData::New(jsClient, Local<Function>::Cast(info[3]));
    if (!callbackData) {
        return;
    }

    bool result = sol_oic_client_find_resource((struct sol_oic_client *)client,
        &theAddress, (const char *)*String::Utf8Value(info[2]), resourceFound,
        callbackData);

    if (!result) {
        delete callbackData;
    }

    info.GetReturnValue().Set(Nan::New(result));
}
