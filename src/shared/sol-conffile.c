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
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "sol-conffile.h"
#include "sol-file-reader.h"
#include "sol-json.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-platform.h"
#include "sol-util.h"
#include "sol-vector.h"

static struct sol_ptr_vector _conffile_entry_vector; /* entries created by conffiles */
static struct sol_ptr_vector _conffiles_loaded; /* paths of the currently loaded conffiles */

struct sol_conffile_entry {
    char *id;
    char *type;
    struct sol_ptr_vector options;
};

static int
_entry_sort_cb(const void *data1, const void *data2)
{
    const struct sol_conffile_entry *e1 = data1;
    const struct sol_conffile_entry *e2 = data2;

    return strcasecmp(e1->id, e2->id);
}

static void
_free_entry(struct sol_conffile_entry *entry)
{
    int idx;
    void *ptr;

    if (!entry)
        return;

    free(entry->id);
    free(entry->type);
    SOL_PTR_VECTOR_FOREACH_IDX (&entry->options, ptr, idx) {
        free(ptr);
    }
    sol_ptr_vector_clear(&entry->options);
    free(entry);
}

static int
sol_conffile_set_entry_options(struct sol_conffile_entry *entry, struct sol_json_token options_object)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    struct sol_ptr_vector vec_options;
    enum sol_json_loop_reason reason;

    sol_ptr_vector_init(&vec_options);

    sol_json_scanner_init_from_token(&scanner, &options_object);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        int key_len, value_len;
        char *tmp;

        sol_json_token_remove_quotes(&key);
        key_len = sol_json_token_get_size(&key);
        sol_json_token_remove_quotes(&value);
        value_len = sol_json_token_get_size(&value);

        if (!key_len || !value_len) {
            reason = SOL_JSON_LOOP_REASON_INVALID;
            break;
        }

        if (asprintf(&tmp, "%.*s=%.*s", key_len, key.start, value_len, value.start) <= 0) {
            SOL_WRN("Couldn't allocate memory for the config file, ignoring options.");
            return -ENOMEM;
            break;
        }
        sol_ptr_vector_append(&vec_options, tmp);
    }
    if (reason != SOL_JSON_LOOP_REASON_OK) {
        int i;
        void *ptr;
        SOL_DBG("Error: Invalid JSON.");
        SOL_PTR_VECTOR_FOREACH_IDX (&vec_options, ptr, i) {
            free(ptr);
        }
        sol_ptr_vector_clear(&vec_options);
        return -ENOKEY;
    }

    sol_ptr_vector_append(&vec_options, NULL);
    entry->options = vec_options;
    return 0;
}

static char *
_dup_json_str(struct sol_json_token token)
{
    char *ret = sol_json_token_get_size(&token) >= 2 ?
        strndup(token.start + 1, sol_json_token_get_size(&token) - 2) : NULL;

    if (!ret)
        SOL_DBG("Error alocating memory for string");
    return ret;
}

static bool
_entry_vector_contains(const char *id)
{
    uint16_t i;
    struct sol_conffile_entry *ptr;

    SOL_PTR_VECTOR_FOREACH_IDX (&_conffile_entry_vector, ptr, i) {
        if (strcmp(ptr->id, id) == 0) {
            return true;
        }
    }
    return false;
}

