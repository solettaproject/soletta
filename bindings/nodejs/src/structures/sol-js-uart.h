/*
 * This file is part of the Soletta (TM) Project
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

#ifndef __SOLETTA_JS_UART_H__
#define __SOLETTA_JS_UART_H__

#include <v8.h>
#include <sol-uart.h>
#include <map>

struct CallbackInfo {
    Nan::Callback *callback;
    Nan::Persistent<v8::Value> *js_buffer;
};

struct sol_uart_data {
    sol_uart *uart;
    Nan::Callback *on_data_cb;
    Nan::Callback *on_feed_done_cb;
    std::map<sol_blob *, CallbackInfo *> feed_callbacks_map;
};

bool c_sol_uart_config(v8::Local<v8::Object> UARTConfig, sol_uart_data *data,
    sol_uart_config *config);

#endif /* __SOLETTA_JS_UART_H__ */
