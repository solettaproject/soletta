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

#include <sol-oic-client.h>

#include "device-id.h"
#include "network.h"
#include "oic-client.h"
#include "../hijack.h"

const char *SolOicClientResource::jsClassName() {
    return "SolOicClientResource";
}

void SolOicClientResource::ref(void *data) {
    sol_oic_resource_ref((struct sol_oic_resource *)data);
}

void SolOicClientResource::unref(void *data) {
    sol_oic_resource_unref((struct sol_oic_resource *)data);
}

using namespace v8;

static Local<Array> jsStringArrayFromStrSliceVector(
    struct sol_vector *vector) {
    Local<Array> jsArray = Nan::New<Array>(vector->len);
    sol_str_slice *slice;
    int index;
    SOL_VECTOR_FOREACH_IDX(vector, slice, index) {
        jsArray->Set(index,
            Nan::New<String>(slice->data, slice->len).ToLocalChecked());
    }
    return jsArray;
}

Local<Object> SolOicClientResource::New(struct sol_oic_resource *resource) {
    Local<Object> jsResource =
        JSReffableHandle<SolOicClientResource>::New((void *)resource);

    Nan::Set(jsResource, Nan::New("addr").ToLocalChecked(),
        js_sol_network_link_addr(&(resource->addr)));
    Nan::Set(jsResource, Nan::New("device_id").ToLocalChecked(),
        js_DeviceIdFromSlice(&(resource->device_id)));
    Nan::Set(jsResource, Nan::New("href").ToLocalChecked(),
        Nan::New<String>(resource->href.data,
            resource->href.len).ToLocalChecked());
    Nan::Set(jsResource, Nan::New("interfaces").ToLocalChecked(),
        jsStringArrayFromStrSliceVector(&(resource->interfaces)));
    Nan::Set(jsResource, Nan::New("is_observing").ToLocalChecked(),
        Nan::New(resource->is_observing));
    Nan::Set(jsResource, Nan::New("observable").ToLocalChecked(),
        Nan::New(resource->observable));
    Nan::Set(jsResource, Nan::New("secure").ToLocalChecked(),
        Nan::New(resource->secure));
    Nan::Set(jsResource, Nan::New("types").ToLocalChecked(),
        jsStringArrayFromStrSliceVector(&(resource->types)));

    return jsResource;
}

const char *SolOicClient::jsClassName() {
    return "SolOicClient";
}

void SolOicClient::ref(void *data) {}

void SolOicClient::unref(void *data) {
    sol_oic_client_del((sol_oic_client *)data);
}

OicCallbackData::OicCallbackData(): jsClient(0), callback(0),
    hijackRefWasSuccessful(false) {}
OicCallbackData::~OicCallbackData() {
    if (jsClient) {
        jsClient->Reset();
        delete jsClient;
    }
    delete callback;
    if (hijackRefWasSuccessful) {
        hijack_unref();
    }
}

bool OicCallbackData::init(Local<Object> _jsClient,
    Local<Function> jsCallback) {
    callback = new Nan::Callback(jsCallback);
    if (!callback) {
        Nan::ThrowError("OicCallbackData: Failed to allocate callback");
        return false;
    }
    jsClient = new Nan::Persistent<Object>(_jsClient);
    if (!jsClient) {
        delete callback;
        Nan::ThrowError("OicCallbackData: Failed to allocate client");
        return false;
    }

    hijackRefWasSuccessful = hijack_ref();
    return hijackRefWasSuccessful;
}

OicCallbackData *OicCallbackData::New(Local<Object> jsClient,
    Local<Function> jsCallback) {
    OicCallbackData *data = new OicCallbackData;
    if (!data) {
        Nan::ThrowError("Failed to allocate OicCallbackData");
    } else if (!data->init(jsClient, jsCallback)) {
        delete data;
        data = 0;
    }
    return data;
}
