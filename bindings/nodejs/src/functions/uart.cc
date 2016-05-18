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

#include <sol-uart.h>
#include <node.h>
#include <nan.h>

#include "../common.h"
#include "../hijack.h"
#include "../structures/sol-js-uart.h"
#include "../structures/js-handle.h"

using namespace v8;

class SolUART : public JSHandle<SolUART> {
public:
    static const char *jsClassName() { return "SolUART"; }
};

static void sol_uart_read_callback(void *user_data, struct sol_uart *uart,
    unsigned char byte_read) {
    Nan::HandleScope scope;
    sol_uart_data *uart_data = (sol_uart_data *)user_data;
    Nan::Callback *callback = uart_data->rx_cb;
    if (!callback)
        return;

    Local<Value> arguments[1] = {
        Nan::New(byte_read)
    };

    callback->Call(1, arguments);
}

NAN_METHOD(bind_sol_uart_open) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsString);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsObject);
    sol_uart_config config;
    sol_uart *uart = NULL;

    if (!hijack_ref())
        return;

    sol_uart_data *uart_data = new sol_uart_data;
    uart_data->rx_cb = NULL;
    uart_data->tx_cb = NULL;
    if (!c_sol_uart_config(info[1]->ToObject(), uart_data, &config)) {
        delete uart_data;
        Nan::ThrowError("Unable to extract sol_uart_config\n");
        return;
    }

    Nan::Callback *readCallback = uart_data->rx_cb;
    config.rx_cb = sol_uart_read_callback;

    uart = sol_uart_open((const char *)*String::Utf8Value(info[0]), &config);
    if (!uart) {
        if (readCallback)
            delete readCallback;
        delete uart_data;
        hijack_unref();
        return;
    }

    uart_data->uart = uart;
    info.GetReturnValue().Set(SolUART::New(uart_data));
}

NAN_METHOD(bind_sol_uart_close) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsObject);
    Local<Object> jsUART = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_uart_data *uart_data = (sol_uart_data *)SolUART::Resolve(jsUART);
    if (!uart_data)
        return;

    sol_uart *uart = uart_data->uart;
    Nan::Callback *callback = uart_data->rx_cb;
    sol_uart_close(uart);
    if (callback) {
        delete callback;
    }
    delete uart_data;
    Nan::SetInternalFieldPointer(jsUART, 0, 0);
    hijack_unref();
}

static void sol_uart_write_callback(void *data, struct sol_uart *uart,
    unsigned char *tx, int status) {
    Nan::HandleScope scope;
    Local<Value> buffer;
    sol_uart_data *uart_data = (sol_uart_data *)data;
    Nan::Callback *callback = uart_data->tx_cb;
    if (!callback)
        return;

    if (status >= 0) {
        Local <Object> bufObj;
        bufObj = Nan::NewBuffer((char *)tx, status).ToLocalChecked();
        buffer = bufObj;
    } else {
        free(tx);
        buffer = Nan::Null();
    }

    Local<Value> arguments[2] = {
        buffer,
        Nan::New(status)
    };
    callback->Call(2, arguments);

    delete callback;
}

NAN_METHOD(bind_sol_uart_write) {
    VALIDATE_ARGUMENT_COUNT(info, 3);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 2, IsFunction);

    Local<Object> jsUART = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_uart_data *uart_data = (sol_uart_data *)SolUART::Resolve(jsUART);
    if (!uart_data)
        return;

    sol_uart *uart = uart_data->uart;
    unsigned char *outputBuffer = (unsigned char *) 0;

    if (!node::Buffer::HasInstance(info[1])) {
        Nan::ThrowTypeError("Argument 1 must be a Buffer");
        return;
    }

    size_t length = node::Buffer::Length(info[1]);

    outputBuffer = (unsigned char *) malloc(length * sizeof(unsigned char));
    if (!outputBuffer) {
        Nan::ThrowError("Failed to allocate memory for output buffer");
        return;
    }

    memcpy(outputBuffer, node::Buffer::Data(info[1]), length);

    Nan::Callback *callback =
        new Nan::Callback(Local<Function>::Cast(info[2]));
    bool returnValue =
        sol_uart_write(uart, outputBuffer, length, sol_uart_write_callback,
            uart_data);

    if (!returnValue) {
        delete callback;
        free(outputBuffer);
    } else {
        uart_data->tx_cb = callback;
    }

    info.GetReturnValue().Set(Nan::New(returnValue));
}

NAN_METHOD(bind_sol_uart_baud_rate_from_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsString);

    sol_uart_baud_rate baud_rate = sol_uart_baud_rate_from_str(
        (const char *)*String::Utf8Value(info[0]));
    info.GetReturnValue().Set(Nan::New(baud_rate));
}

NAN_METHOD(bind_sol_uart_baud_rate_to_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsInt32);

    const char *idString = sol_uart_baud_rate_to_str(
        (sol_uart_baud_rate)info[0]->Int32Value());

    if (idString) {
        info.GetReturnValue().Set(Nan::New(idString).ToLocalChecked());
    } else {
        info.GetReturnValue().Set(Nan::Null());
    }
}

NAN_METHOD(bind_sol_uart_data_bits_from_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsString);

    sol_uart_data_bits data_bits = sol_uart_data_bits_from_str(
        (const char *)*String::Utf8Value(info[0]));
    info.GetReturnValue().Set(Nan::New(data_bits));
}

NAN_METHOD(bind_sol_uart_data_bits_to_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsInt32);

    const char *idString = sol_uart_data_bits_to_str(
        (sol_uart_data_bits)info[0]->Int32Value());

    if (idString) {
        info.GetReturnValue().Set(Nan::New(idString).ToLocalChecked());
    } else {
        info.GetReturnValue().Set(Nan::Null());
    }
}

NAN_METHOD(bind_sol_uart_stop_bits_from_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsString);

    sol_uart_stop_bits stop_bits = sol_uart_stop_bits_from_str(
        (const char *)*String::Utf8Value(info[0]));
    info.GetReturnValue().Set(Nan::New(stop_bits));
}

NAN_METHOD(bind_sol_uart_stop_bits_to_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsInt32);

    const char *idString = sol_uart_stop_bits_to_str(
        (sol_uart_stop_bits)info[0]->Int32Value());

    if (idString) {
        info.GetReturnValue().Set(Nan::New(idString).ToLocalChecked());
    } else {
        info.GetReturnValue().Set(Nan::Null());
    }
}

NAN_METHOD(bind_sol_uart_parity_from_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsString);

    sol_uart_parity parity = sol_uart_parity_from_str(
        (const char *)*String::Utf8Value(info[0]));
    info.GetReturnValue().Set(Nan::New(parity));
}

NAN_METHOD(bind_sol_uart_parity_to_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsInt32);

    const char *idString = sol_uart_parity_to_str(
        (sol_uart_parity)info[0]->Int32Value());

    if (idString) {
        info.GetReturnValue().Set(Nan::New(idString).ToLocalChecked());
    } else {
        info.GetReturnValue().Set(Nan::Null());
    }
}
