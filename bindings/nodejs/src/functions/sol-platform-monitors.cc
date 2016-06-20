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

#include <string.h>
#include <nan.h>
#include <sol-platform.h>

#include "../hijack.h"
#include "../common.h"
#include "../structures/js-handle.h"

using namespace v8;

class SolPlatformMonitor: public JSHandle<SolPlatformMonitor> {
public:
    static const char *jsClassName() { return "SolPlatformMonitor"; }
};

#define ADD_MONITOR(name, marshaller) \
    do { \
        VALIDATE_ARGUMENT_COUNT(info, 1); \
        VALIDATE_ARGUMENT_TYPE(info, 0, IsFunction); \
        int result; \
\
        if (!hijack_ref()) { \
            return; \
        } \
\
        Nan::Callback *callback = \
            new Nan::Callback(Local<Function>::Cast(info[0])); \
        if (!callback) { \
            result = -ENOMEM; \
            goto error; \
        } \
\
        result = sol_platform_add_##name##_monitor((marshaller), callback); \
        if (result) { \
            goto free_callback_error; \
        } \
\
        info.GetReturnValue().Set(SolPlatformMonitor::New(callback)); \
        return; \
    free_callback_error: \
        delete callback; \
    error: \
        hijack_unref(); \
        Nan::ThrowError(strerror(-result)); \
    } while(0)

#define DEL_MONITOR(name, marshaller) \
    do { \
        VALIDATE_ARGUMENT_COUNT(info, 1); \
        VALIDATE_ARGUMENT_TYPE(info, 0, IsObject); \
        int result; \
        Local<Object> jsHandle = Nan::To<Object>(info[0]).ToLocalChecked(); \
        Nan::Callback *callback = \
            (Nan::Callback *)SolPlatformMonitor::Resolve(jsHandle); \
        if (!(callback && hijack_unref())) { \
            return; \
        } \
        result = sol_platform_del_##name##_monitor((marshaller), callback); \
        if (result) { \
            hijack_ref(); \
            return Nan::ThrowError(strerror(-result)); \
        } \
\
        delete callback; \
        Nan::SetInternalFieldPointer(jsHandle, 0, 0); \
        info.GetReturnValue().Set(Nan::New(result)); \
    } while(0)

static void stringMonitor(void *data, const char *newValue) {
    Nan::HandleScope scope;
    Local<Value> arguments[1] = {Nan::New(newValue).ToLocalChecked()};
    ((Nan::Callback *)data)->Call(1, arguments);
}

NAN_METHOD(bind_sol_platform_add_hostname_monitor) {
    ADD_MONITOR(hostname, stringMonitor);
}

NAN_METHOD(bind_sol_platform_del_hostname_monitor) {
    DEL_MONITOR(hostname, stringMonitor);
}

NAN_METHOD(bind_sol_platform_add_timezone_monitor) {
    ADD_MONITOR(timezone, stringMonitor);
}

NAN_METHOD(bind_sol_platform_del_timezone_monitor) {
    DEL_MONITOR(timezone, stringMonitor);
}

static void localeMonitor(void *data,
    enum sol_platform_locale_category category, const char *locale) {
    Nan::HandleScope scope;
    Local<Value> arguments[2] = {
        Nan::New(category),
        Nan::New(locale).ToLocalChecked()
    };
    ((Nan::Callback *)data)->Call(2, arguments);
}

NAN_METHOD(bind_sol_platform_add_locale_monitor) {
    ADD_MONITOR(locale, localeMonitor);
}

NAN_METHOD(bind_sol_platform_del_locale_monitor) {
    DEL_MONITOR(locale, localeMonitor);
}

static void stateMonitor(void *data, enum sol_platform_state state) {
    Nan::HandleScope scope;
    Local<Value> arguments[1] = {Nan::New(state)};
    ((Nan::Callback *)data)->Call(1, arguments);
}

NAN_METHOD(bind_sol_platform_add_state_monitor) {
    ADD_MONITOR(state, stateMonitor);
}

NAN_METHOD(bind_sol_platform_del_state_monitor) {
    DEL_MONITOR(state, stateMonitor);
}

static void systemClockMonitor(void *data, int64_t timestamp) {
    Nan::HandleScope scope;
    Local<Value> arguments[1] = {
        Nan::New<Date>((double)timestamp).ToLocalChecked()
    };
    ((Nan::Callback *)data)->Call(1, arguments);
}

NAN_METHOD(bind_sol_platform_add_system_clock_monitor) {
    ADD_MONITOR(system_clock, systemClockMonitor);
}

NAN_METHOD(bind_sol_platform_del_system_clock_monitor) {
    DEL_MONITOR(system_clock, systemClockMonitor);
}

class ServiceInfo {
public:
    ServiceInfo(Local<Function> _jsCallback, const char *_service):
        callback(new Nan::Callback(_jsCallback)), service(strdup(_service)) {}
    ~ServiceInfo() {
        delete callback;
        free(service);
    }
    Nan::Callback *callback;
    char *service;
};

static void serviceMonitor(void *data, const char *service,
    enum sol_platform_service_state state) {
    Nan::HandleScope scope;
    Local<Value> arguments[2] = {
        Nan::New(service).ToLocalChecked(),
        Nan::New(state)
    };
    ((Nan::Callback *)data)->Call(2, arguments);
}

NAN_METHOD(bind_sol_platform_add_service_monitor) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsFunction);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsString);
    int result;

    if (!hijack_ref()) {
        return;
    }

    ServiceInfo *serviceInfo = new ServiceInfo(
        Local<Function>::Cast(info[0]),
        (const char *)*String::Utf8Value(info[1]));
    if (!serviceInfo) {
        result = -ENOMEM;
        goto error;
    }

    result = sol_platform_add_service_monitor(serviceMonitor,
        serviceInfo->service, serviceInfo->callback);
    if (result) {
        goto free_callback_error;
    }

    info.GetReturnValue().Set(SolPlatformMonitor::New(serviceInfo));
    return;
free_callback_error:
    delete serviceInfo;
error:
    hijack_unref();
    Nan::ThrowError(strerror(-result));
}

NAN_METHOD(bind_sol_platform_del_service_monitor) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    int result;
    ServiceInfo *serviceInfo = (ServiceInfo *)
        SolPlatformMonitor::Resolve(Nan::To<Object>(info[0]).ToLocalChecked());
    if (!(serviceInfo && hijack_unref())) {
        return;
    }
    result = sol_platform_del_service_monitor(serviceMonitor,
        serviceInfo->service, serviceInfo->callback);
    if (result) {
        hijack_ref();
        return Nan::ThrowError(strerror(-result));
    }
    delete serviceInfo;
    info.GetReturnValue().Set(Nan::New(result));
}
