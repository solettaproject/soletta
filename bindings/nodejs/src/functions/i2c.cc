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

#include <errno.h>
#include <v8.h>
#include <node.h>
#include <nan.h>

#include <sol-i2c.h>

#include "../common.h"
#include "../data.h"
#include "../hijack.h"
#include "../structures/js-handle.h"
#include "../sys-constants.h"

using namespace v8;

class SolI2c : public JSHandle<SolI2c> {
public:
    static const char *jsClassName() { return "SolI2c"; }
};

NAN_METHOD(bind_sol_i2c_open)
{
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsUint32);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsUint32);

    sol_i2c *i2c = NULL;
    sol_i2c_speed speed = (sol_i2c_speed) info[1]->Uint32Value();

    i2c = sol_i2c_open(info[0]->Uint32Value(), speed);

    if (i2c) {
        info.GetReturnValue().Set(SolI2c::New(i2c));
    }
}

NAN_METHOD(bind_sol_i2c_open_raw)
{
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsUint32);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsUint32);

    sol_i2c *i2c = NULL;
    sol_i2c_speed speed = (sol_i2c_speed) info[1]->Uint32Value();
    i2c = sol_i2c_open_raw(info[0]->Uint32Value(), speed);

    if (i2c) {
        info.GetReturnValue().Set(SolI2c::New(i2c));
    }
}

NAN_METHOD(bind_sol_i2c_set_slave_address)
{
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsUint32);

    Local<Object> jsI2c = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_i2c *i2c = (sol_i2c *)SolI2c::Resolve(jsI2c);

    int returnValue = sol_i2c_set_slave_address(i2c, info[1]->Uint32Value());
    if (returnValue < 0) {
        info.GetReturnValue().Set(ReverseLookupConstant("E", abs(returnValue)));
    } else {
        info.GetReturnValue().Set(Nan::New(returnValue));
    }
}

NAN_METHOD(bind_sol_i2c_close)
{
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);

    Local<Object> jsI2c = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_i2c *i2c = (sol_i2c *)SolI2c::Resolve(jsI2c);

    sol_i2c_close(i2c);
    Nan::SetInternalFieldPointer(jsI2c, 0, 0);
}

NAN_METHOD(bind_sol_i2c_pending_cancel)
{
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsObject);

    Local<Object> jsI2c = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_i2c *i2c = (sol_i2c *)SolI2c::Resolve(jsI2c);

    Local<Object> jsI2cPending = Nan::To<Object>(info[1]).ToLocalChecked();
    sol_i2c_pending *i2c_pending = (sol_i2c_pending *)SolI2c::Resolve(jsI2cPending);

    sol_i2c_pending_cancel(i2c, i2c_pending);
    Nan::SetInternalFieldPointer(jsI2cPending, 0, 0);
}

static void sol_i2c_write_cb(void *cb_data, struct sol_i2c *i2c,
                             uint8_t *data, ssize_t status)
{
    Nan::HandleScope scope;
    Nan::Callback *callback = (Nan::Callback *)cb_data;
    Local<Value> buffer;

    if (status >= 0) {
        buffer = Nan::NewBuffer((char *)data, status).ToLocalChecked();
    } else {
        buffer = Nan::Null();
        free(data);
    }

    Local<Value> arguments[2] = {
        buffer,
        Nan::New((int)status)
    };

    callback->Call(2, arguments);
    delete callback;
    hijack_unref();
}

NAN_METHOD(bind_sol_i2c_write)
{
    VALIDATE_ARGUMENT_COUNT(info, 3);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 2, IsFunction);

    uint8_t *inputBuffer = (uint8_t *) 0;
    size_t count = 0;

    Local<Object> jsI2c = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_i2c *i2c = (sol_i2c *)SolI2c::Resolve(jsI2c);

    if (!node::Buffer::HasInstance(info[1])) {
        Nan::ThrowTypeError("Argument 1 must be a node Buffer");
        return;
    }

    count = node::Buffer::Length(info[1]);

    inputBuffer = (uint8_t *) calloc(count, sizeof(uint8_t));
    if (!inputBuffer)
    {
        Nan::ThrowError("Failed to allocate memory for input buffer");
        return;
    }

    memcpy(inputBuffer, node::Buffer::Data(info[1]), count);

    if (!hijack_ref()) {
        free(inputBuffer);
        return;
    }

    Nan::Callback *callback =
            new Nan::Callback(Local<Function>::Cast(info[2]));
    sol_i2c_pending *i2c_pending =
            sol_i2c_write(i2c, inputBuffer, count, sol_i2c_write_cb, callback);

    if (!i2c_pending) {
        free(inputBuffer);
        delete callback;
        hijack_unref();
        info.GetReturnValue().Set(ReverseLookupConstant("E", errno));
        return;
    }

    info.GetReturnValue().Set(SolI2c::New(i2c_pending));
}