static int
_json_to_vector(struct sol_json_scanner scanner)
{
    struct sol_conffile_entry *entry = NULL;
    const char *node_group = "nodetypes";
    const char *node_name = "name";
    const char *node_type = "type";
    const char *node_options = "options";
    struct sol_json_token token, key, value;
    struct sol_json_scanner obj_scanner;
    enum sol_json_loop_reason reason;
    bool found_nodes = false;

    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (sol_json_token_str_eq(&key, node_group, strlen(node_group))) {
            found_nodes = true;
            break;
        }
    }
    if (reason != SOL_JSON_LOOP_REASON_OK) {
        SOL_DBG("Error: Invalid Json.");
        goto err;
    }

    if (!found_nodes)
        return -ENOKEY;

    sol_json_scanner_init_from_token(&obj_scanner, &value);
    SOL_JSON_SCANNER_ARRAY_LOOP (&obj_scanner, &token, SOL_JSON_TYPE_OBJECT_START, reason) {
        bool duplicate = false;

        entry = calloc(1, sizeof(*entry));
        SOL_NULL_CHECK_GOTO(entry, err);
        sol_ptr_vector_init(&entry->options);
        SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&obj_scanner, &token, &key, &value, reason) {
            if (sol_json_token_str_eq(&key, node_name, strlen(node_name))) {
                entry->id = _dup_json_str(value);
                /* if we already have this entry on the vector, try the next.
                 * this could be caused by some config files trying to setup
                 * nodes with the same name
                 */
                if (!entry->id) {
                    goto entry_err;
                }
                if (_entry_vector_contains(entry->id)) {
                    duplicate = true;
                    break;
                }
            } else if (sol_json_token_str_eq(&key, node_type, strlen(node_type))) {
                entry->type = _dup_json_str(value);
                if (!entry->type) {
                    goto entry_err;
                }
            } else if (sol_json_token_str_eq(&key, node_options, strlen(node_options))) {
                if (sol_conffile_set_entry_options(entry, value) != 0) {
                    goto entry_err;
                }
            }
        }

        if (duplicate) {
            _free_entry(entry);
            entry = NULL;
            continue;
        }

        if (reason != SOL_JSON_LOOP_REASON_OK) {
            SOL_DBG("Error: Invalid Json.");
            goto entry_err;
        }

        if (!entry->type || !entry->id) {
            SOL_DBG("Error: Invalid config type entry, please check your config file.");
            goto entry_err;
        }

        if (sol_ptr_vector_insert_sorted(&_conffile_entry_vector, entry, _entry_sort_cb) == -1) {
            SOL_DBG("Error: Couldn't setup config entry");
            goto entry_err;
        }
    }
    if (reason != SOL_JSON_LOOP_REASON_OK) {
        SOL_DBG("Error: Invalid Json.");
        goto entry_err;
    }

    return 0;

entry_err:
    _free_entry(entry);
err:
    return -ENOMEM;
}

static void
_get_json_include_paths(
    struct sol_json_scanner json_scanner,
    char **include,
    struct sol_vector *include_fallbacks)
{
    static const char *include_group = "config_includes";
    static const char *include_str = "include";
    static const char *include_fallback = "include_fallbacks";

    bool found_include = false;

    struct sol_json_token token, key, value;
    struct sol_json_scanner include_scanner;
    enum sol_json_loop_reason reason;

    SOL_JSON_SCANNER_OBJECT_LOOP (&json_scanner, &token, &key, &value, reason) {
        if (sol_json_token_str_eq(&key, include_group, strlen(include_group))) {
            found_include = true;
            break;
        }
    }
    if (reason != SOL_JSON_LOOP_REASON_OK) {
        SOL_DBG("Error: Invalid Json.");
        return;
    }

    if (!found_include)
        return;

    sol_json_scanner_init_from_token(&include_scanner, &value);
    SOL_JSON_SCANNER_OBJECT_LOOP (&include_scanner, &token, &key, &value, reason) {
        if (sol_json_token_str_eq(&key, include_str, strlen(include_str))) {
            *include = strndup(value.start + 1, sol_json_token_get_size(&value) - 2);
            if (!*include)
                SOL_DBG("Error: couldn't allocate memory for string.");
            continue;
        }
        if (sol_json_token_str_eq(&key, include_fallback, strlen(include_fallback))) {
            char *buff;
            struct sol_str_slice *s;
            sol_json_token_remove_quotes(&value);
            buff = strndup(value.start, sol_json_token_get_size(&value));
            if (!buff)
                SOL_DBG("Error: couldn't allocate memory for string.");
            else {
                s = sol_vector_append(include_fallbacks);
                if (s) {
                    s->data = buff;
                    s->len = strlen(buff);
                } else {
                    free(buff);
                }
            }
            continue;
        }
    }
    if (reason != SOL_JSON_LOOP_REASON_OK) {
        SOL_DBG("Error: Invalid Json.");
    }
}

static bool
_already_loaded(const char *filename)
{
    uint16_t i;
    char *ptr;

    SOL_PTR_VECTOR_FOREACH_IDX (&_conffiles_loaded, ptr, i) {
        if (strcmp(ptr, filename) == 0) {
            return true;
        }
    }
    return false;
}

