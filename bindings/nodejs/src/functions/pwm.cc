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

#include <sol-pwm.h>

#include <v8.h>
#include <node.h>
#include <nan.h>

#include "../common.h"
#include "../structures/js-handle.h"
#include "../structures/sol-js-pwm.h"

using namespace v8;

class SolPwm : public JSHandle<SolPwm> {
public:
    static const char *jsClassName() { return "SolPwm"; }
};

NAN_METHOD(bind_sol_pwm_open) {
    VALIDATE_ARGUMENT_COUNT(info, 3);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsInt32);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsInt32);
    VALIDATE_ARGUMENT_TYPE(info, 2, IsObject);

    int device;
    int channel;
    sol_pwm_config config;
    sol_pwm *pwm = NULL;

    device = info[0]->Int32Value();
    channel = info[1]->Int32Value();

    if (!c_sol_pwm_config(info[2]->ToObject(), &config)) {
        return;
    }

    pwm = sol_pwm_open(device, channel, &config);
    if (pwm) {
        info.GetReturnValue().Set(SolPwm::New(pwm));
    }
}

NAN_METHOD(bind_sol_pwm_open_raw) {
    VALIDATE_ARGUMENT_COUNT(info, 3);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsInt32);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 1, IsInt32);
    VALIDATE_ARGUMENT_TYPE(info, 2, IsObject);

    int device;
    int channel;
    sol_pwm_config config;
    sol_pwm *pwm = NULL;

    device = info[0]->Int32Value();
    channel = info[1]->Int32Value();

    if (!c_sol_pwm_config(info[2]->ToObject(), &config)) {
        return;
    }

    pwm = sol_pwm_open_raw(device, channel, &config);
    if (pwm) {
        info.GetReturnValue().Set(SolPwm::New(pwm));
    }
}

NAN_METHOD(bind_sol_pwm_open_by_label) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE_OR_NULL(info, 0, IsString);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsObject);

    sol_pwm_config config;
    sol_pwm *pwm = NULL;

    if (!c_sol_pwm_config(info[1]->ToObject(), &config)) {
        return;
    }

    pwm = sol_pwm_open_by_label((const char *)*String::Utf8Value(info[0]),
        &config);

    if (pwm) {
        info.GetReturnValue().Set(SolPwm::New(pwm));
    }
}

NAN_METHOD(bind_sol_pwm_close) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);

    Local<Object> jsPwm = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_pwm *pwm = (sol_pwm *)SolPwm::Resolve(jsPwm);
    if (!pwm)
        return;

    sol_pwm_close(pwm);
    Nan::SetInternalFieldPointer(jsPwm, 0, 0);
}

NAN_METHOD(bind_sol_pwm_set_enabled) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsBoolean);

    Local<Object> jsPwm = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_pwm *pwm = (sol_pwm *)SolPwm::Resolve(jsPwm);
    if (!pwm)
        return;

    bool value = info[1]->BooleanValue();
    info.GetReturnValue().Set(Nan::New(sol_pwm_set_enabled(pwm, value)));
}

NAN_METHOD(bind_sol_pwm_set_period) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsUint32);

    Local<Object> jsPwm = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_pwm *pwm = (sol_pwm *)SolPwm::Resolve(jsPwm);
    if (!pwm)
        return;

    uint32_t value = info[1]->Uint32Value();
    info.GetReturnValue().Set(Nan::New(sol_pwm_set_period(pwm, value)));
}

NAN_METHOD(bind_sol_pwm_set_duty_cycle) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsUint32);

    Local<Object> jsPwm = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_pwm *pwm = (sol_pwm *)SolPwm::Resolve(jsPwm);
    if (!pwm)
        return;

    uint32_t value = info[1]->Uint32Value();
    info.GetReturnValue().Set(Nan::New(sol_pwm_set_duty_cycle(pwm, value)));
}


NAN_METHOD(bind_sol_pwm_get_enabled) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);

    Local<Object> jsPwm = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_pwm *pwm = (sol_pwm *)SolPwm::Resolve(jsPwm);
    if (!pwm)
        return;

    info.GetReturnValue().Set(Nan::New(sol_pwm_get_enabled(pwm)));
}

NAN_METHOD(bind_sol_pwm_get_period) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);

    Local<Object> jsPwm = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_pwm *pwm = (sol_pwm *)SolPwm::Resolve(jsPwm);
    if (!pwm)
        return;

    info.GetReturnValue().Set(Nan::New(sol_pwm_get_period(pwm)));
}

NAN_METHOD(bind_sol_pwm_get_duty_cycle) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);

    Local<Object> jsPwm = Nan::To<Object>(info[0]).ToLocalChecked();
    sol_pwm *pwm = (sol_pwm *)SolPwm::Resolve(jsPwm);
    if (!pwm)
        return;

    info.GetReturnValue().Set(Nan::New(sol_pwm_get_duty_cycle(pwm)));
}

NAN_METHOD(bind_sol_pwm_alignment_from_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsString);

    sol_pwm_alignment alignment = sol_pwm_alignment_from_str(
        (const char *)*String::Utf8Value(info[0]));
    info.GetReturnValue().Set(Nan::New(alignment));
}

NAN_METHOD(bind_sol_pwm_alignment_to_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsInt32);

    const char *idString = sol_pwm_alignment_to_str(
        (sol_pwm_alignment)info[0]->Int32Value());

    if (idString) {
        info.GetReturnValue().Set(Nan::New(idString).ToLocalChecked());
    } else {
        info.GetReturnValue().Set(Nan::Null());
    }
}

NAN_METHOD(bind_sol_pwm_polarity_from_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsString);

    sol_pwm_polarity polarity = sol_pwm_polarity_from_str(
        (const char *)*String::Utf8Value(info[0]));
    info.GetReturnValue().Set(Nan::New(polarity));
}

NAN_METHOD(bind_sol_pwm_polarity_to_str) {
    VALIDATE_ARGUMENT_COUNT(info, 1);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsInt32);

    const char *idString = sol_pwm_polarity_to_str(
        (sol_pwm_polarity)info[0]->Int32Value());

    if (idString) {
        info.GetReturnValue().Set(Nan::New(idString).ToLocalChecked());
    } else {
        info.GetReturnValue().Set(Nan::Null());
    }
}