static void sol_i2c_write_reg_cb(void *cb_data, struct sol_i2c *i2c,
                                 uint8_t reg, uint8_t *data, ssize_t status)
{
    Nan::HandleScope scope;
    Nan::Callback *callback = (Nan::Callback *)cb_data;
    Local<Value> buffer;

    if (status >= 0) {
        buffer = Nan::NewBuffer((char *)data, status).ToLocalChecked();
    } else {
        buffer = Nan::Null();
        free(data);
    }

    Local<Value> arguments[3] = {
        Nan::New(reg),
        buffer,
        Nan::New((int)status)
    };

    callback->Call(3, arguments);

    delete callback;
    hijack_unref();
}

NAN_METHOD(bind_sol_i2c_write_register)
{
    VALIDATE_ARGUMENT_COUNT(info, 4);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsUint32);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 2, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 3, IsFunction);

    uint8_t *inputBuffer = (uint8_t *) 0;
    size_t count;

    Local<Object> jsI2c = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_i2c *i2c = (sol_i2c *)SolI2c::Resolve(jsI2c);

    uint8_t reg = info[1]->Uint32Value();

    if (!node::Buffer::HasInstance(info[2])) {
        Nan::ThrowTypeError("Argument 2 must be a node Buffer");
        return;
    }

    count = node::Buffer::Length(info[2]);

    inputBuffer = (uint8_t *) calloc(count, sizeof(uint8_t));
    if (!inputBuffer) {
        Nan::ThrowError("Failed to allocate memory for input buffer");
        return;
    }

    memcpy(inputBuffer, node::Buffer::Data(info[2]), count);

    if (!hijack_ref()) {
        free(inputBuffer);
        return;
    }

    Nan::Callback *callback =
            new Nan::Callback(Local<Function>::Cast(info[3]));
    sol_i2c_pending *i2c_pending =
            sol_i2c_write_register(i2c, reg, inputBuffer, count,
                                   sol_i2c_write_reg_cb, callback);

    if (!i2c_pending) {
        free(inputBuffer);
        delete callback;
        hijack_unref();
        info.GetReturnValue().Set(ReverseLookupConstant("E", errno));
        return;
    }

    info.GetReturnValue().Set(SolI2c::New(i2c_pending));
}

static void sol_i2c_write_quick_cb(void *cb_data, struct sol_i2c *i2c,
                                   ssize_t status)
{
    Nan::HandleScope scope;
    Nan::Callback *callback = (Nan::Callback *)cb_data;

    Local<Value> arguments[1] = {
        Nan::New((int)status)
    };

    callback->Call(1, arguments);
    delete callback;
    hijack_unref();
}

NAN_METHOD(bind_sol_i2c_write_quick)
{
    VALIDATE_ARGUMENT_COUNT(info, 3);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsBoolean);
    VALIDATE_ARGUMENT_TYPE(info, 2, IsFunction);

    Local<Object> jsI2c = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_i2c *i2c = (sol_i2c *)SolI2c::Resolve(jsI2c);

    bool rw = info[1]->BooleanValue();

    if (!hijack_ref())
        return;

    Nan::Callback *callback =
            new Nan::Callback(Local<Function>::Cast(info[2]));

    sol_i2c_pending *i2c_pending =
            sol_i2c_write_quick(i2c, rw, sol_i2c_write_quick_cb, callback);

    if (!i2c_pending) {
        delete callback;
        hijack_unref();
        info.GetReturnValue().Set(ReverseLookupConstant("E", errno));
        return;
    }

    info.GetReturnValue().Set(SolI2c::New(i2c_pending));
}

static void sol_i2c_read_cb(void *cb_data, struct sol_i2c *i2c, uint8_t *data,
                            ssize_t status)
{
    Nan::HandleScope scope;
    Nan::Callback *callback = (Nan::Callback *)cb_data;
    Local<Value> buffer;

    if (status >= 0) {
        buffer = Nan::NewBuffer((char *)data, status).ToLocalChecked();
    } else {
        buffer = Nan::Null();
        free(data);
    }

    Local<Value> arguments[2] = {
        buffer,
        Nan::New((int)status)
    };

    callback->Call(2, arguments);
    delete callback;
    hijack_unref();
}

