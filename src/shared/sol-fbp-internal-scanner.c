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

#include <assert.h>
#include <ctype.h>

#include "sol-fbp-internal-log.h"
#include "sol-fbp-internal-scanner.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"

/* Scanner is implemented as an state machine, each state is a
 * function that walks the start/end pointers to find tokens or ignore
 * the input and move to a new state. Each time sol_fbp_scan_token() is
 * called, the machine will be run until a new token is found. */

static void *default_state(struct sol_fbp_scanner *s);

void
sol_fbp_scanner_init(struct sol_fbp_scanner *s, struct sol_str_slice input)
{
    sol_fbp_init_log_domain();
    s->state = default_state;
    s->token.type = SOL_FBP_TOKEN_NONE;
    s->token.start = s->token.end = input.data;
    s->input_end = input.data + input.len;

    s->cur.line = s->cur.col = 1;
    s->start = s->cur;
}

static inline char
next(struct sol_fbp_scanner *s)
{
    char c;

    if (s->token.end == s->input_end)
        return 0;
    c = *s->token.end;
    s->token.end++;
    if (c == '\n') {
        s->cur.line++;
        s->cur.col = 1;
    } else {
        s->cur.col++;
    }
    return c;
}

static inline void
set_token(struct sol_fbp_scanner *s, enum sol_fbp_token_type type)
{
    s->token.type = type;
    s->token.line = s->start.line;
    s->token.column = s->start.col;
}

static inline char
peek(struct sol_fbp_scanner *s)
{
    if (s->token.end == s->input_end)
        return 0;
    return *s->token.end;
}

static inline void
ignore(struct sol_fbp_scanner *s)
{
    s->token.start = s->token.end;
    s->start = s->cur;
}

static void *
error_state(struct sol_fbp_scanner *s)
{
    set_token(s, SOL_FBP_TOKEN_ERROR);
    return NULL;
}

