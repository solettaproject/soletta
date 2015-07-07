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
#include <regex.h>
#include <stdio.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "platform-detect");

#include "sol-file-reader.h"
#include "sol-json.h"
#include "sol-platform-detect.h"
#include "sol-str-slice.h"
#include "sol-util.h"
#include "sol-vector.h"

#define PLATFORM_JSON "/platform_detect.json"
#define PLATFORM_NAME_REGEX "^[a-zA-Z0-9][a-zA-Z0-9_-]*$" //this regex matches the schema

static bool
_check_rule(const char *path, const struct sol_vector *match, const struct sol_vector *dont_match)
{
    unsigned int idx;
    int check;
    bool ret = true;
    char *data = NULL;
    char *regex_str;
    regex_t regex;
    struct sol_json_token *regex_token;

    data = sol_util_load_file_string(path, NULL);
    if (!data) {
        SOL_DBG("Could not open file '%s' : %s.", path, sol_util_strerrora(errno));
        return false;
    }

    if (match && match->len) {
        SOL_VECTOR_FOREACH_IDX (match, regex_token, idx) {
            regex_str = strndup(regex_token->start + 1, regex_token->end - regex_token->start - 2);
            check = regcomp(&regex, regex_str, REG_EXTENDED | REG_NOSUB);
            if (check) {
                SOL_DBG("Regular expression \"%s\" failed to compile. Ignoring it.", regex_str);
                free(regex_str);
                continue;
            }

            check = regexec(&regex, data, 0, NULL, 0);
            regfree(&regex);
            if (check) {
                SOL_DBG("Regular expression \"%s\" failed to find a match in file '%s'.",
                    regex_str, path);
                ret = false;
                free(regex_str);
                goto end;
            }
            free(regex_str);
        }
    }

    if (dont_match && dont_match->len) {
        SOL_VECTOR_FOREACH_IDX (dont_match, regex_token, idx) {
            regex_str = strndup(regex_token->start + 1, regex_token->end - regex_token->start - 2);
            check = regcomp(&regex, regex_str, REG_EXTENDED | REG_NOSUB);
            if (check) {
                SOL_DBG("Regular expression \"%s\" failed to compile. Ignoring it.", regex_str);
                free(regex_str);
                continue;
            }

            check = regexec(&regex, data, 0, NULL, 0);
            regfree(&regex);
            if (!check) {
                SOL_DBG("Regular expression \"%s\" found a match in file '%s' when it shouldn't.",
                    regex_str, path);
                ret = false;
                free(regex_str);
                goto end;
            }

            free(regex_str);
        }
    }

end:
    free(data);
    return ret;
}

static int
_parse_regex_array(struct sol_json_token *array, struct sol_vector *vector)
{
    struct sol_json_scanner scanner;
    struct sol_json_token *token;
    enum sol_json_loop_reason reason;

    sol_json_scanner_init_from_token(&scanner, array);
    SOL_JSON_SCANNER_ARRAY_LOOP (&scanner, array, SOL_JSON_TYPE_STRING, reason) {
        token = sol_vector_append(vector);
        SOL_NULL_CHECK(token, -errno);
        *token = *array;
    }

    return 0;
}

