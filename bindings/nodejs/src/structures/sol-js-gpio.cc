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

#include <nan.h>
#include "sol-js-gpio.h"
#include "../common.h"

using namespace v8;

bool c_sol_gpio_config(v8::Local<v8::Object> jsGPIOConfig, 
    sol_gpio_data *gpio_data, sol_gpio_config *config) {
    SOL_SET_API_VERSION(config->api_version = SOL_GPIO_CONFIG_API_VERSION; )

    VALIDATE_AND_ASSIGN((*config), dir, sol_gpio_direction, IsUint32,
                        "(GPIO direction)", false, jsGPIOConfig,
                        Uint32Value);

    VALIDATE_AND_ASSIGN((*config), drive_mode, sol_gpio_drive, IsUint32,
                        "(GPIO pull-up/pull-down resistor)", false, jsGPIOConfig,
                        Uint32Value);

    VALIDATE_AND_ASSIGN((*config), active_low, bool, IsBoolean,
                        "(GPIO active_low state)", false, jsGPIOConfig,
                        BooleanValue);

    if (config->dir == SOL_GPIO_DIR_IN) {
        Local<Value> poll_timeout =
            Nan::Get(jsGPIOConfig, Nan::New("poll_timeout").ToLocalChecked())
                .ToLocalChecked();
        VALIDATE_VALUE_TYPE(poll_timeout, IsUint32, "GPIO in poll_timeout",
            false);
        config->in.poll_timeout = (uint32_t)poll_timeout->Uint32Value();

        Local<Value> trigger_mode =
            Nan::Get(jsGPIOConfig, Nan::New("trigger_mode").ToLocalChecked())
                .ToLocalChecked();
        VALIDATE_VALUE_TYPE(trigger_mode, IsString, "GPIO in trigger_mode",
            false);
        config->in.trigger_mode = (sol_gpio_edge)sol_gpio_edge_from_str(
            (const char *)*(String::Utf8Value(trigger_mode)));

        Local<Value> read_cb = Nan::Get(jsGPIOConfig,
            Nan::New("callback").ToLocalChecked()).ToLocalChecked();

        if (read_cb->IsFunction()) {
            Nan::Callback *callback =
                new Nan::Callback(Local<Function>::Cast(read_cb));
            gpio_data->callback = callback;
            config->in.user_data = gpio_data;
        }
    }

    return true;
}
