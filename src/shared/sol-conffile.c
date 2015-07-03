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
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "sol-conffile.h"
#include "sol-log.h"
#include "sol-util.h"
#include "sol-vector.h"

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

static struct sol_ptr_vector
sol_conffile_keyfile_to_vector(GKeyFile *keyfile)
{
    struct sol_ptr_vector pv;
    gchar **groups, **groups_ptr;
    struct sol_conffile_entry *entry = NULL;

    sol_ptr_vector_init(&pv);

    groups = g_key_file_get_groups(keyfile, NULL);
    for (groups_ptr = groups; *groups_ptr; groups_ptr++) {
        entry = calloc(1, sizeof(*entry));
        SOL_NULL_CHECK(entry, pv);

        entry->id = strdup(*groups_ptr);

        entry->type = g_key_file_get_string(keyfile,
            *groups_ptr, "Type", NULL);

        if (!entry->type) {
            SOL_DBG("could not find mandatory 'Type' key on group [%s],"
                " skipping entry", *groups_ptr);
            free((void *)entry->id);
            free(entry);
            continue;
        }

        entry->options = (const char **)g_key_file_get_string_list(keyfile,
            *groups_ptr, "Options", NULL, NULL);

        sol_ptr_vector_insert_sorted(&pv, entry,
            sol_conffile_entry_sort_cb);
    }
    g_strfreev(groups);

    return pv;
}

static void
sol_conffile_get_keyfile_include_paths(GKeyFile *keyfile,
    char **include,
    char **include_fallbacks)
{
    const char *include_group = "SolettaInclude";
    GError *error = NULL;

    if (!g_key_file_has_group(keyfile, include_group))
        return;

    *include = g_key_file_get_string(keyfile,
        include_group, "Include", &error);
    if (*include == NULL && error
        && error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND) {
        SOL_DBG("could not read value from %s group: %s",
            include_group, error->message);
        g_error_free(error);
    }

    *include_fallbacks = g_key_file_get_string(keyfile, include_group,
        "IncludeFallbacks", &error);
    if (*include_fallbacks == NULL && error
        && error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND) {
        SOL_DBG("could not read value from %s group: %s",
            include_group, error->message);
        g_error_free(error);
    }

    if (!g_key_file_remove_group(keyfile, include_group, &error)) {
        SOL_DBG("could not remove %s group: %s", include_group,
            error ? error->message : "unknown error");
        g_error_free(error);
    }
}

static GKeyFile *
sol_conffile_load_keyfile_from_dirs(const char *file, char **full_path)
{
    char *cwd = NULL;
    GError *error = NULL;
    GKeyFile *keyfile = NULL;
    char cwdbuf[PATH_MAX + 1];
    const char *search_dirs[] = {
        NULL,
        ".",
        PKGSYSCONFDIR,
        NULL,
    };

    cwd = getcwd(cwdbuf, sizeof(cwdbuf));
    if (cwd)
        search_dirs[0] = cwd;
    else
        SOL_DBG("could not getcwd: %d", errno);

    keyfile = g_key_file_new();
    SOL_NULL_CHECK(keyfile, NULL);

    /* absolute path */
    if (g_path_is_absolute(file)) {
        if (!g_key_file_load_from_file(keyfile,
            file, G_KEY_FILE_NONE, &error))
            goto err;
    } else if (!g_key_file_load_from_dirs(keyfile, file, search_dirs,
        full_path, G_KEY_FILE_NONE,
        &error))
        goto err;

    return keyfile;

err:
    SOL_DBG("could not load file '%s' from dirs: %s",
        file, error ? error->message : "unknown error");
    g_error_free(error);
    g_key_file_free(keyfile);
    return NULL;
}