static bool
_platform_validation(const struct sol_json_token *validation)
{
    char *path;
    bool is_platform = false;
    struct sol_vector match, dont_match;
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value, file_path;
    enum sol_json_loop_reason reason;

    sol_vector_init(&match, sizeof(struct sol_json_token));
    sol_vector_init(&dont_match, sizeof(struct sol_json_token));
    sol_json_scanner_init_from_token(&scanner, validation);

    SOL_JSON_SCANNER_ARRAY_LOOP (&scanner, &token, SOL_JSON_TYPE_OBJECT_START, reason) {
        file_path = (struct sol_json_token) {NULL, NULL };
        sol_vector_clear(&match);
        sol_vector_clear(&dont_match);

        SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&scanner, &token, &key, &value, reason) {
            if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "file_path")) {
                if (sol_json_token_get_type(&value) != SOL_JSON_TYPE_STRING)
                    continue;
                file_path = value;
            }

            if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "match")) {
                if (sol_json_token_get_type(&value) != SOL_JSON_TYPE_ARRAY_START)
                    continue;
                if (_parse_regex_array(&value, &match) < 0)
                    goto clear;
            }

            if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "dont_match")) {
                if (sol_json_token_get_type(&value) != SOL_JSON_TYPE_ARRAY_START)
                    continue;
                if (_parse_regex_array(&value, &dont_match) < 0)
                    goto clear;
            }
        }

        if (file_path.start) {
            path = strndup(file_path.start + 1, file_path.end - file_path.start - 2);
            is_platform = _check_rule(path, &match, &dont_match);
            free(path);

            if (!is_platform)
                break;
        }
    }

clear:
    sol_vector_clear(&match);
    sol_vector_clear(&dont_match);
    return is_platform;
}

static struct sol_file_reader *
_json_open_doc(const char *path, struct sol_json_scanner *scanner)
{
    struct sol_str_slice file;
    struct sol_file_reader *raw_file = NULL;

    raw_file = sol_file_reader_open(path);
    if (!raw_file)
        return NULL;

    file = sol_file_reader_get_all(raw_file);
    sol_json_scanner_init(scanner, file.data, file.len);

    return raw_file;
}

char *
sol_platform_detect(void)
{
    bool found = false;
    char *platform = NULL;
    struct sol_file_reader *json_doc;
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value, platform_name = { NULL };
    enum sol_json_loop_reason reason;

    json_doc = _json_open_doc(PKGSYSCONFDIR PLATFORM_JSON, &scanner);
    if (!json_doc) {
        json_doc = _json_open_doc(DATADIR PLATFORM_JSON, &scanner);
        if (!json_doc) {
            SOL_INF(PLATFORM_JSON " could not be found. Searched paths:\n.%s\n%s",
                PKGSYSCONFDIR, DATADIR);
            return NULL;
        }
    }

    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&token, "platforms")) {
            found = true;
            break;
        }
    }
    if (!found)
        goto end;

    sol_json_scanner_init_from_token(&scanner, &value);
    SOL_JSON_SCANNER_ARRAY_LOOP (&scanner, &token, SOL_JSON_TYPE_OBJECT_START, reason) {
        platform_name = (struct sol_json_token) {NULL, NULL };
        found = false;

        SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&scanner, &token, &key, &value, reason) {
            if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "name")) {
                if (sol_json_token_get_type(&value) != SOL_JSON_TYPE_STRING)
                    continue;
                platform_name = value;
            }

            if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "validation")) {
                if (sol_json_token_get_type(&value) != SOL_JSON_TYPE_ARRAY_START)
                    continue;
                found = _platform_validation(&value);
            }
        }

        if (platform_name.start && found)
            break;
    }

    if (platform_name.start && found)
        platform = strndup(platform_name.start + 1, platform_name.end - platform_name.start - 2);

end:
    sol_file_reader_close(json_doc);
    return platform;
}

bool
sol_platform_invalid_name(const char *name)
{
    int check;
    regex_t regex;

    check = regcomp(&regex, PLATFORM_NAME_REGEX, REG_EXTENDED | REG_NOSUB);
    if (check) {
        SOL_WRN("Regular expression for platform name failed to compile: \"%s\".\n"
            "This should never happen.", PLATFORM_NAME_REGEX);
        return true;
    }

    check = regexec(&regex, name, 0, NULL, 0);
    regfree(&regex);
    if (check) {
        SOL_WRN("Platform name doesn't match specifications:\n"
            "name=\"%s\", spec=\"" PLATFORM_NAME_REGEX "\".", name);
        return true;
    }

    return false;
}