static inline bool
is_node_ident(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static void *
space_state(struct sol_fbp_scanner *s)
{
    while (isblank((unsigned char)peek(s)))
        next(s);
    ignore(s);
    return default_state;
}

static void *
comment_state(struct sol_fbp_scanner *s)
{
    char c;

    for (c = peek(s); c != '\n' && c != 0; c = peek(s))
        next(s);
    return default_state;
}

static void *
carriage_return_state(struct sol_fbp_scanner *s)
{
    /* Consume '\r'. */
    next(s);

    if (peek(s) != '\n')
        return error_state;

    return default_state;
}

static void *
arrow_state(struct sol_fbp_scanner *s)
{
    /* Consume '-'. */
    next(s);

    if (next(s) != '>')
        return error_state;

    set_token(s, SOL_FBP_TOKEN_ARROW);
    return default_state;
}

static void *export_state(struct sol_fbp_scanner *s);
static void *export_port_array_state(struct sol_fbp_scanner *s);

static void *
export_ident_state(struct sol_fbp_scanner *s)
{
    while (is_node_ident(peek(s)))
        next(s);
    set_token(s, SOL_FBP_TOKEN_IDENTIFIER);
    return export_state;
}

static void *
export_port_index_state(struct sol_fbp_scanner *s)
{
    while (isdigit(peek(s)))
        next(s);
    set_token(s, SOL_FBP_TOKEN_INTEGER);
    return export_port_array_state;
}

static void *
export_port_array_state(struct sol_fbp_scanner *s)
{
    char c = peek(s);

    switch (c) {
    case ']':
        next(s);
        set_token(s, SOL_FBP_TOKEN_BRACKET_CLOSE);
        return export_state;

    default:
        if (isdigit(c)) {
            return export_port_index_state;
        }
        return error_state;
    }
}

static void *
export_state(struct sol_fbp_scanner *s)
{
    char c = peek(s);

    switch (c) {
    case '.':
        next(s);
        set_token(s, SOL_FBP_TOKEN_DOT);
        return export_ident_state;

    case ':':
        next(s);
        set_token(s, SOL_FBP_TOKEN_COLON);
        return export_ident_state;

    case '[':
        next(s);
        set_token(s, SOL_FBP_TOKEN_BRACKET_OPEN);
        return export_port_array_state;

    case ',':
    case ' ':
    case '\n':
    case '\r':
    case '\t':
    case '#':
    case 0:
        return default_state;

    default:
        return error_state;
    }
    ;
}

static void *
inport_equal_state(struct sol_fbp_scanner *s)
{
    if (next(s) != '=')
        return error_state;
    set_token(s, SOL_FBP_TOKEN_EQUAL);
    return export_ident_state;
}

static void *
inport_state(struct sol_fbp_scanner *s)
{
    set_token(s, SOL_FBP_TOKEN_INPORT_KEYWORD);
    return inport_equal_state;
}

static void *
outport_equal_state(struct sol_fbp_scanner *s)
{
    if (next(s) != '=')
        return error_state;
    set_token(s, SOL_FBP_TOKEN_EQUAL);
    return export_ident_state;
}

static void *
outport_state(struct sol_fbp_scanner *s)
{
    set_token(s, SOL_FBP_TOKEN_OUTPORT_KEYWORD);
    return outport_equal_state;
}

static void *
declare_end_state(struct sol_fbp_scanner *s)
{
    switch (peek(s)) {
    case ',':
    case ' ':
    case '\n':
    case '\r':
    case '\t':
    case '#':
    case 0:
        return default_state;

    default:
        next(s);
        return error_state;
    }
}

static void *
declare_contents_state(struct sol_fbp_scanner *s)
{
    char c;

    for (c = peek(s); is_node_ident(peek(s)) || c == '.'; c = peek(s))
        next(s);
    set_token(s, SOL_FBP_TOKEN_IDENTIFIER);
    return declare_end_state;
}

static void *
declare_second_sep_state(struct sol_fbp_scanner *s)
{
    if (next(s) != ':') {
        return error_state;
    }
    set_token(s, SOL_FBP_TOKEN_COLON);
    return declare_contents_state;
}

static void *
declare_kind_state(struct sol_fbp_scanner *s)
{
    while (is_node_ident(peek(s)))
        next(s);
    set_token(s, SOL_FBP_TOKEN_IDENTIFIER);
    return declare_second_sep_state;
}

static void *
declare_first_sep_state(struct sol_fbp_scanner *s)
{
    if (next(s) != ':')
        return error_state;
    set_token(s, SOL_FBP_TOKEN_COLON);
    return declare_kind_state;
}

static void *
declare_name_state(struct sol_fbp_scanner *s)
{
    while (is_node_ident(peek(s)))
        next(s);
    set_token(s, SOL_FBP_TOKEN_IDENTIFIER);
    return declare_first_sep_state;
}

static void *
declare_equal_state(struct sol_fbp_scanner *s)
{
    if (next(s) != '=')
        return error_state;
    set_token(s, SOL_FBP_TOKEN_EQUAL);
    return declare_name_state;
}

static void *
declare_state(struct sol_fbp_scanner *s)
{
    set_token(s, SOL_FBP_TOKEN_DECLARE_KEYWORD);
    return declare_equal_state;
}

static void *
option_end_state(struct sol_fbp_scanner *s)
{
    switch (peek(s)) {
    case ',':
    case ' ':
    case '\n':
    case '\r':
    case '\t':
    case '#':
    case 0:
        return default_state;

    default:
        next(s);
        return error_state;
    }
}

static void *
option_name_state(struct sol_fbp_scanner *s)
{
    while (is_node_ident(peek(s)))
        next(s);
    set_token(s, SOL_FBP_TOKEN_IDENTIFIER);
    return option_end_state;
}

static void *
option_second_sep_state(struct sol_fbp_scanner *s)
{
    if (next(s) != ':')
        return error_state;
    set_token(s, SOL_FBP_TOKEN_COLON);
    return option_name_state;
}
static void *
option_node_option_state(struct sol_fbp_scanner *s)
{
    while (is_node_ident(peek(s)))
        next(s);
    set_token(s, SOL_FBP_TOKEN_IDENTIFIER);
    return option_second_sep_state;
}

static void *
option_first_sep_state(struct sol_fbp_scanner *s)
{
    if (next(s) != '.')
        return error_state;
    set_token(s, SOL_FBP_TOKEN_DOT);
    return option_node_option_state;
}

static void *
option_node_name_state(struct sol_fbp_scanner *s)
{
    while (is_node_ident(peek(s)))
        next(s);
    set_token(s, SOL_FBP_TOKEN_IDENTIFIER);
    return option_first_sep_state;
}

static void *
option_equal_state(struct sol_fbp_scanner *s)
{
    if (next(s) != '=')
        return error_state;
    set_token(s, SOL_FBP_TOKEN_EQUAL);
    return option_node_name_state;
}

static void *
option_state(struct sol_fbp_scanner *s)
{
    set_token(s, SOL_FBP_TOKEN_OPTION_KEYWORD);
    return option_equal_state;
}

static const struct sol_str_table_ptr keyword_table[] = {
    SOL_STR_TABLE_PTR_ITEM("INPORT", inport_state),
    SOL_STR_TABLE_PTR_ITEM("OUTPORT", outport_state),
    SOL_STR_TABLE_PTR_ITEM("DECLARE", declare_state),
    SOL_STR_TABLE_PTR_ITEM("OPTION", option_state),
    { }
};

static void *
identifier_state(struct sol_fbp_scanner *s)
{
    set_token(s, SOL_FBP_TOKEN_IDENTIFIER);
    return default_state;
}

static void *
identifier_or_keyword_state(struct sol_fbp_scanner *s)
{
    void *(*next_state)(struct sol_fbp_scanner *s);
    struct sol_str_slice identifier;

    /* Note that FBP allows numbers to be the first character. */
    while (is_node_ident(peek(s)))
        next(s);

    identifier.data = s->token.start;
    identifier.len = s->token.end - s->token.start;
    next_state = sol_str_table_ptr_lookup_fallback(
        keyword_table, identifier, identifier_state);

    return next_state;
}

static void *port_array_state(struct sol_fbp_scanner *s);

static void *
port_index_state(struct sol_fbp_scanner *s)
{
    while (isdigit(peek(s)))
        next(s);
    set_token(s, SOL_FBP_TOKEN_INTEGER);
    return port_array_state;
}

static void *
port_array_state(struct sol_fbp_scanner *s)
{
    char c = peek(s);

    switch (c) {
    case ']':
        next(s);
        set_token(s, SOL_FBP_TOKEN_BRACKET_CLOSE);
        return default_state;

    default:
        if (isdigit(c)) {
            return port_index_state;
        }
        return error_state;
    }
}

static inline bool
is_meta_ident(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '/' || c == '|' ||
           c == ':' || c == '.' || c == '-';
}

static void *meta_state(struct sol_fbp_scanner *s);
static void *string_state(struct sol_fbp_scanner *s);
static void *string_start_state(struct sol_fbp_scanner *s);
static void *string_end_state(struct sol_fbp_scanner *s);

static void *
string_start_state(struct sol_fbp_scanner *s)
{
    if (peek(s) == '"') {
        next(s);
        return string_state(s);
    }
    SOL_DBG("Error, expected \" but got %c", peek(s));
    return error_state;
}

static void *
string_end_state(struct sol_fbp_scanner *s)
{
    if (peek(s) == '"') {
        next(s);
        set_token(s, SOL_FBP_TOKEN_STRING);
        return meta_state;
    }
    SOL_DBG("Error, expected \" but got %c", peek(s));
    return error_state;
}

static void *
string_escape_state(struct sol_fbp_scanner *s)
{
    if (peek(s) == '\\') {
        char next_char;
        next(s);
        next_char = peek(s);

        switch (next_char) {
        case 'a': break;
        case 'b': break;
        case 'f': break;
        case 'n': break;
        case 'r': break;
        case 't': break;
        case 'v': break;
        case '\\': break;
        case '"': break;
        case '\'': break;
        default:
            SOL_WRN("Error, invalid escape sequence. \\%c", next_char);
            return error_state;
        }

        next(s);
        return string_state(s);
    }
    SOL_DBG("Error, expected \\ but got %c", peek(s));
    return error_state;
}

static void *
string_state(struct sol_fbp_scanner *s)
{
    unsigned char curr;

    for (;;) {
        curr = peek(s);
        switch (curr) {
        case '\\':
            return string_escape_state(s);
        case '"':
            return string_end_state(s);
        case '\0':
        case '\n':
        case '\r':
            goto fail;
        }
        next(s);
    }

fail:
    return error_state;
}

static void *
meta_item_state(struct sol_fbp_scanner *s)
{
    while (is_meta_ident(peek(s)))
        next(s);
    set_token(s, SOL_FBP_TOKEN_IDENTIFIER);
    return meta_state;
}

static void *
meta_state(struct sol_fbp_scanner *s)
{
    char c = peek(s);

    switch (c) {
    case ':':
        next(s);
        set_token(s, SOL_FBP_TOKEN_COLON);
        return meta_state;

    case ')':
        next(s);
        set_token(s, SOL_FBP_TOKEN_PAREN_CLOSE);
        return default_state;

    case '=':
        next(s);
        set_token(s, SOL_FBP_TOKEN_EQUAL);
        return meta_state;

    case ',':
        next(s);
        set_token(s, SOL_FBP_TOKEN_COMMA);
        return meta_state;

    case ' ':
    case '\t':
    case '\n':
    case '\r':
        next(s);
        ignore(s);
        return meta_state;

    case '"':
        return string_start_state;

    default:
        if (is_meta_ident(c)) {
            return meta_item_state;
        }
        return error_state;
    }
}

static inline bool
is_component_ident(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '/' || c == '-';
}

static void *
component_state(struct sol_fbp_scanner *s)
{
    while (is_component_ident(peek(s)))
        next(s);
    set_token(s, SOL_FBP_TOKEN_IDENTIFIER);
    return meta_state;
}

static void *
default_state(struct sol_fbp_scanner *s)
{
    char c = peek(s);

    switch (c) {
    case 0:
        set_token(s, SOL_FBP_TOKEN_EOF);
        return NULL;

    case ' ':
    case '\t':
        return space_state;

    case '\r':
        return carriage_return_state;

    case '#':
        return comment_state;

    case '-':
        return arrow_state;

    case '\n':
    case ',':
        next(s);
        set_token(s, SOL_FBP_TOKEN_STMT_SEPARATOR);
        return default_state;

    case '(':
        next(s);
        set_token(s, SOL_FBP_TOKEN_PAREN_OPEN);
        return component_state;

    case '[':
        next(s);
        set_token(s, SOL_FBP_TOKEN_BRACKET_OPEN);
        return port_array_state;

    default:
        if (is_node_ident(c))
            return identifier_or_keyword_state;
        return error_state;
    }
}

void
sol_fbp_scan_token(struct sol_fbp_scanner *s)
{
    if (s->token.type == SOL_FBP_TOKEN_EOF || s->token.type == SOL_FBP_TOKEN_ERROR)
        return;

    s->token.start = s->token.end;
    s->start = s->cur;

    set_token(s, SOL_FBP_TOKEN_NONE);

    do {
        s->state = s->state(s);
    } while (s->token.type == SOL_FBP_TOKEN_NONE);
}