static struct sol_str_slice
_load_json_from_dirs(const char *file, char **full_path, struct sol_file_reader **file_reader)
{
    size_t i;
    struct sol_str_slice config_file_contents = SOL_STR_SLICE_EMPTY;
    char *curr_dir;
    const char *search_dirs[] = {
        NULL, /* current dir ( setup later ) */
        ".",  /* another way of current dir */
        "",   /* full path */
        PKGSYSCONFDIR /* pkg system install */
    };

    search_dirs[0] = curr_dir = get_current_dir_name();

    for (i = 0; i < ARRAY_SIZE(search_dirs); i++) {
        char *filename;

        if (asprintf(&filename, "%s/%s", search_dirs[i], file) < 0 || !filename) {
            SOL_WRN("Couldn't allocate memory for config file.");
            break;
        }

        // if we _already_loaded this particular conffile, we don't actually need to do a thing.
        if (_already_loaded(filename)) {
            free(filename);
            goto exit;
        }
        *file_reader = sol_file_reader_open(filename);
        if (*file_reader) {
            config_file_contents = sol_file_reader_get_all(*file_reader);

            /* We can't close the file_reader on sucess because then the slice would
             * also be killed, so we postpone it till later. */
            if (config_file_contents.len != 0) {
                sol_ptr_vector_append(&_conffiles_loaded, filename);
                break;
            }

            /* but then there's no need to keep it open if it failed. */
            free(filename);
            sol_file_reader_close(*file_reader);
            *file_reader = NULL;
        }
    }

    if (!config_file_contents.data)
        SOL_DBG("could not load config file.");

exit:
    free(curr_dir);
    return config_file_contents;
}

static void
_clear_data(void)
{
    void *ptr;
    struct sol_conffile_entry *e;
    uint16_t i;

    // Clear the entries.
    SOL_PTR_VECTOR_FOREACH_IDX (&_conffile_entry_vector, e, i) {
        _free_entry(e);
    }
    sol_ptr_vector_clear(&_conffile_entry_vector);

    // Clear the filenames.
    SOL_PTR_VECTOR_FOREACH_IDX (&_conffiles_loaded, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&_conffiles_loaded);
}

static struct sol_str_slice
_load_json_from_paths(const char *path,
    struct sol_vector *fallback_paths,
    char **full_path,
    struct sol_file_reader **file_reader)
{
    struct sol_str_slice config_file_contents = SOL_STR_SLICE_EMPTY;

    if (path)
        config_file_contents = _load_json_from_dirs(path, full_path, file_reader);

    if (fallback_paths && !config_file_contents.len) {
        uint16_t idx;
        struct sol_str_slice *s_ptr;
        SOL_VECTOR_FOREACH_IDX (fallback_paths, s_ptr, idx) {
            SOL_DBG("Trying to load conffile: %s", s_ptr->data);
            config_file_contents = _load_json_from_dirs(s_ptr->data, full_path, file_reader);
            if (config_file_contents.len) {
                SOL_DBG("Successfully loaded conffile: %s", s_ptr->data);
                break;
            }
        }
    }
    return config_file_contents;
}

static void
_fill_vector(const char *path, struct sol_vector *fallback_paths)
{
    char *full_path = NULL;
    char *include = NULL;
    struct sol_str_slice *slice;
    uint16_t i;
    struct sol_vector include_fallbacks = SOL_VECTOR_INIT(struct sol_str_slice);
    struct sol_str_slice config_file_contents = SOL_STR_SLICE_EMPTY;
    struct sol_file_reader *file_reader = NULL;
    struct sol_json_scanner json_scanner;

    config_file_contents = _load_json_from_paths(path, fallback_paths, &full_path, &file_reader);
    if (!config_file_contents.len) {
        if (file_reader)
            sol_file_reader_close(file_reader);
        return;
    }

    sol_json_scanner_init(&json_scanner, config_file_contents.data, config_file_contents.len);
    _get_json_include_paths(json_scanner, &include, &include_fallbacks);

    if (_json_to_vector(json_scanner) != 0)
        goto free_for_all;

    if (include || include_fallbacks.len) {
        _fill_vector(include, &include_fallbacks);
    }

free_for_all:
    sol_file_reader_close(file_reader);
    free(full_path);
    free(include);

    SOL_VECTOR_FOREACH_IDX (&include_fallbacks, slice, i) {
        free((char *)slice->data);
    }

    sol_vector_clear(&include_fallbacks);
}

