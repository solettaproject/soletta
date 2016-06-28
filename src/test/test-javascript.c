/*
 * This file is part of the Soletta (TM) Project
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

#include <errno.h>

#include "sol-flow.h"
#include "sol-flow-parser.h"
#include "sol-log.h"
#include "sol-util-internal.h"

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

    for (i = 0; i < sol_util_array_size(tests); i++) {
        struct sol_flow_parser *parser;
        struct sol_flow_node_type *type;
        entry = &tests[i];

        parser = sol_flow_parser_new(NULL, NULL);
        type = sol_flow_parse_string_metatype(parser, "js", entry->input, "buffer");
        if (type && entry->should_fail) {
            SOL_ERR("Node was created but should fail, input='%s'", entry->input);
            FAIL();
        } else if (!type && !entry->should_fail) {
            SOL_ERR("Node was not created, input='%s'", entry->input);
            FAIL();
        }
        sol_flow_parser_del(parser);
    }
}


TEST_MAIN();
