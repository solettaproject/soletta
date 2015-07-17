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

#include <errno.h>

#include "sol-flow.h"
#include "sol-flow-js.h"
#include "sol-log.h"
#include "sol-util.h"

#include "test.h"

#define JS_ASSERT_TRUE(_buf) {                                               \
        struct sol_flow_node_type *t = sol_flow_js_new_type(_buf, strlen(_buf)); \
        if (!t) {                                                                \
            SOL_WRN("Failed to parse '%s'.", _buf);                              \
            ASSERT(false);                                                       \
        }                                                                        \
        sol_flow_node_type_del(t); }

#define JS_ASSERT_FALSE(_buf) {                                              \
        struct sol_flow_node_type *t = sol_flow_js_new_type(_buf, strlen(_buf)); \
        if (t) {                                                                 \
            SOL_WRN("Parse should not be successful '%s'.", _buf);               \
            ASSERT(false);                                                       \
        }                                                                        \
        sol_flow_node_type_del(t); }

DEFINE_TEST(test_js);

static void
test_js(void)
{
    JS_ASSERT_FALSE("");

    /* variables and methods */
    JS_ASSERT_FALSE("var ports = {};");
    JS_ASSERT_FALSE("var foo = 123; var my_ports = {};");
    JS_ASSERT_FALSE("function in_port() { print('hello!'); }");
    JS_ASSERT_TRUE("var node = {};");
    JS_ASSERT_TRUE("var foo = 123; var node = {}; var bar = 'bar';");
    JS_ASSERT_TRUE("function bar() { print('hello!'); } var node = {};");

    /* in/out ports */
    JS_ASSERT_TRUE("var node = { in: [{ name: 'IN_PORT', type:'int' }, { name: 'IN_PORT2', type: 'string'}]};");
    JS_ASSERT_TRUE("var node = { out: [{ name: 'OUT_PORT', type:'float' }, { name: 'OUT_PORT2', type: 'byte'}]};");
    JS_ASSERT_TRUE("var node = { in: [{ name: 'IN_PORT', type:'string' }], out: [{ name: 'OUT_PORT', type: 'int'}]};");

    /* methods */
    JS_ASSERT_TRUE("var node = { in: [{ name: 'IN', type: 'rgb', process: function() { print('process'); }} ]};");
    JS_ASSERT_TRUE("var node = { out: [{ name: 'OUT', type: 'string', connect: function() { print('connect'); }} ]};");

    /* properties on node variable */
    JS_ASSERT_TRUE("var node = { in: [{ name: 'IN', type: 'rgb', process: function() { print('process'); }} ], property_1:123 };");
}


TEST_MAIN();
