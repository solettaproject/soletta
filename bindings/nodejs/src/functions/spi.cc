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

#include <node.h>
#include <nan.h>
#include <sol-spi.h>

#include "../common.h"
#include "../data.h"
#include "../hijack.h"
#include "../structures/js-handle.h"
#include "../structures/sol-js-spi.h"

using namespace v8;

class SolSpi : public JSHandle<SolSpi> {
public:
    static const char *jsClassName() { return "SolSpi"; }
};

NAN_METHOD(bind_sol_spi_open) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsUint32);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsObject);

    sol_spi_config config;
    sol_spi *spi = NULL;

    if (!c_sol_spi_config(info[1]->ToObject(), &config)) {
        return;
    }

    spi = sol_spi_open(info[0]->Uint32Value(), &config);
    if (spi) {
        info.GetReturnValue().Set(SolSpi::New(spi));
    }
}

NAN_METHOD(bind_sol_spi_close) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsObject);
    Local<Object> jsSpi = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_spi *spi= (sol_spi *)SolSpi::Resolve(jsSpi);
    if (!spi)
        return;

    sol_spi_close(spi);
    Nan::SetInternalFieldPointer(jsSpi, 0, 0);
}

static void sol_spi_transfer_cb(void *cb_data, struct sol_spi *spi,
    const uint8_t *tx, uint8_t *rx, ssize_t status) {
    Nan::HandleScope scope;
    Nan::Callback *callback = (Nan::Callback *)cb_data;
    Local<Value> txBuffer, rxBuffer;

    if (status >= 0) {
        txBuffer = Nan::NewBuffer((char *)tx, status).ToLocalChecked();
        rxBuffer = Nan::NewBuffer((char *)rx, status).ToLocalChecked();
    } else {
        free((void *)tx);
        free(rx);
        txBuffer = Nan::Null();
        rxBuffer = Nan::Null();
    }

    Local<Value> arguments[3] = {
        txBuffer,
        rxBuffer,
        Nan::New((int)status)
    };

    callback->Call(3, arguments);
    delete callback;
    hijack_unref();
}

NAN_METHOD(bind_sol_spi_transfer) {
    VALIDATE_ARGUMENT_COUNT(info, 3);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsObject);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 2, IsFunction);
    Local<Object> jsSpi = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_spi *spi= (sol_spi *)SolSpi::Resolve(jsSpi);
    if (!spi)
        return;

    if (!node::Buffer::HasInstance(info[1])) {
        Nan::ThrowTypeError("Argument 1 must be a node Buffer");
        return;
    }

    size_t length = node::Buffer::Length(info[1]);
    uint8_t *txBuffer = (uint8_t *) calloc(length, sizeof(uint8_t));
    if (!txBuffer) {
        Nan::ThrowError("Failed to allocate memory for output buffer");
        return;
    }

    uint8_t *rxBuffer = (uint8_t *) calloc(length, sizeof(uint8_t));
    if (!rxBuffer) {
        free(txBuffer);
        Nan::ThrowError("Failed to allocate memory for input buffer");
        return;
    }

    if (!hijack_ref()) {
        free(txBuffer);
        free(rxBuffer);
        return;
    }

    memcpy(txBuffer, node::Buffer::Data(info[1]), length);
    Nan::Callback *callback =
        new Nan::Callback(Local<Function>::Cast(info[2]));
    bool returnValue =
        sol_spi_transfer(spi, txBuffer, rxBuffer, length,
            sol_spi_transfer_cb, callback);

    if (!returnValue) {
        free(txBuffer);
        free(rxBuffer);
        delete callback;
        hijack_unref();
    }

    info.GetReturnValue().Set(Nan::New(returnValue));
}

NAN_METHOD(bind_sol_spi_mode_from_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsString);

    sol_spi_mode mode = sol_spi_mode_from_str(
        (const char *)*String::Utf8Value(info[0]));
    info.GetReturnValue().Set(Nan::New(mode));
}

NAN_METHOD(bind_sol_spi_mode_to_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsInt32);

    const char *idString = sol_spi_mode_to_str(
        (sol_spi_mode)info[0]->Int32Value());

    if (idString) {
        info.GetReturnValue().Set(Nan::New(idString).ToLocalChecked());
    } else {
        info.GetReturnValue().Set(Nan::Null());
    }
}
