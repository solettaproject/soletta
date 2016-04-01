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

#include <sol-gpio.h>

#include <v8.h>
#include <node.h>
#include <nan.h>
#include <map>

#include "../common.h"
#include "../hijack.h"
#include "../structures/sol-js-gpio.h"
#include "../structures/js-handle.h"

using namespace v8;

class SolGpio : public JSHandle<SolGpio> {
public:
    static const char *jsClassName() { return "SolGpio"; }
};

static void sol_gpio_read_callback(void *data, struct sol_gpio *gpio, bool value) {
    Nan::HandleScope scope;
    sol_gpio_data *gpio_data = (sol_gpio_data *)data;
    Nan::Callback *callback = gpio_data->callback;
    if (!callback)
        return;

    Local<Value> arguments[2] = {
        SolGpio::New(gpio_data),
        Nan::New(value)
    };
    callback->Call(2, arguments);
}

NAN_METHOD(bind_sol_gpio_open) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsUint32);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsObject);

    uint32_t pin;
    sol_gpio_config config;
    sol_gpio *gpio = NULL;

    pin = info[0]->Uint32Value();
    sol_gpio_data *gpio_data = new sol_gpio_data;
    gpio_data->callback = NULL;

    if (!c_sol_gpio_config(info[1]->ToObject(), gpio_data, &config)) {
        delete gpio_data;
        Nan::ThrowError("Unable to extract sol_gpio_config\n");
        return;
    }

    Nan::Callback *callback = gpio_data->callback;
    if (callback) {
        if (!hijack_ref()) {
            delete callback;
            delete gpio_data;
            Nan::ThrowError("Failed to ref for mainloop");
            return;
        }
        config.in.cb = sol_gpio_read_callback;
    }

    gpio = sol_gpio_open(pin, &config);
    if (gpio) {
        gpio_data->gpio = gpio;
        info.GetReturnValue().Set(SolGpio::New(gpio_data));
        return;
    } else {
        if (callback)
            delete callback;
        delete gpio_data;
    }
}

NAN_METHOD(bind_sol_gpio_close) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    Local<Object> jsGpio = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_gpio_data *gpio_data = (sol_gpio_data *)SolGpio::Resolve(jsGpio);
    sol_gpio *gpio;

    if (!gpio_data)
        return;
    gpio = gpio_data->gpio;

    Nan::Callback *callback = gpio_data->callback;
    sol_gpio_close(gpio);
    if (callback) {
        delete callback;
        delete gpio_data;
        hijack_unref();
    }
}

NAN_METHOD(bind_sol_gpio_write) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsBoolean);
    bool value;
    Local<Object> jsGpio = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_gpio_data *gpio_data = (sol_gpio_data *)SolGpio::Resolve(jsGpio);
    sol_gpio *gpio;

    if (!gpio_data)
        return;

    gpio = gpio_data->gpio;
    value = info[1]->BooleanValue();

    info.GetReturnValue().Set(Nan::New(sol_gpio_write(gpio, value)));
}

NAN_METHOD(bind_sol_gpio_read) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    Local<Object> jsGpio = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_gpio_data *gpio_data = (sol_gpio_data *)SolGpio::Resolve(jsGpio);
    sol_gpio *gpio;

    if (!gpio_data)
        return;

    gpio = gpio_data->gpio;
    info.GetReturnValue().Set(Nan::New(sol_gpio_read(gpio)));
}
