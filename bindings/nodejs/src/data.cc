/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <nan.h>
#include <v8.h>
#include "data.h"
#include "common.h"

using namespace v8;

Local<Array> jsArrayFromBytes(unsigned char *bytes, size_t length) {
  size_t index;
  Local<Array> returnValue = Nan::New<Array>(length);

  for (index = 0; index < length; index++) {
    Nan::Set(returnValue, index, Nan::New(bytes[index]));
  }
  return returnValue;
}

bool fillCArrayFromJSArray(unsigned char *bytes, size_t length,
                                  Local<Array> array) {
  size_t index, arrayLength;

  arrayLength = array->Length();
  if (arrayLength != length) {
    Nan::ThrowError("byte array has the wrong length");
    return false;
  }

  for (index = 0; index < length; index++) {
    Local<Value> byte = Nan::Get(array, index).ToLocalChecked();
    VALIDATE_VALUE_TYPE(byte, IsUint32, "byte array item", false);
    bytes[index] = (unsigned char)(byte->Uint32Value());
  }

  return true;
}

bool c_StringNew(Local<String> jsString, char **p_string) {
  size_t length = strlen((const char *)*(String::Utf8Value(jsString)));
  char *string = (char *)malloc(length + 1);
  if (!string) {
    Nan::ThrowError("Failed to allocate memory for C string");
    return false;
  }
  string[length] = 0;
  strcpy(string, (const char *)*(String::Utf8Value(jsString)));

  *p_string = string;
  return true;
}
