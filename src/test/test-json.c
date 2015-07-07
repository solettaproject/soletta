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

#include "test.h"
#include "sol-json.h"
#include "sol-util.h"
#include "sol-log.h"

#define TOKENS (const enum sol_json_type[])
struct test_entry {
    const char *input;
    const enum sol_json_type *output;
    int expected_elements;
};


static struct test_entry scan_tests[] = {
    {
        "{}",
        TOKENS {
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_OBJECT_END
        },
        .expected_elements = 2
    },
    {
        "{ \"string\" : \"this is a string\"}",
        TOKENS {
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_OBJECT_END
        },
        .expected_elements = 5
    },
    {
        "{ \"number\" : 12345 }",
        TOKENS {
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_NUMBER,
            SOL_JSON_TYPE_OBJECT_END
        },
        .expected_elements = 5
    },
    {
        "{"
        "   \"menu\": {"
        "       \"id\": \"file\","
        "       \"value\": \"File\","
        "       \"popup\": {"
        "           \"menuitem\": ["
        "               {\"value\": \"New\"},"
        "               {\"value\": \"Open\"},"
        "               {\"value\": \"Close\"}"
        "           ]"
        "       }"
        "   }"
        "}",
        TOKENS {
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_ELEMENT_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_ELEMENT_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_ARRAY_START,
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_OBJECT_END,
            SOL_JSON_TYPE_ELEMENT_SEP,
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_OBJECT_END,
            SOL_JSON_TYPE_ELEMENT_SEP,
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_OBJECT_END,
            SOL_JSON_TYPE_ARRAY_END,
            SOL_JSON_TYPE_OBJECT_END,
            SOL_JSON_TYPE_OBJECT_END,
            SOL_JSON_TYPE_OBJECT_END
        },
        .expected_elements = 39
    }
};

DEFINE_TEST(test_json);

static void
test_json(void)
{
    unsigned int i = 0;
    int j = 0;

    for (i = 0; i < ARRAY_SIZE(scan_tests); i++) {
        struct test_entry *t;
        struct sol_json_scanner scanner;
        struct sol_json_token input;
        const enum sol_json_type *output;

        t = &scan_tests[i];
        input.start = t->input;
        input.end = t->input + strlen(t->input);
        sol_json_scanner_init(&scanner, input.start, input.end - input.start);

        output = scan_tests[i].output;

        for (j = 0; j < scan_tests[i].expected_elements; j++) {
            if (!sol_json_scanner_next(&scanner, &input)) {
                SOL_WRN("Error: Unexpected end of file.");
                ASSERT(false);
            }
            if (sol_json_token_get_type(&input) != output[j]) {
                SOL_WRN("Token: %c , Expected: %c \n", sol_json_token_get_type(&input), output[j]);
                ASSERT(false);
            }
        }
    }
}

TEST_MAIN();
