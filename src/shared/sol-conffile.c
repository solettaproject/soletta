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
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#include "sol-conffile.h"
#include "sol-log.h"
#include "sol-util.h"
#include "sol-vector.h"
#include "sol-json.h"

static struct sol_ptr_vector sol_conffile_entry_vector;

struct sol_conffile_entry {
    const char *id;
    const char *type;
    const char **options;
};

static int
sol_conffile_entry_sort_cb(const void *data1, const void *data2)
{
    const struct sol_conffile_entry *e1 = data1;
    const struct sol_conffile_entry *e2 = data2;

    return strcasecmp(e1->id, e2->id);
}

static void
sol_conffile_set_entry_options(struct sol_conffile_entry *entry, struct sol_json_token options_object)
{
    int len;
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    struct sol_ptr_vector vec_options;
    enum sol_json_loop_reason reason;

    sol_ptr_vector_init(&vec_options);

    sol_json_scanner_init_from_token(&scanner, &options_object);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        int key_len, value_len;
        char *tmp;
        key_len = sol_json_token_get_size(&key) - 2; // remove quotes from the key.
        value_len = sol_json_token_get_size(&value); // do not remove quotes from the value. (not everything is a string here)
        len = key_len + value_len + 2; // plus = and \0
        tmp = malloc( len );
        snprintf(tmp, len, "%.*s=%.*s\0", key_len, key.start + 1, value_len, value.start);
        sol_ptr_vector_append(&vec_options, tmp);
    }
    entry->options = (const char **)vec_options.base.data;
}

static struct sol_ptr_vector
sol_conffile_keyfile_to_vector(struct sol_json_scanner scanner)
{
    struct sol_ptr_vector pv;
    struct sol_conffile_entry *entry = NULL;
    const char *node_group = "nodetypes";
    const char *node_name = "name";
    const char *node_type = "type";
    const char *node_options = "options";

    struct sol_json_token token, key, value;
    struct sol_json_scanner obj_scanner;
    enum sol_json_loop_reason reason;
    bool found_nodes = false;

    sol_ptr_vector_init(&pv);

    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (sol_json_token_str_eq(&key, node_group, strlen(node_group))) {
            found_nodes = true;
            break;
        }
    }

    if (!found_nodes)
        return pv;

    sol_json_scanner_init_from_token(&obj_scanner, &value);
    SOL_JSON_SCANNER_ARRAY_LOOP (&obj_scanner, &token, SOL_JSON_TYPE_OBJECT_START, reason) {
        entry = calloc(1, sizeof(*entry));
        SOL_NULL_CHECK(entry, pv);
        SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&obj_scanner, &token, &key, &value, reason) {

    #define DUP_JSON_STR( JSON_VALUE ) \
    sol_json_token_get_size(&JSON_VALUE) >= 2 ? \
    strndup(JSON_VALUE.start + 1, sol_json_token_get_size(&JSON_VALUE) - 2 ) : NULL

            if (sol_json_token_str_eq(&key, node_name, strlen(node_name))) {
                entry->id = DUP_JSON_STR(value);
            }

            if (sol_json_token_str_eq(&key, node_type, strlen(node_type))) {
                entry->type = DUP_JSON_STR(value);
            }

            if (sol_json_token_str_eq(&key, node_options, strlen(node_options))) {
                sol_conffile_set_entry_options(entry, value);
            }

            #undef DUP_JSON_STR
        }
        sol_ptr_vector_insert_sorted(&pv, entry, sol_conffile_entry_sort_cb);
    }
    return pv;
}

static void
sol_conffile_get_keyfile_include_paths(
    struct sol_json_scanner json_scanner,
    char **include,
    char **include_fallbacks)
{
    const char *include_group = "SolettaInclude";
    const char *include_str = "Include";
    const char *include_fallback = "IncludeFallbacks";

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

    if (!found_include)
        return;

    sol_json_scanner_init_from_token(&include_scanner, &value);
    SOL_JSON_SCANNER_OBJECT_LOOP (&include_scanner, &token, &key, &value, reason) {
        if (sol_json_token_str_eq(&key, include_str, strlen(include_str))) {
            *include = strndup(value.start, sol_json_token_get_size(&value));
            if (!*include)
                SOL_DBG("Error: couldn't allocate memory for string.");
            continue;
        }
        if (sol_json_token_str_eq(&key, include_fallback, strlen(include_fallback))) {
            *include_fallbacks = strndup(value.start, sol_json_token_get_size(&value));
            if (!*include_fallbacks)
                SOL_DBG("Error: couldn't allocate memory for string.");
            continue;
        }
    }
}

static char *
sol_conffile_load_keyfile_from_dirs(const char *file, char **full_path)
{
    int i;
    char *config_file_contents;
    const char *search_dirs[] = {
        NULL,
        ".",
        PKGSYSCONFDIR,
        NULL,
    };
    int search_dirs_size;

    search_dirs_size = ARRAY_SIZE(search_dirs);

    /* absolute path */
    if (file[0] == '/') {
        config_file_contents = sol_util_load_file_string(file, NULL);
    } else {
        for (i = 0; i < search_dirs_size; i++) {
            char filename[PATH_MAX];
            size_t size;
            sprintf(filename, "%s/%s", search_dirs[i], file);
            config_file_contents = sol_util_load_file_string(file, &size);
            if (strlen(config_file_contents) != 0)
                break;
        }
    }

    if (!config_file_contents)
        SOL_DBG("could not load file '%s' from dirs: unknown error");

    return config_file_contents;
}