NAN_METHOD(bind_sol_i2c_read)
{
    VALIDATE_ARGUMENT_COUNT(info, 3);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsUint32);
    VALIDATE_ARGUMENT_TYPE(info, 2, IsFunction);

    uint8_t *outputBuffer = (uint8_t *) 0;
    Local<Object> jsI2c = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_i2c *i2c = (sol_i2c *)SolI2c::Resolve(jsI2c);

    size_t count = info[1]->Uint32Value();
    outputBuffer = (uint8_t *) calloc(count, sizeof(uint8_t));

    if (!outputBuffer) {
        Nan::ThrowError("Failed to allocate memory for output buffer");
        return;
    }

    if (!hijack_ref()) {
        free(outputBuffer);
        return;
    }

    Nan::Callback *callback =
            new Nan::Callback(Local<Function>::Cast(info[2]));

    sol_i2c_pending *i2c_pending =
            sol_i2c_read(i2c, outputBuffer, count, sol_i2c_read_cb, callback);

    if (!i2c_pending) {
        free(outputBuffer);
        delete callback;
        hijack_unref();
        info.GetReturnValue().Set(ReverseLookupConstant("E", errno));
        return;
    }

    info.GetReturnValue().Set(SolI2c::New(i2c_pending));
}

static void sol_i2c_read_reg_cb(void *cb_data, struct sol_i2c *i2c,
                                uint8_t reg, uint8_t *data, ssize_t status)
{
    Nan::HandleScope scope;
    Nan::Callback *callback = (Nan::Callback *)cb_data;
    Local<Value> buffer;

    if (status >= 0) {
        buffer = Nan::NewBuffer((char *)data, status).ToLocalChecked();
    } else {
        free(data);
        buffer = Nan::Null();
    }

    Local<Value> arguments[3] = {
        Nan::New(reg),
        buffer,
        Nan::New((int)status)
    };

    callback->Call(3, arguments);
    delete callback;
    hijack_unref();
}

NAN_METHOD(bind_sol_i2c_read_register)
{
    VALIDATE_ARGUMENT_COUNT(info, 4);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsUint32);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 2, IsUint32);
    VALIDATE_ARGUMENT_TYPE(info, 3, IsFunction);

    uint8_t *outputBuffer = (uint8_t *) 0;
    Local<Object> jsI2c = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_i2c *i2c = (sol_i2c *)SolI2c::Resolve(jsI2c);

    uint8_t reg = info[1]->Uint32Value();
    size_t count = info[2]->Uint32Value();

    outputBuffer = (uint8_t *) calloc(count, sizeof(uint8_t));
    if (!outputBuffer) {
        Nan::ThrowError("Failed to allocate memory for output buffer");
        return;
    }

    if (!hijack_ref()) {
        free(outputBuffer);
        return;
    }

    Nan::Callback *callback =
            new Nan::Callback(Local<Function>::Cast(info[3]));

    sol_i2c_pending *i2c_pending =
            sol_i2c_read_register(i2c, reg, outputBuffer, count,
                                  sol_i2c_read_reg_cb, callback);

    if (!i2c_pending) {
        free(outputBuffer);
        delete callback;
        hijack_unref();
        info.GetReturnValue().Set(ReverseLookupConstant("E", errno));
        return;
    }

    info.GetReturnValue().Set(SolI2c::New(i2c_pending));
}

NAN_METHOD(bind_sol_i2c_read_register_multiple)
{
    VALIDATE_ARGUMENT_COUNT(info, 5);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsUint32);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 2, IsUint32);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 3, IsUint32);
    VALIDATE_ARGUMENT_TYPE(info, 4, IsFunction);

    uint8_t *outputBuffer = (uint8_t *) 0;
    Local<Object> jsI2c = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_i2c *i2c = (sol_i2c *)SolI2c::Resolve(jsI2c);

    uint8_t reg = info[1]->Uint32Value();
    size_t count = info[2]->Uint32Value();
    uint8_t times = info[3]->Uint32Value();

    outputBuffer = (uint8_t *) calloc(times * count, sizeof(uint8_t));
    if (!outputBuffer) {
        Nan::ThrowError("Failed to allocate memory for output buffer");
        return;
    }

    if (!hijack_ref()) {
        free(outputBuffer);
        return;
    }

    Nan::Callback *callback =
            new Nan::Callback(Local<Function>::Cast(info[4]));

    sol_i2c_pending *i2c_pending =
            sol_i2c_read_register_multiple(i2c, reg, outputBuffer,
                                           count, times,
                                           sol_i2c_read_reg_cb, callback);

    if (!i2c_pending) {
        free(outputBuffer);
        delete callback;
        hijack_unref();
        info.GetReturnValue().Set(ReverseLookupConstant("E", errno));
        return;
    }

    info.GetReturnValue().Set(SolI2c::New(i2c_pending));
}

NAN_METHOD(bind_sol_i2c_speed_from_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsString);

    sol_i2c_speed speed = sol_i2c_speed_from_str(
        (const char *)*String::Utf8Value(info[0]));
    info.GetReturnValue().Set(Nan::New(speed));
}

NAN_METHOD(bind_sol_i2c_speed_to_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsInt32);

    const char *idString = sol_i2c_speed_to_str(
        (sol_i2c_speed)info[0]->Int32Value());

    if (idString) {
        info.GetReturnValue().Set(Nan::New(idString).ToLocalChecked());
    } else {
        info.GetReturnValue().Set(Nan::Null());
    }
}
