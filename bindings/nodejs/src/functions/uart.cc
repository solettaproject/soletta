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

#include <sol-buffer.h>
#include <sol-str-slice.h>
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

static ssize_t sol_uart_on_data_callback(void *user_data,
    struct sol_uart *uart, const struct sol_buffer *buf) {
    Nan::HandleScope scope;
    sol_uart_data *uart_data = (sol_uart_data *)user_data;
    if (!uart_data)
        return 0;

    Nan::Callback *callback = uart_data->on_data_cb;
    if (!callback)
        return 0;

    struct sol_str_slice slice;
    slice = sol_buffer_get_slice(buf);
    Local<Value> buffer =
        Nan::CopyBuffer(slice.data, slice.len).ToLocalChecked();

    Local<Value> arguments[1] = {
        buffer
    };
    callback->Call(1, arguments);

    return slice.len;
}

static void sol_uart_on_feed_done_callback(void *user_data,
    struct sol_uart *uart, struct sol_blob *blob, int status) {
    Nan::HandleScope scope;
    sol_uart_data *uart_data = (sol_uart_data *)user_data;
    if (!uart_data)
        return;

    Local<Value> arguments[1] = {
        Nan::New(status)
    };

    Nan::Callback *on_feed_done_cb = uart_data->on_feed_done_cb;
    if (on_feed_done_cb)
        on_feed_done_cb->Call(1, arguments);

   // Retrieve the JS feed callback and call it
   auto iter = uart_data->feed_callbacks_map.find(blob);
   if (iter != uart_data->feed_callbacks_map.end()) {
       CallbackInfo *info = iter->second;
       Nan::Callback *callback = info->callback;
       callback->Call(1, arguments);
       uart_data->feed_callbacks_map.erase(blob);
       delete callback;

       Nan::Persistent<Value> *js_buffer = info->js_buffer;
       js_buffer->Reset();
       delete js_buffer;
       delete info;
   }
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
    uart_data->on_data_cb = NULL;
    uart_data->on_feed_done_cb = NULL;
    if (!c_sol_uart_config(info[1]->ToObject(), uart_data, &config)) {
        delete uart_data;
        Nan::ThrowError("Unable to extract sol_uart_config\n");
        return;
    }

    Nan::Callback *on_data_cb = uart_data->on_data_cb;
    Nan::Callback *on_feed_done_cb = uart_data->on_feed_done_cb;
    config.on_data = sol_uart_on_data_callback;
    config.on_feed_done = sol_uart_on_feed_done_callback;

    uart = sol_uart_open((const char *)*String::Utf8Value(info[0]), &config);
    if (!uart) {
        if (on_data_cb)
            delete on_data_cb;
        if (on_feed_done_cb)
            delete on_feed_done_cb;
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
    Nan::Callback *on_data_cb = uart_data->on_data_cb;
    Nan::Callback *on_feed_done_cb = uart_data->on_feed_done_cb;
    sol_uart_close(uart);

    if (on_data_cb)
        delete on_data_cb;

    if (on_feed_done_cb)
        delete on_feed_done_cb;

    hijack_unref();
    delete uart_data;
    Nan::SetInternalFieldPointer(jsUART, 0, 0);
}

NAN_METHOD(bind_sol_uart_feed) {
    VALIDATE_ARGUMENT_COUNT(info, 3);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 2, IsFunction);

    Local<Object> jsUART = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_uart_data *uart_data = (sol_uart_data *)SolUART::Resolve(jsUART);
    if (!uart_data)
        return;

    sol_uart *uart = uart_data->uart;
    if (!node::Buffer::HasInstance(info[1])) {
        Nan::ThrowTypeError("Argument 1 must be a Buffer");
        return;
    }

    size_t length = node::Buffer::Length(info[1]);
    Nan::Persistent<Value> *js_buffer = new Nan::Persistent<Value>(info[1]);

    struct sol_blob *blob;
    blob = sol_blob_new(&SOL_BLOB_TYPE_NO_FREE_DATA, NULL,
        node::Buffer::Data(info[1]), length);
    if (!blob) {
        js_buffer->Reset();
        delete js_buffer;
        Nan::ThrowError("Failed to allocate memory for blob");
        return;
    }

    CallbackInfo *callback_info = new CallbackInfo;
    callback_info->callback = new Nan::Callback(Local<Function>::Cast(info[2]));
    callback_info->js_buffer = js_buffer;

    // Map JS feed callback info to a blob
    uart_data->feed_callbacks_map[blob] = callback_info;
    int returnValue = sol_uart_feed(uart, blob);
    if (returnValue < 0) {
        uart_data->feed_callbacks_map.erase(blob);
        js_buffer->Reset();
        delete js_buffer;

        delete callback_info->callback;
        delete callback_info;
    }

    sol_blob_unref(blob);
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
