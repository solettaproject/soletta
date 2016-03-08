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

#include "../structures/oic-client.h"
#include "../common.h"

using namespace v8;

NAN_METHOD(bind_sol_oic_client_new) {
    VALIDATE_ARGUMENT_COUNT(info, 0);
    struct sol_oic_client *client = sol_oic_client_new();
    if (client) {
        info.GetReturnValue().Set(SolOicClient::New(client));
    } else {
        info.GetReturnValue().Set(Nan::Null());
    }
}

NAN_METHOD(bind_sol_oic_client_del) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    Local<Object> jsClient = Nan::To<Object>(info[0]).ToLocalChecked();
    struct sol_oic_client *client = (struct sol_oic_client *)
        SolOicClient::Resolve(jsClient);
    if (client) {
        sol_oic_client_del(client);
        Nan::SetInternalFieldPointer(jsClient, 0, 0);
    }
}
