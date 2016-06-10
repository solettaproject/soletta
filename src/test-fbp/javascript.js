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
        },
        {
            name:'IN_COMPOSED',
            type:'composed:string,int',
            process: function(v) {
              sendPacket("OUT_COMPOSED", v);
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
        },
        {
            name:'OUT_COMPOSED',
            type:'composed:string,int'
        }
    ]
};
