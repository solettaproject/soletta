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

var node = {
    in: [
        {
            name:'IN_BOOLEAN',
            type:'boolean',
            process: function(v) {
                sendPacket("OUT_BOOLEAN", v);
            }
        },
        {
            name:'IN_BYTE',
            type:'byte',
            process: function(v) {
                sendPacket("OUT_BYTE", v);
            }
        },
        {
            name:'IN_FLOAT',
            type:'float',
            process: function(v) {
                sendPacket("OUT_FLOAT", v);
            }
        },
        {
            name:'IN_INT',
            type:'int',
            process: function(v) {
                sendPacket("OUT_INT", v);
            }
        },
        {
            name:'IN_RGB',
            type:'rgb',
            process: function(v) {
                sendPacket("OUT_RGB", v);
            }
        },
        {
            name:'IN_STRING',
            type:'string',
            process: function(v) {
                sendPacket("OUT_STRING", v);
            }
        },
        {
            name:'IN_BLOB',
            type:'blob',
            process: function(v) {
              sendPacket("OUT_BLOB", v);
            }
        },
        {
            name:'IN_LOCATION',
            type:'location',
            process: function(v) {
                sendPacket("OUT_LOCATION", v);
            }
        },
        {
            name:'IN_TIMESTAMP',
            type:'timestamp',
            process: function(v) {
                sendPacket("OUT_TIMESTAMP", v);
            }
        },
        {
            name:'IN_DIRECTION_VECTOR',
            type:'direction-vector',
            process: function(v) {
                sendPacket("OUT_DIRECTION_VECTOR", v);
            }
        },
        {
            name:'IN_JSON_OBJECT',
            type:'json-object',
            process: function(v) {
                sendPacket("OUT_JSON_OBJECT", v);
            }
        },
        {
            name:'IN_JSON_ARRAY',
            type:'json-array',
            process: function(v) {
                sendPacket("OUT_JSON_ARRAY", v);
            }
        },
        {
            name:'IN_HTTP_RESPONSE',
            type:'http-response',
            process: function(v) {
              sendPacket("OUT_HTTP_RESPONSE", v);
            }
        }
    ],
    out: [
        {
            name:'OUT_BOOLEAN',
            type:'boolean'
        },
        {
            name:'OUT_BYTE',
            type:'byte'
        },
        {
            name:'OUT_FLOAT',
            type:'float'
        },
        {
            name:'OUT_INT',
            type:'int'
        },
        {
            name:'OUT_RGB',
            type:'rgb'
        },
        {
            name:'OUT_STRING',
            type:'string'
        },
        {
            name:'OUT_BLOB',
            type:'blob'
        },
        {
            name:'OUT_LOCATION',
            type:'location'
        },
        {
            name:'OUT_TIMESTAMP',
            type:'timestamp'
        },
        {
            name:'OUT_DIRECTION_VECTOR',
            type:'direction-vector'
        },
        {
            name:'OUT_JSON_OBJECT',
            type:'json-object'
        },
        {
            name:'OUT_JSON_ARRAY',
            type:'json-array'
        },
        {
            name:'OUT_HTTP_RESPONSE',
            type:'http-response'
        }
    ]
};
