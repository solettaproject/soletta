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

#include <map>
#include <string.h>
#include <nan.h>
#include <sol-oic-server.h>

#include "../common.h"
#include "../hijack.h"
#include "../structures/js-handle.h"
#include "../structures/network.h"
#include "../structures/oic-map.h"

using namespace v8;

class SolOicServerResource : public JSHandle<SolOicServerResource> {
public:
    static const char *jsClassName() { return "SolOicServerResource"; }
};

enum OicServerMethod {
    OIC_SERVER_GET = 0,
    OIC_SERVER_PUT = 1,
    OIC_SERVER_POST = 2,
    OIC_SERVER_DEL = 3,
    OIC_SERVER_METHOD_COUNT = 4
};
static const char *keys[4] = { "get", "put", "post", "del" };

class ResourceInfo {
    ResourceInfo(): hijackRefWasSuccessful(false), resource(0) {
        handlers[0] = handlers[1] = handlers[2] = handlers[3] = 0;
    }
    bool hijackRefWasSuccessful;
public:
    bool init(Local<Object> definition) {
        for (int index = 0; index < OIC_SERVER_METHOD_COUNT; index++) {
            Local<Value> value =
                Nan::Get(definition, Nan::New(keys[index]).ToLocalChecked())
                    .ToLocalChecked();
            if (value->IsFunction()) {
                handlers[index] =
                    new Nan::Callback(Local<Function>::Cast(value));
                if (!handlers[index]) {
                    return false;
                }
            }
        }
        hijackRefWasSuccessful = hijack_ref();
        return hijackRefWasSuccessful;
    }
    virtual ~ResourceInfo() {
        for (int index = 0; index < OIC_SERVER_METHOD_COUNT; index++) {
            delete handlers[index];
        }
        if (hijackRefWasSuccessful) {
            hijack_unref();
        }
    }
    static ResourceInfo *New(Local<Object> definition) {
        ResourceInfo *info = new ResourceInfo;
        if (!info->init(definition)) {
            Nan::ThrowError("Failed to allocate ResourceInfo");
            delete info;
            info = 0;
        }
        return info;
    }
    struct sol_oic_server_resource *resource;
    Nan::Callback *handlers[4];
};

#define ENTITY_HANDLER_SIGNATURE \
    void *data, \
    struct sol_oic_request *request

static int entityHandler(ENTITY_HANDLER_SIGNATURE,
    enum OicServerMethod method) {
    Nan::HandleScope scope;
    enum sol_coap_response_code returnValue = SOL_COAP_RESPONSE_CODE_NOT_IMPLEMENTED;
    struct ResourceInfo *info = (struct ResourceInfo *)data;
    struct sol_oic_response *response = NULL;
    Local<Object> outputPayload = Nan::New<Object>();
    Local<Value> arguments[2] = {
        js_sol_oic_map_reader(sol_oic_server_request_get_reader(request)),
        outputPayload
    };
    //TODO: Make JS API Async
    Nan::Callback *callback = info->handlers[method];
    if (callback) {
        Local<Value> jsReturnValue = callback->Call(2, arguments);
        VALIDATE_CALLBACK_RETURN_VALUE_TYPE(jsReturnValue, IsUint32,
            "entity handler", returnValue);
        returnValue = (enum sol_coap_response_code)
            Nan::To<uint32_t>(jsReturnValue).FromJust();

        response = sol_oic_server_response_new(request);
        if (!response) {
            Nan::ThrowError("entity handler: Failed to create response");
        }
        if (!c_sol_oic_map_writer(outputPayload,
            sol_oic_server_response_get_writer(response))) {
            sol_oic_server_response_free(response);
            Nan::ThrowError("entity handler: Failed to encode output payload");
        }

    }
    return sol_oic_server_send_response(request, response, returnValue);
}

static int defaultGet(ENTITY_HANDLER_SIGNATURE) {
    return entityHandler(data, request, OIC_SERVER_GET);
}

static int defaultPut(ENTITY_HANDLER_SIGNATURE) {
    return entityHandler(data, request, OIC_SERVER_PUT);
}

static int defaultPost(ENTITY_HANDLER_SIGNATURE) {
    return entityHandler(data, request, OIC_SERVER_POST);
}

static int defaultDel(ENTITY_HANDLER_SIGNATURE) {
    return entityHandler(data, request, OIC_SERVER_DEL);
}

