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
        Nan::Callback *callback = \
            (Nan::Callback *)SolPlatformMonitor::Resolve( \
                Nan::To<Object>(info[0]).ToLocalChecked()); \
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
        Nan::Callback *callback = \
            (Nan::Callback *)SolPlatformMonitor::Resolve( \
                Nan::To<Object>(info[0]).ToLocalChecked()); \
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
        info.GetReturnValue().Set(Nan::New(result)); \
    } while(0)

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
