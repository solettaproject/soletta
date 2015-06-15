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
            connect: function() {
                print("IN_BOOLEAN connect()");
            },
            disconnect: function() {
                print("IN_BOOLEAN disconnect()");
            },
            process: function(v) {
                sendPacket("OUT_BOOLEAN", v);
            }
        },
        {
            name:'IN_BYTE',
            type:'byte',
            connect: function() {
                print("IN_BYTE connect()");
            },
            disconnect: function() {
                print("IN_BYTE disconnect()");
            },
            process: function(v) {
                sendPacket("OUT_BYTE", v);
            }
        },
        {
            name:'IN_FLOAT',
            type:'float',
            connect: function() {
                print("IN_FLOAT connect()");
            },
            disconnect: function() {
                print("IN_FLOAT disconnect()");
            },
            process: function(v) {
                sendPacket("OUT_FLOAT", v);
            }
        },
        {
            name:'IN_INT',
            type:'int',
            connect: function() {
                print("IN_INT connect()");
            },
            disconnect: function() {
                print("IN_INT disconnect()");
            },
            process: function(v) {
                sendPacket("OUT_INT", v);
            }
        },
        {
            name:'IN_RGB',
            type:'rgb',
            connect: function() {
                print("IN_RGB connect()");
            },
            disconnect: function() {
                print("IN_RGB disconnect()");
            },
            process: function(v) {
                sendPacket("OUT_RGB", v);
            }
        },
        {
            name:'IN_STRING',
            type:'string',
            connect: function() {
                print("IN_STRING connect()");
            },
            disconnect: function() {
                print("IN_STRING disconnect()");
            },
            process: function(v) {
                sendPacket("OUT_STRING", v);
            }
        }
    ],
    out: [
        {
            name:'OUT_BOOLEAN',
            type:'boolean',
            connect: function() {
                print("OUT_BOOLEAN connect()");
            },
            disconnect: function() {
                print("OUT_BOOLEAN disconnect()");
            }
        },
        {
            name:'OUT_BYTE',
            type:'byte',
            connect: function() {
                print("OUT_BYTE connect()");
            },
            disconnect: function() {
                print("OUT_BYTE disconnect()");
            }
        },
        {
            name:'OUT_FLOAT',
            type:'float',
            connect: function() {
                print("OUT_FLOAT connect()");
            },
            disconnect: function() {
                print("OUT_FLOAT disconnect()");
            }
        },
        {
            name:'OUT_INT',
            type:'int',
            connect: function() {
                print("OUT_INT connect()");
            },
            disconnect: function() {
                print("OUT_INT disconnect()");
            }
        },
        {
            name:'OUT_RGB',
            type:'rgb',
            connect: function() {
                print("OUT_RGB connect()");
            },
            disconnect: function() {
                print("OUT_RGB disconnect()");
            }
        },
        {
            name:'OUT_STRING',
            type:'string',
            connect: function() {
                print("OUT_STRING connect()");
            },
            disconnect: function() {
                print("OUT_STRING disconnect()");
            }
        },
    ],
    open: function() {
        print("open()");
    },
    close: function() {
        print("close()");
    }
};
