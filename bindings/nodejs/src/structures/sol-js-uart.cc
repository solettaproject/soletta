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
#include "../common.h"
#include "sol-js-uart.h"

using namespace v8;

bool c_sol_uart_config(v8::Local<v8::Object> jsUARTConfig,
    sol_uart_data *uart_data, sol_uart_config *config) {
    SOL_SET_API_VERSION(config->api_version = SOL_UART_CONFIG_API_VERSION;)

    VALIDATE_AND_ASSIGN((*config), baud_rate, sol_uart_baud_rate, IsInt32,
        "(Baud rate)", false, jsUARTConfig, Int32Value);

    VALIDATE_AND_ASSIGN((*config), data_bits, sol_uart_data_bits, IsInt32,
        "(Amount of data bits)", false, jsUARTConfig, Int32Value);

    VALIDATE_AND_ASSIGN((*config), parity, sol_uart_parity, IsInt32,
        "(Parity characteristic)", false, jsUARTConfig, Int32Value);

    VALIDATE_AND_ASSIGN((*config), stop_bits, sol_uart_stop_bits, IsInt32,
        "(Amount of stop bits)", false, jsUARTConfig, Int32Value);

    Local<Value> read_cb = Nan::Get(jsUARTConfig,
        Nan::New("callback").ToLocalChecked()).ToLocalChecked();
    if (read_cb->IsFunction()) {
        Nan::Callback *rx_cb =
            new Nan::Callback(Local<Function>::Cast(read_cb));

        uart_data->rx_cb = rx_cb;
        config->rx_cb_user_data = uart_data;
    }

    VALIDATE_AND_ASSIGN((*config), flow_control, bool, IsBoolean,
        "(Enable software flow control)", false, jsUARTConfig,
        BooleanValue);

    return true;
}
