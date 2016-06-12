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

extern "C" {
#include <sol-aio.h>
}

#include <v8.h>
#include <node.h>
#include <nan.h>

#include "../common.h"
#include "../hijack.h"
#include "../structures/js-handle.h"

using namespace v8;

class SolAio : public JSHandle<SolAio> {
public:
    static const char *jsClassName() { return "SolAio"; }
};

NAN_METHOD(bind_sol_aio_open) {
    VALIDATE_ARGUMENT_COUNT(info, 3);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsInt32);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsInt32);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 2, IsUint32);

    int32_t device;
    int32_t pin;
    uint32_t precision;
    sol_aio *aio = NULL;

    device = info[0]->Int32Value();
    pin = info[1]->Int32Value();
    precision = info[2]->Uint32Value();

    aio = sol_aio_open(device, pin, precision);
    if ( aio ) {
        info.GetReturnValue().Set(SolAio::New(aio));
    }
}

NAN_METHOD(bind_sol_aio_open_by_label) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsString);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsUint32);

    uint32_t precision;
    sol_aio *aio = NULL;

    precision = info[1]->Uint32Value();

    aio = sol_aio_open_by_label((const char *)*String::Utf8Value(info[0]), precision);
    if ( aio ) {
        info.GetReturnValue().Set(SolAio::New(aio));
    }
}

NAN_METHOD(bind_sol_aio_open_raw) {
    VALIDATE_ARGUMENT_COUNT(info, 3);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsInt32);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsInt32);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 2, IsUint32);

    int32_t device;
    int32_t pin;
    uint32_t precision;
    sol_aio *aio = NULL;

    device = info[0]->Int32Value();
    pin = info[1]->Int32Value();
    precision = info[2]->Uint32Value();

    aio = sol_aio_open_raw(device, pin, precision);
    if ( aio ) {
        info.GetReturnValue().Set(SolAio::New(aio));
    }
}

NAN_METHOD(bind_sol_aio_close) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    Local<Object> jsAio = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_aio *aio = (sol_aio *)SolAio::Resolve(jsAio);

    sol_aio_close(aio);
    Nan::SetInternalFieldPointer(jsAio, 0, 0);
}

static void sol_aio_read_cb(void *cb_data, struct sol_aio *aio,
                            int32_t ret)
{
    Nan::HandleScope scope;
    Nan::Callback *callback = (Nan::Callback *)cb_data;

    Local<Value> arguments[1] = {
        Nan::New(ret)
    };
    callback->Call(1, arguments);

    delete callback;
    hijack_unref();
}

NAN_METHOD(bind_sol_aio_get_value) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsFunction);
    Local<Object> jsAio = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_aio *aio = (sol_aio *)SolAio::Resolve(jsAio);

    Nan::Callback *callback =
        new Nan::Callback(Local<Function>::Cast(info[1]));

    sol_aio_pending *aio_pending =
        sol_aio_get_value(aio, sol_aio_read_cb, callback);

    if (!aio_pending) {
        delete callback;
        return;
    } else if (!hijack_ref()) {
        sol_aio_pending_cancel(aio, aio_pending);        
        delete callback;
        return;
    }

    info.GetReturnValue().Set(SolAio::New(aio_pending));
}

NAN_METHOD(bind_sol_aio_pending_cancel)
{
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsObject);
    Local<Object> jsAio = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_aio *aio = (sol_aio *)SolAio::Resolve(jsAio);

    Local<Object> jsAioPending = Nan::To<Object>(info[1]).ToLocalChecked();
    sol_aio_pending *aio_pending = (sol_aio_pending *)SolAio::Resolve(jsAioPending);

    sol_aio_pending_cancel(aio, aio_pending);
}
