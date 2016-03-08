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

#pragma once

#define SET_FUNCTION(destination, functionName) \
    Nan::ForceSet((destination), Nan::New(#functionName).ToLocalChecked(), \
    Nan::GetFunction(Nan::New<FunctionTemplate>( \
    bind_ ## functionName)).ToLocalChecked(), \
    (v8::PropertyAttribute)(v8::DontDelete));

#define SET_CONSTANT_NUMBER(destination, name) \
    Nan::ForceSet((destination), Nan::New(#name).ToLocalChecked(), \
    Nan::New((name)), \
    (v8::PropertyAttribute)(v8::ReadOnly | v8::DontDelete));

#define SET_CONSTANT_STRING(destination, name) \
    Nan::ForceSet((destination), Nan::New(#name).ToLocalChecked(), \
    Nan::New((name)).ToLocalChecked(), \
    (v8::PropertyAttribute)(v8::ReadOnly | v8::DontDelete));

#define SET_CONSTANT_OBJECT(destination, name) \
    Nan::ForceSet((destination), Nan::New(#name).ToLocalChecked(), \
    bind_ ## name, \
    (v8::PropertyAttribute)(v8::ReadOnly | v8::DontDelete));

#define VALIDATE_CALLBACK_RETURN_VALUE_TYPE(value, typecheck, message, fallback) \
    if (!value->typecheck()) { \
        Nan::ThrowTypeError( \
            message " callback return value type must satisfy " #typecheck "()"); \
        return fallback; \
    }

#define VALIDATE_ARGUMENT_COUNT(args, length) \
    if ((args).Length() < (length)) { \
        return Nan::ThrowRangeError("Argument count must be exactly " #length); \
    }

#define VALIDATE_ARGUMENT_TYPE(args, index, typecheck) \
    if (!(args)[(index)]->typecheck()) { \
        return Nan::ThrowTypeError("Argument " #index " must satisfy " #typecheck \
            "()"); \
    }

#define VALIDATE_VALUE_TYPE(value, typecheck, message, failReturn) \
    if (!(value)->typecheck()) { \
        Nan::ThrowTypeError(message " must satisfy " #typecheck "()"); \
        return failReturn; \
    }

#define VALIDATE_VALUE_TYPE_OR_FREE(value, typecheck, message, failReturn, \
        pointer_to_free, free_function) \
    if (!(value)->typecheck()) { \
        Nan::ThrowTypeError(message " must satisfy " #typecheck "()"); \
        free_function((pointer_to_free)); \
        return failReturn; \
    }

#define VALIDATE_ARGUMENT_TYPE_OR_NULL(args, index, typecheck) \
    if (!((args)[(index)]->typecheck() || (args)[(index)]->IsNull())) { \
        return Nan::ThrowTypeError("Argument " #index " must satisfy " #typecheck \
            "() or IsNull()"); \
    }

#define SET_STRING_IF_NOT_NULL(destination, source, memberName) \
    if ((source)->memberName) { \
        Nan::Set((destination), Nan::New(#memberName).ToLocalChecked(), \
            Nan::New((source)->memberName).ToLocalChecked()); \
    }

#define SET_VALUE_ON_OBJECT(destination, type, source, memberName) \
    Nan::Set((destination), Nan::New(#memberName).ToLocalChecked(), \
    Nan::New<type>((source)->memberName));

#define VALIDATE_AND_ASSIGN(destination, memberName, destinationType, \
        typecheck, message, failReturn, source, accessor) \
    Local<Value> memberName = \
        Nan::Get(source, Nan::New(#memberName).ToLocalChecked()) \
        .ToLocalChecked(); \
    VALIDATE_VALUE_TYPE(memberName, typecheck, message "." #memberName, \
    failReturn); \
    destination.memberName = (destinationType)memberName->accessor();
