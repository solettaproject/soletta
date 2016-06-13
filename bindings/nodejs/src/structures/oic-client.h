/*
 * This file is part of the Soletta™ Project
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

#pragma once

#include <sol-oic-client.h>
#include "../structures/js-handle.h"

class SolOicClientResource : public JSReffableHandle<SolOicClientResource> {
public:
    static const char *jsClassName();
    static void ref(void *data);
    static void unref(void *data);
    static v8::Local<v8::Object> New(struct sol_oic_resource *resource);
};

class SolOicClient : public JSReffableHandle<SolOicClient> {
public:
    static const char *jsClassName();
    static void ref(void *data);
    static void unref(void *data);
};

class OicCallbackData {
protected:
    OicCallbackData();
public:
    bool init(v8::Local<v8::Object> jsClient, v8::Local<v8::Function> jsCallback);
    virtual ~OicCallbackData();
    static OicCallbackData *New(v8::Local<v8::Object> jsClient, v8::Local<v8::Function> jsCallback);
    Nan::Persistent<v8::Object> *jsClient;
    Nan::Callback *callback;
private:
    bool hijackRefWasSuccessful;
};
