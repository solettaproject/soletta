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

#include <sol-oic-client.h>

#include "../common.h"
#include "../structures/network.h"
#include "../structures/oic-client.h"
#include "../structures/oic-map.h"

using namespace v8;

class OicClientRequest: public OicCallbackData {
protected:
    OicClientRequest(bool _isOneShot = true): OicCallbackData(),
        isOneShot(_isOneShot) {}
public:
    static OicClientRequest *New(Local<Object> jsClient,
        Local<Function> jsCallback) {
        OicClientRequest *request = new OicClientRequest;
        if (!request) {
            Nan::ThrowError("Failed to allocate OicClientRequest");
        } else if (!request->init(jsClient, jsCallback)) {
            delete request;
            request = 0;
        }
        return request;
    }
    bool isOneShot;
};

static void requestAnswered(sol_coap_responsecode_t code,
    struct sol_oic_client *client,
    const struct sol_network_link_addr *address,
    const struct sol_oic_map_reader *response, void *data) {
    Nan::HandleScope scope;
    OicClientRequest *request = (OicClientRequest *)data;
    Local<Value> arguments[4] = {
        Nan::New(code),
        Nan::New<Object>(*(request->jsClient)),
        js_sol_network_link_addr(address),
        js_sol_oic_map_reader(response)
    };

    // We need to copy the value of isOneShot because in the case of
    // observation, the JS callback we invoke may cause the deletion of request
    // if it ends up calling the unobserve() binding. In such a case we can no
    // longer dereference request after having called the callback.
    bool isOneShot = request->isOneShot;

    request->callback->Call(4, arguments);

    if (isOneShot) {
        delete request;
    }
}

static bool request_setup(Local<Object> jsClient, Local<Object> jsResource,
    struct sol_oic_client **client, struct sol_oic_resource **resource) {
    *client = (struct sol_oic_client *)SolOicClient::Resolve(jsClient);
    *resource =
        (struct sol_oic_resource *)SolOicClientResource::Resolve(jsResource);
    if (!((*client) && (*resource))) {
        return false;
    }
    return true;
}

static Local<Value> do_request(
    struct sol_oic_request *(*create_request_cAPI)(
        sol_coap_method_t method,
        struct sol_oic_resource *res),
    Local<Object> jsClient, Local<Object> jsResource, sol_coap_method_t method,
    Local<Value> jsPayload, Local<Function> jsCallback) {
    Nan::Persistent<Object> *persistentPayload = 0;
    struct sol_oic_client *client = 0;
    struct sol_oic_resource *resource = 0;
    struct sol_oic_request *oic_request;
    bool result = false;

    if (!request_setup(jsClient, jsResource, &client, &resource)) {
        return Nan::Undefined();
    }

    OicClientRequest *request = OicClientRequest::New(jsClient, jsCallback);
    if (!request) {
        return Nan::Undefined();
    }

    if (!jsPayload->IsNull()) {
        persistentPayload =
            new Nan::Persistent<Object>(
                Nan::To<Object>(jsPayload).ToLocalChecked());
        if (!persistentPayload) {
            delete request;
            return Nan::Undefined();
        }
    }

    oic_request = create_request_cAPI(method, resource);
    if (oic_request &&
        oic_map_writer_callback(persistentPayload,
        sol_oic_client_request_get_writer(oic_request))) {
        result = sol_oic_client_request(client, oic_request,
            requestAnswered, request) == 0;
    }

    if (persistentPayload) {
        persistentPayload->Reset();
        delete persistentPayload;
    }

    if (!result) {
        delete request;
    }

    return Nan::New(result);
}

#define DO_REQUEST(info, cAPI) \
    do { \
        VALIDATE_ARGUMENT_COUNT(info, 5); \
        VALIDATE_ARGUMENT_TYPE(info, 0, IsObject); \
        VALIDATE_ARGUMENT_TYPE(info, 1, IsObject); \
        VALIDATE_ARGUMENT_TYPE(info, 2, IsUint32); \
        VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 3, IsObject); \
        VALIDATE_ARGUMENT_TYPE(info, 4, IsFunction); \
        info.GetReturnValue().Set(do_request((cAPI), \
            Nan::To<Object>(info[0]).ToLocalChecked(), \
            Nan::To<Object>(info[1]).ToLocalChecked(), \
            (sol_coap_method_t)Nan::To<uint32_t>(info[2]).FromJust(), \
            info[3], Local<Function>::Cast(info[4]))); \
    } while(0)

NAN_METHOD(bind_sol_oic_client_request) {
    DO_REQUEST(info, sol_oic_client_request_new);
}

