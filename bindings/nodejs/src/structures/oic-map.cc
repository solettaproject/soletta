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

#include <string>
#include <nan.h>

#include "../common.h"
#include "../data.h"
#include "oic-map.h"

using namespace v8;

Local<Value> js_sol_oic_map_reader(
    const struct sol_oic_map_reader *representation) {
    if (!representation) {
        return Nan::Null();
    }
    Local<Object> returnValue = Nan::New<Object>();

    struct sol_oic_repr_field field;
    enum sol_oic_map_loop_reason end_reason;
    struct sol_oic_map_reader iterator = {0, 0, 0, 0, 0, 0};
    SOL_OIC_MAP_LOOP(representation, &field, &iterator, end_reason) {
        Local<Value> jsValue;
        if (field.type == SOL_OIC_REPR_TYPE_UINT) {
            jsValue = Nan::New<Uint32>((uint32_t)field.v_uint);
        } else if (field.type == SOL_OIC_REPR_TYPE_INT) {
            jsValue = Nan::New<Int32>((int32_t)field.v_int);
        } else if (field.type == SOL_OIC_REPR_TYPE_SIMPLE) {
            jsValue = Nan::New(field.v_simple);
        } else if (field.type == SOL_OIC_REPR_TYPE_TEXT_STRING) {
            jsValue = Nan::New<String>(field.v_slice.data,
                field.v_slice.len).ToLocalChecked();
        } else if (field.type == SOL_OIC_REPR_TYPE_BYTE_STRING) {
            jsValue = jsArrayFromBytes((unsigned char *)(field.v_slice.data),
                        field.v_slice.len);
        } else if (field.type == SOL_OIC_REPR_TYPE_FLOAT) {
            jsValue = Nan::New(field.v_float);
        } else if (field.type == SOL_OIC_REPR_TYPE_DOUBLE) {
            jsValue = Nan::New(field.v_double);
        } else {
            jsValue = Nan::Undefined();
        }
        Nan::Set(returnValue, Nan::New(field.key).ToLocalChecked(), jsValue);
    }

    return returnValue;
}

static bool encodeSingleValue(const char *name, Local<Value> value,
    struct sol_oic_map_writer *map) {
    struct sol_oic_repr_field field;
    bool returnValue = true;
    std::string buffer("");

    if (value->IsInt32()) {
        field.key = name;
        field.type = SOL_OIC_REPR_TYPE_INT;
        field.v_uint = value->Int32Value();
    }
    else
    if (value->IsUint32()) {
        field.key = name;
        field.type = SOL_OIC_REPR_TYPE_TEXT_STRING;
        field.v_uint = value->Uint32Value();
    }
    else
    if (value->IsString()) {
        char *theString = 0;
        if (!c_StringNew(value->ToString(), &theString)) {
            return false;
        }
        field.key = name;
        field.type = SOL_OIC_REPR_TYPE_TEXT_STRING;
        field.v_slice.data = theString;
        field.v_slice.len = strlen(theString);
    }
    else
    if (value->IsArray()) {
        unsigned char *theData = 0;
        size_t theDataLength = 0;

        Local<Array> array = Local<Array>::Cast(value);
        theDataLength = array->Length();
        theData = (unsigned char *)malloc(theDataLength);
        if (!theData) {
            buffer += std::string(name) + ": unable to allocate array";
            goto error;
        }

        if (!fillCArrayFromJSArray(theData, theDataLength, array)) {
            free(theData);
            return false;
        }

        field.key = name;
        field.type = SOL_OIC_REPR_TYPE_BYTE_STRING;
        field.v_slice.data = (const char *)theData;
        field.v_slice.len = theDataLength;
    }
    else
    if (value->IsNumber()) {
        field.key = name;
        field.type = SOL_OIC_REPR_TYPE_DOUBLE;
        field.v_double = value->NumberValue();
    }
    else {
        buffer += std::string(name) + ": unable to handle value type";
        goto error;
    }

    returnValue = sol_oic_map_append(map, &field);
    if (field.type == SOL_OIC_REPR_TYPE_TEXT_STRING ||
            field.type == SOL_OIC_REPR_TYPE_BYTE_STRING) {
        free((void *)(field.v_slice.data));
    }
    if (!returnValue) {
        buffer += std::string(name) + ": encoding failed";
        goto error;
    }

    return true;
error:
    Nan::ThrowError(buffer.c_str());
    return false;
}

bool c_sol_oic_map_writer(Local<Object> payload,
    struct sol_oic_map_writer *map) {
    bool returnValue = true;

    uint32_t index, length;
    Local<Array> propertyNames =
        Nan::GetPropertyNames(payload).ToLocalChecked();
    length = propertyNames->Length();
    for (index = 0; index < length && returnValue; index++) {
        Local<Value> name = Nan::Get(propertyNames, index).ToLocalChecked();
        Local<Value> value = Nan::Get(payload, name).ToLocalChecked();
        returnValue = encodeSingleValue((const char *)*String::Utf8Value(name),
            value, map);
    }

    return returnValue;
}

bool oic_map_writer_callback(void *data, struct sol_oic_map_writer *map) {
    bool returnValue = true;

    if (data) {
        Nan::HandleScope scope;
        Nan::Persistent<Object> *jsPayload = (Nan::Persistent<Object> *)data;
        returnValue = c_sol_oic_map_writer(Nan::New<Object>(*jsPayload), map);
    }

    return returnValue;
}

