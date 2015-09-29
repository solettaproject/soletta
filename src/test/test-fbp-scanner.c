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

#include <stdbool.h>

#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-str-slice.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "sol-fbp-internal-scanner.h"

#include "test.h"

#define TOKENS (const enum sol_fbp_token_type[])

struct test_entry {
    const char *input;
    const enum sol_fbp_token_type *output;
};

static struct test_entry scan_tests[] = {
    {
        "",
        TOKENS {
            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "a OUT -> IN b",
        TOKENS {
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_ARROW,

            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "a OUT -> IN b # comment!",
        TOKENS {
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_ARROW,

            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "  a OUT -> IN b  \n\n\n  c OUT -> IN d   ",
        TOKENS {
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_ARROW,

            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_STMT_SEPARATOR,
            SOL_FBP_TOKEN_STMT_SEPARATOR,
            SOL_FBP_TOKEN_STMT_SEPARATOR,

            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_ARROW,

            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "a_node OUT -> IN node_in_the_middle OUT -> IN another_node",
        TOKENS {
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_ARROW,

            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_ARROW,

            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "MyTimer(Timer) OUT -> IN Led(Super/LED)",
        TOKENS {
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_ARROW,

            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,
            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "MyTimer(Timer:interval=400) OUT -> IN Led(Super/LED:color=blue,brightness=100)",
        TOKENS {
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,

            /* Meta section for MyTimer. */
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_ARROW,

            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,

            /* Meta section for Led. */
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COMMA,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,

            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "INPORT=Read.IN:FILENAME",
        TOKENS {
            SOL_FBP_TOKEN_INPORT_KEYWORD,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_DOT,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "OUTPORT=Counter.OUT:OUT",
        TOKENS {
            SOL_FBP_TOKEN_OUTPORT_KEYWORD,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_DOT,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "OUTPORT=Counter.OUT[0]:OUT",
        TOKENS {
            SOL_FBP_TOKEN_OUTPORT_KEYWORD,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_DOT,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_BRACKET_OPEN,
            SOL_FBP_TOKEN_INTEGER,
            SOL_FBP_TOKEN_BRACKET_CLOSE,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "INPORT=Read.IN:FILENAME, Read(ReadFile) OUT -> IN Display(Output)",
        TOKENS {
            SOL_FBP_TOKEN_INPORT_KEYWORD,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_DOT,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_STMT_SEPARATOR,

            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_ARROW,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,

            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "INPORT=Read.IN:FILENAME\n Read(ReadFile) OUT -> IN Display(Output)",
        TOKENS {
            SOL_FBP_TOKEN_INPORT_KEYWORD,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_DOT,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,

            SOL_FBP_TOKEN_STMT_SEPARATOR,

            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_ARROW,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,

            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "Timer(timer:interval=500) OUT -> IN c1(console:prefix=\"teste=\",flush=true)",
        TOKENS {
            // Timer
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,

            // Connection
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_ARROW,
            SOL_FBP_TOKEN_IDENTIFIER,

            // Console
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_STRING,
            SOL_FBP_TOKEN_COMMA,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,
            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "Timer(timer:interval=500) OUT -> IN c1(console:prefix=\"test with \\\"quotes\\\" \",flush=true)",
        TOKENS {
            // Timer
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,

            // Connection
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_ARROW,
            SOL_FBP_TOKEN_IDENTIFIER,

            // Console
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_STRING,
            SOL_FBP_TOKEN_COMMA,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,
            SOL_FBP_TOKEN_EOF,
        },
    },
    {
        "Timer(timer:interval=500) OUT -> IN c1(console:prefix=\"test \\n \\t \\\" \\\\ \",flush=true)",
        TOKENS {
            // Timer
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,

            // Connection
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_ARROW,
            SOL_FBP_TOKEN_IDENTIFIER,

            // Console
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_STRING,
            SOL_FBP_TOKEN_COMMA,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,
            SOL_FBP_TOKEN_EOF,
        },
    },
    {   /* test for invalid strings */
        "Timer(timer:interval=500) OUT -> IN c1(console:prefix=\"test\"",
        TOKENS {
            // Timer
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_CLOSE,

            // Connection
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_ARROW,
            SOL_FBP_TOKEN_IDENTIFIER,

            // Console
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_PAREN_OPEN,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_STRING,
            SOL_FBP_TOKEN_ERROR,
            SOL_FBP_TOKEN_EOF
        },
    },
    { /* Declare statement with a filename. */
        "DECLARE=MyType:fbp:MyType.fbp",
        TOKENS {
            SOL_FBP_TOKEN_DECLARE_KEYWORD,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EOF,
        },
    },
    { /* Connection with array ports */
        "a OUT[1] -> IN[0] b",
        TOKENS {
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_BRACKET_OPEN,
            SOL_FBP_TOKEN_INTEGER,
            SOL_FBP_TOKEN_BRACKET_CLOSE,
            SOL_FBP_TOKEN_ARROW,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_BRACKET_OPEN,
            SOL_FBP_TOKEN_INTEGER,
            SOL_FBP_TOKEN_BRACKET_CLOSE,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EOF,
        },
    },
    { /* Export options in FBP files */
        "OPTION=Subnode.option:MyOption",
        TOKENS {
            SOL_FBP_TOKEN_OPTION_KEYWORD,
            SOL_FBP_TOKEN_EQUAL,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_DOT,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_COLON,
            SOL_FBP_TOKEN_IDENTIFIER,
            SOL_FBP_TOKEN_EOF,
        },
    },
};

#define TOKEN_NAME(T) #T,

static const char *token_names[] = {
    SOL_FBP_TOKEN_LIST(TOKEN_NAME)
};

#undef TOKEN_NAME

DEFINE_TEST(run_table_tests);

static void
run_table_tests(void)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(scan_tests); i++) {
        struct test_entry *t;
        struct sol_fbp_scanner scanner;
        struct sol_str_slice input;
        const enum sol_fbp_token_type *output;
        int pos = 0;

        t = &scan_tests[i];
        input.len = strlen(t->input);
        input.data = t->input;
        sol_fbp_scanner_init(&scanner, input);

        pos = 0;
        output = scan_tests[i].output;
        while (*output != SOL_FBP_TOKEN_EOF) {
            sol_fbp_scan_token(&scanner);
            if (scanner.token.type != *output) {
                SOL_WRN(
                    "Failed to scan string '%s': "
                    "wrong token at position %d expected '%s' but got '%s'",
                    scan_tests[i].input, pos, token_names[*output],
                    token_names[scanner.token.type]);
                ASSERT(false);
            }
            pos++;
            output++;
        }
    }
}


DEFINE_TEST(scan_errors);

static void
scan_errors(void)
{
    uint16_t i, j;

    static struct sol_str_slice tests[] = {
        SOL_STR_SLICE_LITERAL("INPORT.2"),
        SOL_STR_SLICE_LITERAL("Something(())"),
        SOL_STR_SLICE_LITERAL("Something)"),
        SOL_STR_SLICE_LITERAL("DECLARE=A"),
        SOL_STR_SLICE_LITERAL("DECLARE=A:B"),
        SOL_STR_SLICE_LITERAL("PORT["),
        SOL_STR_SLICE_LITERAL("PORT]"),
        SOL_STR_SLICE_LITERAL("PORT[NaN]"),
        SOL_STR_SLICE_LITERAL("OPTION=A"),
        SOL_STR_SLICE_LITERAL("OPTION=A:B"),
        SOL_STR_SLICE_LITERAL("OPTION=A.B"),
        SOL_STR_SLICE_LITERAL("OPTION=A:B.C"),
    };

    for (i = 0; i < ARRAY_SIZE(tests); i++) {
        struct sol_fbp_scanner scanner;
        struct sol_vector v = SOL_VECTOR_INIT(enum sol_fbp_token_type);
        enum sol_fbp_token_type *type;

        sol_fbp_scanner_init(&scanner, tests[i]);
        do {
            sol_fbp_scan_token(&scanner);
            type = sol_vector_append(&v);
            *type = scanner.token.type;
        } while (*type != SOL_FBP_TOKEN_EOF && *type != SOL_FBP_TOKEN_ERROR);

        if (*type == SOL_FBP_TOKEN_EOF) {
            SOL_WRN(
                "Expected ERROR when scanning string '%.*s' but got a succesful scan:\n",
                SOL_STR_SLICE_PRINT(tests[i]));
            SOL_VECTOR_FOREACH_IDX (&v, type, j) {
                SOL_WRN("- %s\n", token_names[*type]);
            }
            ASSERT(false);
        }

        sol_vector_clear(&v);
    }
}


DEFINE_TEST(token_position);
static void
token_position(void)
{
    struct sol_fbp_scanner scanner;
    const char *input =
        "a(mod/A)     OUT  ->  IN     b(mod/B)\n"
        "\n"
        "\n"
        "    a OUT -> IN c(mod/C)\n";
    unsigned int i;

    struct entry {
        enum sol_fbp_token_type type;
        unsigned int line;
        unsigned int col;
        const char *contents;
    } expected[] = {
        { SOL_FBP_TOKEN_IDENTIFIER,     1,  1, "a" },
        { SOL_FBP_TOKEN_PAREN_OPEN,     1,  2, "(" },
        { SOL_FBP_TOKEN_IDENTIFIER,     1,  3, "mod/A" },
        { SOL_FBP_TOKEN_PAREN_CLOSE,    1,  8, ")" },
        { SOL_FBP_TOKEN_IDENTIFIER,     1, 14, "OUT" },
        { SOL_FBP_TOKEN_ARROW,          1, 19, "->" },
        { SOL_FBP_TOKEN_IDENTIFIER,     1, 23, "IN" },
        { SOL_FBP_TOKEN_IDENTIFIER,     1, 30, "b" },
        { SOL_FBP_TOKEN_PAREN_OPEN,     1, 31, "(" },
        { SOL_FBP_TOKEN_IDENTIFIER,     1, 32, "mod/B" },
        { SOL_FBP_TOKEN_PAREN_CLOSE,    1, 37, ")" },
        { SOL_FBP_TOKEN_STMT_SEPARATOR, 1, 38, "\n" },
        { SOL_FBP_TOKEN_STMT_SEPARATOR, 2,  1, "\n" },
        { SOL_FBP_TOKEN_STMT_SEPARATOR, 3,  1, "\n" },
        { SOL_FBP_TOKEN_IDENTIFIER,     4,  5, "a" },
        { SOL_FBP_TOKEN_IDENTIFIER,     4,  7, "OUT" },
        { SOL_FBP_TOKEN_ARROW,          4, 11, "->" },
        { SOL_FBP_TOKEN_IDENTIFIER,     4, 14, "IN" },
        { SOL_FBP_TOKEN_IDENTIFIER,     4, 17, "c" },
        { SOL_FBP_TOKEN_PAREN_OPEN,     4, 18, "(" },
        { SOL_FBP_TOKEN_IDENTIFIER,     4, 19, "mod/C" },
        { SOL_FBP_TOKEN_PAREN_CLOSE,    4, 24, ")" },
        { SOL_FBP_TOKEN_STMT_SEPARATOR, 4, 25, "\n" },
        { SOL_FBP_TOKEN_EOF,            0,  0, NULL },
    };

    sol_fbp_scanner_init(&scanner, sol_str_slice_from_str(input));

    for (i = 0; i < ARRAY_SIZE(expected); i++) {
        struct entry *e = &expected[i];
        struct sol_fbp_token *t = &scanner.token;
        char *token_contents;

        sol_fbp_scan_token(&scanner);
        token_contents = strndupa(t->start, t->end - t->start);
        if (e->type != t->type) {
            SOL_WRN("Failed to scan string '%s': wrong token type for token at index (%u), expected '%s' but got '%s'",
                input, i, token_names[e->type], token_names[t->type]);
            ASSERT(false);
        }

        if (e->type == SOL_FBP_TOKEN_EOF)
            break;

        if (!streq(e->contents, token_contents)) {
            SOL_WRN("Failed to scan string '%s': wrong contents for token '%s' (%u), expected '%s' but got '%s'",
                input, token_names[t->type], i, e->contents, token_contents);
            ASSERT(false);
        }

        if (e->line != t->line || e->col != t->column) {
            SOL_WRN("Failed to scan string '%s': wrong position for token '%s' (%u) with contents '%s', expected (%u, %u) but got (%u, %u)",
                input, token_names[t->type], i, token_contents, e->line, e->col, t->line, t->column);
            ASSERT(false);
        }
    }
}


TEST_MAIN();