NAN_METHOD(bind_sol_oic_client_resource_non_confirmable_request) {
    DO_REQUEST(info, sol_oic_client_non_confirmable_request_new);
}

typedef int (*ObserveAPI)(
    struct sol_oic_client *client,
    struct sol_oic_resource *res,
    void(*callback)(
        sol_coap_responsecode_t response_code,
        struct sol_oic_client *cli,
        const struct sol_network_link_addr *addr,
        const struct sol_oic_map_reader *repr_map,
        void *data),
    const void *data,
    bool observe);

class OicClientObservation : public OicClientRequest {
protected:
    OicClientObservation(ObserveAPI _cAPI): OicClientRequest(false),
        cAPI(_cAPI) {}
public:
    bool init(Local<Object> jsClient, Local<Object> _jsResource,
        Local<Function> jsCallback) {
        jsResource =
            new Nan::Persistent<Object>(_jsResource);
        if (!jsResource) {
            Nan::ThrowError(
                "OicClientObservation: Failed to allocate resource");
            return false;
        }
        if (!OicCallbackData::init(jsClient, jsCallback)) {
            jsResource->Reset();
            delete jsResource;
            return false;
        }
        return true;
    }
    static OicClientObservation *New(Local<Object> jsClient,
        Local<Object> jsResource, Local<Function> jsCallback,
        ObserveAPI cAPI) {
        OicClientObservation *observation = new OicClientObservation(cAPI);
        if (!observation) {
            Nan::ThrowError("Failed to allocate OicClientObservation");
        } else if (!observation->init(jsClient, jsResource, jsCallback)) {
            delete observation;
            observation = 0;
        }
        return observation;
    }
    virtual ~OicClientObservation() {
        if (jsResource) {
            jsResource->Reset();
            delete jsResource;
        }
    }
    Nan::Persistent<Object> *jsResource;
    ObserveAPI cAPI;
};

class SolOicObservation : public JSHandle<SolOicObservation> {
public:
    static const char *jsClassName() { return "SolOicObservation"; }
};

Local<Value> do_observe(ObserveAPI cAPI, Local<Object> jsClient,
    Local<Object> jsResource, Local<Function> jsCallback) {

    struct sol_oic_client *client = 0;
    struct sol_oic_resource *resource = 0;
    if (!request_setup(jsClient, jsResource, &client, &resource)) {
        return Nan::Undefined();
    }

    OicClientObservation *observation =
        OicClientObservation::New(jsClient, jsResource, jsCallback, cAPI);
    if (!observation) {
        return Nan::Undefined();
    }

    if (cAPI(client, resource, requestAnswered, observation, true) < 0) {
        delete observation;
        return Nan::Undefined();
    }

    return SolOicObservation::New(observation);
}

#define DO_OBSERVE(cAPI) \
    do { \
        VALIDATE_ARGUMENT_COUNT(info, 3); \
        VALIDATE_ARGUMENT_TYPE(info, 0, IsObject); \
        VALIDATE_ARGUMENT_TYPE(info, 1, IsObject); \
        VALIDATE_ARGUMENT_TYPE(info, 2, IsFunction); \
        info.GetReturnValue().Set(do_observe((cAPI),\
            Nan::To<Object>(info[0]).ToLocalChecked(), \
            Nan::To<Object>(info[1]).ToLocalChecked(), \
            Local<Function>::Cast(info[2]))); \
    } while(0)

NAN_METHOD(bind_sol_oic_client_resource_observe) {
    DO_OBSERVE(sol_oic_client_resource_set_observable);
}

NAN_METHOD(bind_sol_oic_client_resource_observe_non_confirmable) {
    DO_OBSERVE(sol_oic_client_resource_set_observable_non_confirmable);
}

NAN_METHOD(bind_sol_oic_client_resource_unobserve) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);

    Local<Object> jsObservation = Nan::To<Object>(info[0]).ToLocalChecked();
    OicClientObservation *observation = (OicClientObservation *)
        SolOicObservation::Resolve(jsObservation);
    if (!observation) {
        return;
    }

    struct sol_oic_client *client = 0;
    struct sol_oic_resource *resource = 0;
    if (!request_setup(Nan::New(*(observation->jsClient)),
        Nan::New(*(observation->jsResource)), &client, &resource)) {
        return;
    }

    bool result = (observation->cAPI(client, resource, requestAnswered,
        observation, false) == 0);
    if (result) {
        Nan::SetInternalFieldPointer(jsObservation, 0, 0);
        delete observation;
    }

    info.GetReturnValue().Set(Nan::New(result));
}
