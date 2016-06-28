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

 #ifndef __SOLETTA_JS_GPIO_H__
#define __SOLETTA_JS_GPIO_H__

#include <v8.h>
#include <sol-gpio.h>

struct sol_gpio_data {
    sol_gpio *gpio;
    Nan::Callback *callback;
};

bool c_sol_gpio_config(v8::Local<v8::Object> gpioConfig, sol_gpio_data *data,
    sol_gpio_config *config);

#endif /* __SOLETTA_JS_GPIO_H__ */

