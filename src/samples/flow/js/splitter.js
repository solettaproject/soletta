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

/*
 * This is our splitter JS node type in order to be used in the telegram.fbp sample.
 *
 * This node has the 'SEPARATOR' port, that will receive the separator string and
 * the 'IN' port, that will receive a string to be splitted using the separator
 * and send a string packet for each entry of this separation.
 */

var node = {
    in: [
        {
            name:'IN',
            type:'string',
            process: function(v) {
                var words = v.split(this.separator);
                for (var i in words)
                    sendPacket("OUT", words[i]);
            }
        },
        {
            name:'SEPARATOR',
            type:'string',
            process: function(v) {
                this.separator = v;
            }
        }
    ],
    out: [
        { name:'OUT', type:'string' }
    ],
    separator: ' '
};