#define ASSIGN_STR_SLICE_MEMBER_FROM_PROPERTY(to, from, message, member) \
    do { \
        to.member.data = strdup((const char *)*String::Utf8Value( \
            Nan::Get(from, Nan::New(#member).ToLocalChecked()) \
                .ToLocalChecked())); \
        if (!to.member.data) { \
            message = "Failed to allocate memory for " #member; \
            goto member##_failed; \
        } \
        to.member.len = strlen(to.member.data); \
    } while(0)

static bool c_sol_oic_resource_type(Local<Object> js,
    struct sol_oic_resource_type *definition) {
    const char *error = 0;
    struct sol_oic_resource_type local = {
		SOL_SET_API_VERSION(.api_version = SOL_OIC_RESOURCE_TYPE_API_VERSION,)
        .resource_type = {0, 0},
        .interface = {0, 0},
        .path = {0, 0},
        .get = { .handle = defaultGet },
        .put = { .handle = defaultPut },
        .post = { .handle = defaultPost },
        .del = { .handle = defaultDel }
    };

    ASSIGN_STR_SLICE_MEMBER_FROM_PROPERTY(local, js, error, resource_type);
    ASSIGN_STR_SLICE_MEMBER_FROM_PROPERTY(local, js, error, interface);
    ASSIGN_STR_SLICE_MEMBER_FROM_PROPERTY(local, js, error, path);

    *definition = local;
    return true;

path_failed:
    free((void *)(local.interface.data));
interface_failed:
    free((void *)(local.resource_type.data));
resource_type_failed:
    Nan::ThrowError(error);
    return false;
}

NAN_METHOD(bind_sol_oic_server_register_resource) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsUint32);

    struct sol_oic_server_resource *resource = 0;

    struct sol_oic_resource_type resourceType;
    if (!c_sol_oic_resource_type(Nan::To<Object>(info[0]).ToLocalChecked(),
        &resourceType)) {
        return;
    }

    struct ResourceInfo *resourceInfo =
        ResourceInfo::New(Nan::To<Object>(info[0]).ToLocalChecked());
    if (!resourceInfo) {
        return;
    }

    resource = sol_oic_server_register_resource(&resourceType, resourceInfo,
        (enum sol_oic_resource_flag)(Nan::To<uint32_t>(info[1]).FromJust()));
    resourceInfo->resource = resource;

    free((void *)(resourceType.resource_type.data));
    free((void *)(resourceType.interface.data));
    free((void *)(resourceType.path.data));

    if (!resource) {
        delete resourceInfo;
    } else {
        info.GetReturnValue().Set(SolOicServerResource::New(resourceInfo));
    }
}

NAN_METHOD(bind_sol_oic_server_unregister_resource) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    Local<Object> jsResourceInfo = Nan::To<Object>(info[0]).ToLocalChecked();
    struct ResourceInfo *resourceInfo = (struct ResourceInfo *)
        SolOicServerResource::Resolve(jsResourceInfo);
    if (!resourceInfo) {
        return;
    }
    sol_oic_server_unregister_resource(resourceInfo->resource);
    delete resourceInfo;
    Nan::SetInternalFieldPointer(jsResourceInfo, 0, 0);
}

NAN_METHOD(bind_sol_oic_server_send_notification_to_observers) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsObject);
    struct sol_oic_response *notification;
    bool result = true;
    struct ResourceInfo *resourceInfo = (struct ResourceInfo *)
        SolOicServerResource::Resolve(
            Nan::To<Object>(info[0]).ToLocalChecked());
    if (!resourceInfo) {
        return;
    }

    notification = sol_oic_server_notification_new(resourceInfo->resource);
    if (!notification) {
        info.GetReturnValue().Set(Nan::New(false));
        return;
    }

    Nan::Persistent<Object> *jsPayload = 0;
    if (!info[1]->IsNull()) {
        jsPayload = new Nan::Persistent<Object>(
            Nan::To<Object>(info[1]).ToLocalChecked());
    }

    if (jsPayload) {
        result = oic_map_writer_callback(jsPayload, sol_oic_server_response_get_writer(notification));
    }

    if (result)
        result = sol_oic_server_send_notification_to_observers(notification) == 0;
    else
        sol_oic_server_response_free(notification);
    info.GetReturnValue().Set(Nan::New(result));

    if (jsPayload) {
        jsPayload->Reset();
        delete jsPayload;
    }
}
