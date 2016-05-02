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

#include <string.h>
#include <nan.h>
#include <sol-oic-client.h>

#include "../common.h"
#include "../structures/network.h"
#include "../structures/oic-client.h"

using namespace v8;

static bool resourceFound(void *data, struct sol_oic_client *client,
    struct sol_oic_resource *resource) {
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
    VALIDATE_ARGUMENT_TYPE(info, 3, IsString);
    VALIDATE_ARGUMENT_TYPE(info, 4, IsFunction);

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
        OicCallbackData::New(jsClient, Local<Function>::Cast(info[4]));
    if (!callbackData) {
        return;
    }

    bool result = (sol_oic_client_find_resource((struct sol_oic_client *)client,
        &theAddress, (const char *)*String::Utf8Value(info[2]),
        (const char *)*String::Utf8Value(info[3]), resourceFound,
        callbackData) == 0);

    if (!result) {
        delete callbackData;
    }

    info.GetReturnValue().Set(Nan::New(result));
}
