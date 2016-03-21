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

#pragma once

#include <string>
#include <nan.h>
#include <sol-macros.h>

template <class T> class JSHandle {
    static Nan::Persistent<v8::FunctionTemplate> &
    theTemplate()
    {
        static Nan::Persistent<v8::FunctionTemplate> returnValue;

        if (SOL_UNLIKELY(returnValue.IsEmpty())) {
            v8::Local<v8::FunctionTemplate> theTemplate =
                Nan::New<v8::FunctionTemplate>();
            theTemplate
            ->SetClassName(Nan::New(T::jsClassName()).ToLocalChecked());
            theTemplate->InstanceTemplate()->SetInternalFieldCount(1);
            Nan::Set(Nan::GetFunction(theTemplate).ToLocalChecked(),
                Nan::New("displayName").ToLocalChecked(),
                Nan::New(T::jsClassName()).ToLocalChecked());
            returnValue.Reset(theTemplate);
        }
        return returnValue;
    }
public:
    static v8::Local<v8::Object> New(void *data)
    {
        v8::Local<v8::Object> returnValue =
            Nan::GetFunction(Nan::New(theTemplate())).ToLocalChecked()
            ->NewInstance();
        Nan::SetInternalFieldPointer(returnValue, 0, data);

        return returnValue;
    }

    // If the object is not of the expected type, or if the pointer inside the
    // object has already been removed, then we must throw an error
    static void *
    Resolve(v8::Local<v8::Object> jsObject)
    {
        void *returnValue = 0;

        if (Nan::New(theTemplate())->HasInstance(jsObject)) {
            returnValue = Nan::GetInternalFieldPointer(jsObject, 0);
        }
        if (!returnValue) {
            Nan::ThrowTypeError((std::string("Object is not of type ") +
                T::jsClassName()).c_str());
        }
        return returnValue;
    }
};

class UnrefData {
public:
    UnrefData(void *_data, void (*_unref)(void *), v8::Local<v8::Object> js);
    virtual ~UnrefData();
    void *data;
    void (*unref)(void *);
    Nan::Persistent<v8::Object> *persistent;
};

template <class T> class JSReffableHandle : public JSHandle<T> {
    static void
    InstanceIsGone(const Nan::WeakCallbackInfo<UnrefData> & data)
    {
        delete data.GetParameter();
    }
public:
    static v8::Local<v8::Object> New(void *data)
    {
        v8::Local<v8::Object> theObject = JSHandle<T>::New(data);
        T::ref(data);
        UnrefData *unrefData = new UnrefData(data, T::unref, theObject);

        unrefData->persistent->SetWeak(unrefData, InstanceIsGone,
            Nan::WeakCallbackType::kParameter);
        return theObject;
    }
};
