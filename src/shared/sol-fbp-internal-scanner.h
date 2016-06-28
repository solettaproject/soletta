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

#pragma once

#include "sol-str-slice.h"

/* Given an input string written using the "FBP file format" described
 * in https://github.com/noflo/fbp/blob/master/README.md, produces
 * tokens suited for parsing that language. */

#define SOL_FBP_TOKEN_LIST(X)                           \
    X(NONE)                                            \
    X(ARROW)                                           \
    X(BRACKET_CLOSE)                                   \
    X(BRACKET_OPEN)                                    \
    X(COLON)                                           \
    X(COMMA)                                           \
    X(DOT)                                             \
    X(EOF)                                             \
    X(ERROR)                                           \
    X(EQUAL)                                           \
    X(IDENTIFIER)                                      \
    X(INPORT_KEYWORD)                                  \
    X(INTEGER)                                         \
    X(OUTPORT_KEYWORD)                                 \
    X(PAREN_CLOSE)                                     \
    X(PAREN_OPEN)                                      \
    X(STMT_SEPARATOR)                                  \
    X(STRING)                                          \
    X(DECLARE_KEYWORD)                                 \
    X(OPTION_KEYWORD)

#define TOKEN_ENUM(T) SOL_FBP_TOKEN_ ## T,

enum sol_fbp_token_type {
    SOL_FBP_TOKEN_LIST(TOKEN_ENUM)
};

#undef TOKEN_ENUM

struct sol_fbp_token {
    enum sol_fbp_token_type type;
    const char *start;
    const char *end;
    unsigned int line;
    unsigned int column;
};

/* To be used together with "%.*s" formatting in printf family of functions. */
#define SOL_FBP_TOKEN_PRINT(_s) (int)((_s).end - (_s).start + 1), (_s).start

struct sol_fbp_scanner {
    const char *input_end;
    struct sol_fbp_token token;
    void *(*state)(struct sol_fbp_scanner *s);

    struct {
        unsigned int line;
        unsigned int col;
    } start, cur;
};

void sol_fbp_scanner_init(struct sol_fbp_scanner *s, struct sol_str_slice input);
void sol_fbp_scan_token(struct sol_fbp_scanner *s);