static GKeyFile *
sol_conffile_load_keyfile_from_paths(const char *path,
    const char *fallback_paths,
    char **full_path)
{
    gchar **ptr;
    gchar **splitted_paths;
    GKeyFile *keyfile = NULL;

    if (path)
        keyfile = sol_conffile_load_keyfile_from_dirs(path, full_path);

    if (fallback_paths && !keyfile) {
        splitted_paths = g_strsplit(fallback_paths, ";", -1);
        for (ptr = splitted_paths; *ptr; ptr++) {
            keyfile = sol_conffile_load_keyfile_from_dirs
                    (*ptr, full_path);
            if (keyfile)
                break;
        }
        g_strfreev(splitted_paths);
    }

    return keyfile;
}

static void
sol_conffile_free_entry_vector(struct sol_ptr_vector *pv)
{
    struct sol_conffile_entry *e;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (pv, e, i) {
        free((void *)e->id);
        g_free((void *)e->type);
        if (e->options)
            g_strfreev((gchar **)e->options);
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

    if (sol_ptr_vector_get_len(&pv) <= 0)
        return false;

    if (sol_ptr_vector_get_len(&sol_conffile_entry_vector) <= 0) {
        sol_conffile_entry_vector = pv;
        return true;
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&pv, pv_entry, i) {
        SOL_PTR_VECTOR_FOREACH_IDX (&sol_conffile_entry_vector,
            sol_conffile_entry_vector_entry, j) {
            if (streq(pv_entry->id,
                sol_conffile_entry_vector_entry->id)) {
                SOL_DBG("Ignoring entry [%s], as it already exists",
                    pv_entry->id);
                sol_conffile_free_entry_vector(&pv);
                return false;
            }
        }
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&pv, pv_entry, i) {
        sol_ptr_vector_insert_sorted(&sol_conffile_entry_vector,
            pv_entry, sol_conffile_entry_sort_cb);
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
    GKeyFile *keyfile = NULL;
    struct sol_ptr_vector pv;

    keyfile = sol_conffile_load_keyfile_from_paths
            (path, fallback_paths, &full_path);
    if (!keyfile)
        return;

    sol_conffile_get_keyfile_include_paths
        (keyfile, &include, &include_fallbacks);

    pv = sol_conffile_keyfile_to_vector(keyfile);
    g_key_file_free(keyfile);

    if (!sol_conffile_append_to_entry_vector(pv)) {
        SOL_DBG("Ignoring conffile %s", full_path);
        goto free_for_all;
    }

    if (include || include_fallbacks)
        sol_conffile_fill_vector(include, include_fallbacks);

free_for_all:
    g_free(full_path);
    g_free(include);
    g_free(include_fallbacks);
}

static void
sol_conffile_load_vector(void)
{
    sol_conffile_fill_vector(getenv("SOL_FLOW_MODULE_RESOLVER_CONFFILE"),
        "sol-flow.conf");
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
    sol_conffile_free_entry_vector(&sol_conffile_entry_vector);
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
    char entry_id[PATH_MAX];
    void **vector_pointer;
    int r;

    if (sol_ptr_vector_get_len(&sol_conffile_entry_vector) <= 0)
        return -EINVAL;

    r = snprintf(entry_id, sizeof(entry_id), "SolettaNodeEntry %s", id);
    if (r < 0 || r >= (int)sizeof(entry_id))
        return -EINVAL;

    key.id = entry_id;
    vector_pointer = bsearch(&key,
        sol_conffile_entry_vector.base.data,
        sol_ptr_vector_get_len(&sol_conffile_entry_vector),
        sol_conffile_entry_vector.base.elem_size,
        sol_conffile_bsearch_entry_sort_cb);
    if (!vector_pointer) {
        SOL_DBG("could not find entry [%s]", entry_id);
        return -EINVAL;
    }

    entry = *vector_pointer;
    if (!entry) {
        SOL_DBG("could not find entry [%s]", entry_id);
        return -EINVAL;
    }

    if (!entry->type) {
        SOL_DBG("could not find mandatory [%s] Type= key", entry_id);
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