static void
_add_lookup_path(struct sol_vector *vector, const char **search_dirs, char *appname, const char *board_name)
{
    size_t i;
    struct sol_vector files = SOL_VECTOR_INIT(struct sol_str_slice);

#define ADD_PATH_FMT(_vector, _fmt, ...) \
    do { \
        int _r; \
        char *_buff; \
        struct sol_str_slice *_s; \
        _r = asprintf(&_buff, _fmt, ## __VA_ARGS__); \
        if (_r < 0 || !_buff) \
            SOL_WRN("Couldn't allocate memory for config file formatting."); \
        else { \
            _s = sol_vector_append(_vector); \
            if (_s) { \
                _s->data = _buff; \
                _s->len = strlen(_buff); \
            } else { \
                free(_buff); \
            } \
        } \
    } while (0) \

    if (appname && board_name) {
        ADD_PATH_FMT(&files, "sol-flow-%s-%s.json", appname, board_name);
    }

    if (appname) {
        ADD_PATH_FMT(&files, "sol-flow-%s.json", appname);
    }

    if (board_name) {
        ADD_PATH_FMT(&files, "sol-flow-%s.json", board_name);
    }

    ADD_PATH_FMT(&files, "sol-flow.json");

    for (i = 0; i < ARRAY_SIZE(search_dirs); i++) {
        struct sol_str_slice *curr_file;
        uint16_t idx;
        const char *dir = search_dirs[i];

        if (!dir)
            continue;

        SOL_VECTOR_FOREACH_IDX (&files, curr_file, idx) {
            ADD_PATH_FMT(vector, "%s/%s", dir, curr_file->data);
            free((char *)curr_file->data);
        }
    }

    sol_vector_clear(&files);

#undef ADD_PATH_FMT
}

static void
_load_vector_defaults(void)
{
    char *appname, **argv;
    const char *board_name;
    uint16_t i;
    struct sol_str_slice *slice;
    struct sol_vector fallback_paths = SOL_VECTOR_INIT(struct sol_str_slice);
    const char *search_dirs[] = {
        ".", /* $PWD */
        NULL, /* appdir */
        PKGSYSCONFDIR, /* i.e /etc/soletta/ */
    };

    board_name = sol_platform_get_board_name();
    argv = sol_argv();
    appname = NULL;

    if (argv) {
        appname = basename(argv[0]);
        search_dirs[1] = dirname(argv[0]);
    }

    _add_lookup_path(&fallback_paths, search_dirs, appname, board_name);

    _fill_vector(getenv("SOL_FLOW_MODULE_RESOLVER_CONFFILE"),
        &fallback_paths);

    SOL_VECTOR_FOREACH_IDX (&fallback_paths, slice, i) {
        free((char *)slice->data);
    }

    sol_vector_clear(&fallback_paths);
}

static void
_init(void)
{
    static int first_call = true;

    if (first_call) {
        sol_ptr_vector_init(&_conffile_entry_vector);
        sol_ptr_vector_init(&_conffiles_loaded);
        atexit(_clear_data);
    }
    first_call = false;
}

static int
_bsearch_entry_cb(const void *data1, const void *data2)
{
    const void **p = (const void **)data2;

    return _entry_sort_cb(data1, *p);
}

static int
_resolve_config(const char *id, const char **type, const char ***opts)
{
    struct sol_conffile_entry key;
    struct sol_conffile_entry *entry;
    void **vector_pointer;

    if (sol_ptr_vector_get_len(&_conffile_entry_vector) <= 0) {
        return -ENOENT;
    }

    key.id = (char *)id;
    vector_pointer = bsearch(&key,
        _conffile_entry_vector.base.data,
        sol_ptr_vector_get_len(&_conffile_entry_vector),
        _conffile_entry_vector.base.elem_size,
        _bsearch_entry_cb);
    if (!vector_pointer) {
        SOL_DBG("could not find entry [%s]", id);
        return -ENOENT;
    }

    entry = *vector_pointer;
    if (!entry) {
        SOL_DBG("could not find entry [%s]", id);
        return -EINVAL;
    }

    if (!entry->type) {
        SOL_DBG("could not find mandatory [%s] Type= key", id);
        return -EINVAL;
    }

    *type = entry->type;

    if (opts && entry->options.base.len)
        *opts = (const char **)entry->options.base.data;

    return 0;
}

int
sol_conffile_resolve(const char *id, const char **type, const char ***opts)
{
    static bool first_call = true;

    _init();
    if (first_call)
        _load_vector_defaults();

    return _resolve_config(id, type, opts);
}

int
sol_conffile_resolve_path(const char *id, const char **type, const char ***opts, const char *path)
{
    _init();
    _fill_vector(path, NULL);
    return _resolve_config(id, type, opts);
}
