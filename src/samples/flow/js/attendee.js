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

/*
 * This is our custom JS node type in order to be used in the attendee.fbp sample.
 *
 * This node has three in/out ports representing common line, preferential line
 * and attendee.
 *
 * Everytime we receive a number from 'IN_COMMON' or 'IN_PREFERENTIAL' we'll append
 * to the respective vector (representing the line).
 *
 * Everytime we receive a boolean from 'IN_ATTENDEE' we'll return the next in line
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
