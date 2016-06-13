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
 * This is our telegram JS node type in order to be used in the telegram.fbp sample.
 *
 * This node has the 'IN' port, that will be receiving the 'words' and appending
 * them to the buffer, 'LENGTH' that is used to set the maximum length, in other
 * words, when the telegram should be sent and the 'OUT' port that sends the telegram.
 */

var node = {
    in: [
        {
            name:'IN',
            type:'string',
            process: function(v) {
                if (v.length + this.buffer.length > this.length) {
                    sendPacket("OUT", this.buffer);
                    this.buffer = '';
                }
                this.buffer += v + ' ';
            }
        },
        {
            name:'LENGTH',
            type:'int',
            process: function(v) {
                this.length = v.val;
            }
        }
    ],
    out: [
        { name:'OUT', type:'string' }
    ],
    buffer: '',
    length: 0
};
