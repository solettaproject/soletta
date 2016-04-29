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
    enum sol_oic_map_loop_status end_status;
    struct sol_oic_map_reader iterator = {0, 0, 0, 0, 0, 0};
    SOL_OIC_MAP_LOOP(representation, &field, &iterator, end_status) {
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
        field.type = SOL_OIC_REPR_TYPE_UINT;
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