static char *
sol_conffile_load_keyfile_from_paths(const char *path,
    const char *fallback_paths,
    char **full_path)
{
    char *config_file_contents = NULL;

    if (path)
        config_file_contents = sol_conffile_load_keyfile_from_dirs(path, full_path);

    if (fallback_paths && !config_file_contents) {
        int idx;
        struct sol_str_slice *s_ptr;
        struct sol_vector str_splitted = sol_util_str_split(sol_str_slice_from_str(fallback_paths), ";", 0);
        SOL_VECTOR_FOREACH_IDX (&str_splitted, s_ptr, idx) {
            config_file_contents = sol_conffile_load_keyfile_from_dirs(s_ptr->data, full_path);
            if (config_file_contents)
                break;
        }
        sol_vector_clear(&str_splitted);
    }
    return config_file_contents;
}

static void
sol_conffile_free_entry_vector(struct sol_ptr_vector *pv)
{
    struct sol_conffile_entry *e;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (pv, e, i) {
        free((void *)e->id);
        free((void *)e->type);
        if (e->options)
            free((char **)e->options);
        free(e);
    }

    sol_ptr_vector_clear(pv);
}

static bool
sol_conffile_append_to_entry_vector(struct sol_ptr_vector pv)
{
    struct sol_conffile_entry *sol_conffile_entry_vector_entry;
    struct sol_conffile_entry *pv_entry;
    uint16_t i, j;

    if (sol_ptr_vector_get_len(&pv) <= 0) {
        return false;
    }

    if (sol_ptr_vector_get_len(&sol_conffile_entry_vector) <= 0) {
        sol_conffile_entry_vector = pv;
        return true;
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&pv, pv_entry, i) {
        SOL_PTR_VECTOR_FOREACH_IDX (&sol_conffile_entry_vector, sol_conffile_entry_vector_entry, j) {
            if (streq(pv_entry->id, sol_conffile_entry_vector_entry->id)) {
                SOL_WRN("Ignoring entry [%s], as it already exists", pv_entry->id);
                sol_conffile_free_entry_vector(&pv);
                return false;
            }
        }
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&pv, pv_entry, i) {
        sol_ptr_vector_insert_sorted(&sol_conffile_entry_vector, pv_entry, sol_conffile_entry_sort_cb);
    }

    sol_ptr_vector_clear(&pv);
    return true;
}

static void
sol_conffile_fill_vector(const char *path, const char *fallback_paths)
{
    char *full_path = NULL;
    char *include = NULL;
    char *include_fallbacks = NULL;
    char *config_file_contents = NULL;

    struct sol_ptr_vector pv;
    struct sol_json_scanner json_scanner;

    config_file_contents = sol_conffile_load_keyfile_from_paths(path, fallback_paths, &full_path);
    if (!config_file_contents)
        return;

    sol_json_scanner_init(&json_scanner, config_file_contents, strlen(config_file_contents));
    sol_conffile_get_keyfile_include_paths(json_scanner, &include, &include_fallbacks);

    pv = sol_conffile_keyfile_to_vector(json_scanner);
    free(config_file_contents);

    if (!sol_conffile_append_to_entry_vector(pv)) {
        SOL_DBG("Ignoring conffile %s", full_path);
        goto free_for_all;
    }

    if (include || include_fallbacks)
        sol_conffile_fill_vector(include, include_fallbacks);

free_for_all:
    free(full_path);
    free(include);
    free(include_fallbacks);
}

static void
sol_conffile_load_vector(void)
{
    sol_conffile_fill_vector(getenv("SOL_FLOW_MODULE_RESOLVER_CONFFILE"),
        "sol-flow.json");
    /*
     * TODO: Add the following fill priority:
     * 1. Envvar
     * 2. Arg0
     * 3. Platform
     * 4. Common/Fallback
     */
}

static void
sol_conffile_clear_data(void)
{
    //sol_conffile_free_entry_vector(&sol_conffile_entry_vector);
}

static int
sol_conffile_bsearch_entry_sort_cb(const void *data1, const void *data2)
{
    /* sol_ptr_vector holds a vector of pointers rather than a vector of data. */
    const void **p = (const void **)data2;

    return sol_conffile_entry_sort_cb(data1, *p);
}

static int
_sol_conffile_resolve(const char *id, const char **type, const char ***opts)
{
    struct sol_conffile_entry key;
    struct sol_conffile_entry *entry;
    void **vector_pointer;
    int r;

    if (sol_ptr_vector_get_len(&sol_conffile_entry_vector) <= 0) {
        return -EINVAL;
    }

    key.id = id;
    vector_pointer = bsearch(&key,
        sol_conffile_entry_vector.base.data,
        sol_ptr_vector_get_len(&sol_conffile_entry_vector),
        sol_conffile_entry_vector.base.elem_size,
        sol_conffile_bsearch_entry_sort_cb);
    if (!vector_pointer) {
        SOL_DBG("could not find entry [%s]", id);
        return -EINVAL;
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
    *opts = entry->options;

    return 0;
}

int
sol_conffile_resolve(const char *id, const char **type, const char ***opts)
{
    if (sol_ptr_vector_get_len(&sol_conffile_entry_vector) > 0)
        sol_conffile_clear_data();

    sol_conffile_load_vector();
    atexit(sol_conffile_clear_data);

    return _sol_conffile_resolve(id, type, opts);
}

int
sol_conffile_resolve_path(const char *id, const char **type, const char ***opts, const char *path)
{
    if (sol_ptr_vector_get_len(&sol_conffile_entry_vector) > 0)
        sol_conffile_clear_data();

    sol_conffile_fill_vector(path, NULL);
    atexit(sol_conffile_clear_data);

    return _sol_conffile_resolve(id, type, opts);
}
