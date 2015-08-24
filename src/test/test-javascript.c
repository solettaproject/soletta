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
#include "sol-flow-parser.h"
#include "sol-log.h"
#include "sol-util.h"

#include "test.h"

DEFINE_TEST(test_js);

static void
test_js(void)
{
    struct test_entry {
        const char *input;
        bool should_fail;
    } *entry, tests[] = {
        { NULL, .should_fail = true },
        { "", .should_fail = true },

        /* Variables and methods. */
        { "var ports = {};", .should_fail = true },
        { "var foo = 123; var my_ports = {};", .should_fail = true },
        { "function in_port() { print('hello!'); }", .should_fail = true },

        { "var node = {};" },
        { "var foo = 123; var node = {}; var bar = 'bar';" },
        { "function bar() { print('hello!'); } var node = {};" },

        /* In/Out ports. */
        { "var node = { in: [{ name: 'IN_PORT', type:'int' }, { name: 'IN_PORT2', type: 'string'}]};" },
        { "var node = { out: [{ name: 'OUT_PORT', type:'float' }, { name: 'OUT_PORT2', type: 'byte'}]};" },
        { "var node = { in: [{ name: 'IN_PORT', type:'string' }], out: [{ name: 'OUT_PORT', type: 'int'}]};" },

        /* Methods. */
        { "var node = { in: [{ name: 'IN', type: 'rgb', process: function() { print('process'); }} ]};" },
        { "var node = { out: [{ name: 'OUT', type: 'string', connect: function() { print('connect'); }} ]};" },

        /* Properties on node variable. */
        { "var node = { in: [{ name: 'IN', type: 'rgb', process: function() { print('process'); }} ], property_1:123 };" },
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(tests); i++) {
        struct sol_flow_parser *parser;
        struct sol_flow_node_type *type;
        entry = &tests[i];

        parser = sol_flow_parser_new(NULL, NULL);
        type = sol_flow_parse_string_metatype(parser, "js", entry->input, "buffer");
        if (type && entry->should_fail) {
            SOL_ERR("Node was created but should fail, input='%s'", entry->input);
            FAIL();
        } else if (!type && !entry->should_fail) {
            SOL_ERR("Node was created but should fail, input='%s'", entry->input);
            FAIL();
        }
        sol_flow_parser_del(parser);
    }
}


TEST_MAIN();
