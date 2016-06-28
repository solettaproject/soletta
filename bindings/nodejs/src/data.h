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

#ifndef __SOLETTA_JS_DATA_H__
#define __SOLETTA_JS_DATA_H__

#include <v8.h>

v8::Local<v8::Array> jsArrayFromBytes(unsigned char *bytes, size_t length);
bool fillCArrayFromJSArray(unsigned char *bytes, size_t length,
    v8::Local<v8::Array> array);
bool c_StringNew(v8::Local<v8::String> jsString, char **p_string);

#endif /* ndef __SOLETTA_JS_DATA_H__ */
