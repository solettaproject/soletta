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
