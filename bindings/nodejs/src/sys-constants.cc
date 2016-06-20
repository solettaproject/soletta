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

#include <nan.h>
#include "sys-constants.h"

using namespace v8;

namespace node {void DefineConstants(Local<Object> target);}

static Nan::Persistent<Object> forwardTable;
static Nan::Persistent<Object> reverseTable;

NAN_METHOD(bind__sysConstants) {
	if (info.Length() < 1) {
		Local<Object> raw = Nan::New<Object>();
		node::DefineConstants(raw);
		info.GetReturnValue().Set(raw);
	} else {
		Local<Object> constants = Nan::To<Object>(info[0]).ToLocalChecked();
		Local<Object> forward = Nan::To<Object>(Nan::Get(constants,
			Nan::New("forward").ToLocalChecked()).ToLocalChecked())
				.ToLocalChecked();
		Local<Object> reverse = Nan::To<Object>(Nan::Get(constants,
			Nan::New("reverse").ToLocalChecked()).ToLocalChecked())
				.ToLocalChecked();
		forwardTable.Reset(forward);
		reverseTable.Reset(reverse);
	}
}

Local<Value> ReverseLookupConstant(const char *nameSpace, int value) {
	Local<Value> returnValue = Nan::Undefined();

	Local<Value> jsNameSpaceValue = Nan::Get(Nan::New<Object>(reverseTable),
		Nan::New(nameSpace).ToLocalChecked()).ToLocalChecked();
	if (jsNameSpaceValue->IsObject()) {
		Local<Object> jsNameSpace =
			Nan::To<Object>(jsNameSpaceValue).ToLocalChecked();
		returnValue = Nan::Get(jsNameSpace, Nan::New(value)).ToLocalChecked();
	}

	return returnValue;
}
