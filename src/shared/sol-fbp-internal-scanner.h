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
    X(DECLARE_KEYWORD)

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
