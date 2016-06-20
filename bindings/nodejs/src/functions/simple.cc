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
#include "../common.h"

#include <sol-platform.h>

using namespace v8;

#define RETURN_CONSTANT_STRING_NO_PARAMS(functionName) \
    do { \
        VALIDATE_ARGUMENT_COUNT(info, 0); \
        const char *theReturnValue = functionName(); \
        if (theReturnValue) { \
            info.GetReturnValue().Set(Nan::New(theReturnValue).ToLocalChecked()); \
        } else { \
            info.GetReturnValue().Set(Nan::Null()); \
        } \
    } while(0)

NAN_METHOD(bind_sol_platform_get_machine_id) {
    RETURN_CONSTANT_STRING_NO_PARAMS(sol_platform_get_machine_id);
}
NAN_METHOD(bind_sol_platform_get_hostname) {
    RETURN_CONSTANT_STRING_NO_PARAMS(sol_platform_get_hostname);
}
NAN_METHOD(bind_sol_platform_get_board_name) {
    RETURN_CONSTANT_STRING_NO_PARAMS(sol_platform_get_board_name);
}
NAN_METHOD(bind_sol_platform_get_os_version) {
    RETURN_CONSTANT_STRING_NO_PARAMS(sol_platform_get_os_version);
}
NAN_METHOD(bind_sol_platform_get_serial_number) {
    RETURN_CONSTANT_STRING_NO_PARAMS(sol_platform_get_serial_number);
}
NAN_METHOD(bind_sol_platform_get_sw_version) {
    RETURN_CONSTANT_STRING_NO_PARAMS(sol_platform_get_sw_version);
}
NAN_METHOD(bind_sol_platform_get_timezone) {
    RETURN_CONSTANT_STRING_NO_PARAMS(sol_platform_get_timezone);
}

NAN_METHOD(bind_sol_platform_get_locale) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsInt32);
    const char *returnValue = sol_platform_get_locale(
        (enum sol_platform_locale_category)info[0]->Int32Value());
    if (returnValue) {
        info.GetReturnValue().Set(Nan::New(returnValue).ToLocalChecked());
    } else {
        info.GetReturnValue().Set(Nan::Null());
    }
}

NAN_METHOD(bind_sol_platform_get_service_state) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsString);
    info.GetReturnValue().Set(Nan::New((int)sol_platform_get_service_state(
        (const char *)*String::Utf8Value(info[0]))));
}

NAN_METHOD(bind_sol_platform_get_state) {
    VALIDATE_ARGUMENT_COUNT(info, 0);
    info.GetReturnValue().Set(Nan::New(sol_platform_get_state()));
}

NAN_METHOD(bind_sol_platform_get_system_clock) {
    VALIDATE_ARGUMENT_COUNT(info, 0);
    info.GetReturnValue().Set(
        Nan::New((double)sol_platform_get_system_clock()));
}

#define RETURN_INT_SINGLE_STRING_PARAM(functionName) \
    VALIDATE_ARGUMENT_COUNT(info, 1); \
    VALIDATE_ARGUMENT_TYPE(info, 0, IsString); \
    info.GetReturnValue().Set(Nan::New(functionName((const char *)*String::Utf8Value(info[0]))));

NAN_METHOD(bind_sol_platform_start_service) {
    RETURN_INT_SINGLE_STRING_PARAM(sol_platform_start_service);
}
NAN_METHOD(bind_sol_platform_stop_service) {
    RETURN_INT_SINGLE_STRING_PARAM(sol_platform_stop_service);
}
NAN_METHOD(bind_sol_platform_restart_service) {
    RETURN_INT_SINGLE_STRING_PARAM(sol_platform_restart_service);
}
NAN_METHOD(bind_sol_platform_set_hostname) {
    RETURN_INT_SINGLE_STRING_PARAM(sol_platform_set_hostname);
}
NAN_METHOD(bind_sol_platform_set_target) {
    RETURN_INT_SINGLE_STRING_PARAM(sol_platform_set_target);
}
NAN_METHOD(bind_sol_platform_set_timezone) {
    RETURN_INT_SINGLE_STRING_PARAM(sol_platform_set_timezone);
}
NAN_METHOD(bind_sol_platform_set_system_clock) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsNumber);
    info.GetReturnValue().Set(Nan::New(sol_platform_set_system_clock(
        (int64_t)info[0]->NumberValue())));
}
NAN_METHOD(bind_sol_platform_set_locale) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsUint32);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsString);
    info.GetReturnValue().Set(Nan::New(sol_platform_set_locale(
        (enum sol_platform_locale_category)info[0]->Uint32Value(),
        (const char *)*String::Utf8Value(info[0]))));
}
