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

/*
 * This is our custom JS node type in order to be used in the mycounter.fbp sample.
 *
 * Every time we receive a boolean from 'IN' we'll add or subtract from this value
 * depending on boolean value, TRUE will add 1 and FALSE will subtract 1, after we'll
 * send this value to 'OUT' port.
 */

var node = {
    in: [
        {
            name:'IN',
            type:'boolean',
            process: function(v) {
                this.counter += v ? 1 : -1;
                sendPacket("OUT", this.counter);
            }
        }
    ],
    out: [
        { name:'OUT', type:'int' }
    ],
    counter: 0
};
