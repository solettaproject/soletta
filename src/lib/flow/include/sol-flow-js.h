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

#pragma once

#include "sol-flow.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * JS node allows the usage of Javascript language to create new and
 * customizable node types.
 *
 * A JS node type is specified with one object containing each
 * input and output port declarations (name and type) and its
 * callback functions that will be trigged on the occurrence
 * of certain events like input/output ports processes, open/close
 * processes, so forth and so on.
 */

/**
 * Creates a new "JS node" type.
 *
 * The Javascript code must contain an object:
 *
 *     - 'node': This object will be used to declare input and output ports
 *               and its callback functions that will be trigged on the occurence
 *               of certain events like input/output ports processes, open/close
 *               processes, so forth and so on.
 *
 * e.g.  var node = {
 *           in: [
 *               {
 *                   name: 'IN',
 *                   type: 'int',
 *                   process: function(v) {
 *                       sendPacket("OUT", 42);
 *                   }
 *               }
 *           ],
 *           out: [ { name: 'OUT', type: 'int' } ]
 *       };
 *
 * @param buf A buffer containing the Javascript code in which will be used
 *            in this new JS node type.
 * @param len The size of the buffer.
 *
 * @return A new JS node type on success, otherwise @c NULL.
 */
struct sol_flow_node_type *sol_flow_js_new_type(const char *buf, size_t len);

#ifdef __cplusplus
}
#endif
