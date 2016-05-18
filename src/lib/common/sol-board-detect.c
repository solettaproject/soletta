/*
 * This file is part of the Soletta Project
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
#include <regex.h>
#include <stdio.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "board-detect");

#include "sol-file-reader.h"
#include "sol-json.h"
#include "sol-board-detect.h"
#include "sol-str-slice.h"
#include "sol-util-file.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#define BOARD_DIR "boards/"

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
_board_validation(const struct sol_json_token *validation)
{
    char *path;
    bool is_board = false;
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
            is_board = _check_rule(path, &match, &dont_match);
            free(path);

            if (!is_board)
                break;
        }
    }

clear:
    sol_vector_clear(&match);
    sol_vector_clear(&dont_match);
    return is_board;
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

static enum sol_util_iterate_dir_reason
_append_file_path(void *data, const char *dir_path, struct dirent *ent)
{
    int r;
    size_t name_len, suffix_len;
    char *file_path;
    struct sol_ptr_vector *list = data;

    if (ent->d_type != DT_REG && ent->d_type != DT_LNK) //It won't be recursive
        return SOL_UTIL_ITERATE_DIR_CONTINUE;

    name_len = strlen(ent->d_name);
    suffix_len = strlen(".json");
    if (suffix_len > name_len || strcmp(ent->d_name + name_len - suffix_len, ".json"))
        return SOL_UTIL_ITERATE_DIR_CONTINUE;

    r = asprintf(&file_path, "%s%s", dir_path, ent->d_name);
    SOL_INT_CHECK(r, < 0, -ENOMEM);

    r = sol_ptr_vector_insert_sorted(list, file_path, (int (*)(const void *, const void *))strcmp);
    SOL_INT_CHECK(r, < 0, r);

    return SOL_UTIL_ITERATE_DIR_CONTINUE;
}

static char *
_process_file(const char *path)
{
    bool found = false;
    char *board = NULL;
    struct sol_file_reader *json_doc;
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value, board_name = { NULL };
    enum sol_json_loop_reason reason;

    json_doc = _json_open_doc(path, &scanner);
    if (!json_doc) {
        SOL_INF("Could not open file: %s", path);
        return NULL;
    }

    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&token, "boards")) {
            found = true;
            break;
        }
    }
    if (!found)
        goto end;

    sol_json_scanner_init_from_token(&scanner, &value);
    SOL_JSON_SCANNER_ARRAY_LOOP (&scanner, &token, SOL_JSON_TYPE_OBJECT_START, reason) {
        board_name = (struct sol_json_token) {NULL, NULL };
        found = false;

        SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&scanner, &token, &key, &value, reason) {
            if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "name")) {
                if (sol_json_token_get_type(&value) != SOL_JSON_TYPE_STRING)
                    continue;
                board_name = value;
            }

            if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "validation")) {
                if (sol_json_token_get_type(&value) != SOL_JSON_TYPE_ARRAY_START)
                    continue;
                found = _board_validation(&value);
            }
        }

        if (board_name.start && found)
            break;
    }

    if (board_name.start && found)
        board = strndup(board_name.start + 1, board_name.end - board_name.start - 2);

end:
    sol_file_reader_close(json_doc);
    return board;
}

char *
sol_board_detect(void)
{
    unsigned int idx;
    char *path;
    char *board = NULL;
    struct sol_ptr_vector file_list = SOL_PTR_VECTOR_INIT;

    sol_util_iterate_dir(PKGSYSCONFDIR BOARD_DIR, _append_file_path, &file_list);
    sol_util_iterate_dir(SOL_DATADIR BOARD_DIR, _append_file_path, &file_list);

    SOL_PTR_VECTOR_FOREACH_IDX (&file_list, path, idx) {
        if (!board)
            board = _process_file(path);
        free(path);
    }

    sol_ptr_vector_clear(&file_list);
    return board;
}
