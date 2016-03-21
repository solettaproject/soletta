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

#include <sol-network.h>
#include <nan.h>

#include "../common.h"
#include "../structures/network.h"

using namespace v8;

NAN_METHOD(bind_sol_network_link_addr_from_str) {
    VALIDATE_ARGUMENT_COUNT(info, 2);
    VALIDATE_ARGUMENT_TYPE(info, 0, IsObject);
    VALIDATE_ARGUMENT_TYPE(info, 1, IsString);

    struct sol_network_link_addr local;
    if (!c_sol_network_link_addr(Nan::To<Object>(info[0]).ToLocalChecked(),
        &local)) {
        return;
    }
    const struct sol_network_link_addr *result =
        sol_network_link_addr_from_str(&local,
            (const char *)*String::Utf8Value(info[1]));
    if (result) {
        info.GetReturnValue().Set(js_sol_network_link_addr(result));
    } else {
        info.GetReturnValue().Set(Nan::Null());
    }
}
