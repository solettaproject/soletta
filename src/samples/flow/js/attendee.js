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
 * This is our custom JS node type in order to be used in the attendee.fbp sample.
 *
 * This node has three in/out ports representing common line, preferential line
 * and attendee.
 *
 * Every time we receive a number from 'IN_COMMON' or 'IN_PREFERENTIAL' we'll append
 * to the respective vector (representing the line).
 *
 * Every time we receive a boolean from 'IN_ATTENDEE' we'll return the next in line
 * (considering that preferential line has higher priority).
 */

var node = {
    in: [
        {
            name:'IN_COMMON',
            type:'int',
            process: function(v) {
                if (v.val > 0) {
                    sendPacket("OUT_COMMON", v.val);
                    this.common_line.push(v.val);
                }
            }
        },
        {
            name:'IN_PREFERENTIAL',
            type:'int',
            process: function(v) {
                if (v.val > 0) {
                    sendPacket("OUT_PREFERENTIAL", v.val);
                    this.preferential_line.push(v.val);
                }
            }
        },
        {
            name:'IN_ATTENDEE',
            type:'boolean',
            process: function(v) {
                if (this.preferential_line.length > 0) {
                    sendPacket("OUT_ATTENDEE", this.preferential_line[0]);
                    this.preferential_line.shift();
                    return;
                }

                if (this.common_line.length > 0) {
                    sendPacket("OUT_ATTENDEE", this.common_line[0]);
                    this.common_line.shift();
                    return;
                }

                sendPacket("OUT_ATTENDEE", 0);
            }
        }
    ],
    out: [
        { name:'OUT_COMMON', type:'int' },
        { name:'OUT_PREFERENTIAL', type:'int' },
        { name:'OUT_ATTENDEE', type:'int' }
    ],
    common_line: [],
    preferential_line: []
};
