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

#include <libkmod.h>
#include <stdio.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-kmod");

#include "sol-platform-linux-micro.h"
#include "sol-util.h"
#include "sol-vector.h"

static int
kmod_apply_value(struct kmod_ctx *ctx, char *mod_name)
{
    const int probe_flags = KMOD_PROBE_APPLY_BLACKLIST;
    struct kmod_list *itr, *modlist = NULL;
    int r = 0;

    SOL_DBG("load: %s", mod_name);

    r = kmod_module_new_from_lookup(ctx, mod_name, &modlist);
    if (r < 0) {
        SOL_ERR("Failed to lookup alias '%s': %m", mod_name);
        return r;
    }

    if (!modlist) {
        SOL_ERR("Failed to find module '%s'", mod_name);
        return -ENOENT;
    }

    kmod_list_foreach(itr, modlist) {
        struct kmod_module *mod;
        int state, err;

        mod = kmod_module_get_module(itr);
        state = kmod_module_get_initstate(mod);

        switch (state) {
        case KMOD_MODULE_BUILTIN:
            SOL_INF("Module '%s' is builtin", kmod_module_get_name(mod));
            break;

        case KMOD_MODULE_LIVE:
            SOL_DBG("Module '%s' is already loaded", kmod_module_get_name(mod));
            break;

        default:
            err = kmod_module_probe_insert_module(mod, probe_flags,
                NULL, NULL, NULL, NULL);

            if (err == 0)
                SOL_INF("Inserted module '%s'", kmod_module_get_name(mod));
            else if (err == KMOD_PROBE_APPLY_BLACKLIST)
                SOL_INF("Module '%s' is blacklisted", kmod_module_get_name(mod));
            else {
                SOL_ERR("Failed to insert '%s': %m", kmod_module_get_name(mod));
                r = err;
            }
        }

        kmod_module_unref(mod);
    }

    kmod_module_unref_list(modlist);
    return r;
}

static char *
remove_leading_whitespace(char *input)
{
    while (isspace(*input))
        input++;
    return input;
}

static void
remove_trailing_whitespace(char *input)
{
    char *ptr = input + strlen(input) - 1;

    while (ptr > input && isspace(*ptr)) {
        *ptr = '\0';
        ptr--;
    }
}

static int
kmod_apply_file(struct kmod_ctx *ctx, int fd)
{
    FILE *conf;
    char buf[1024];
    int err = 0, line_no = 1;

    conf = fdopen(fd, "re");
    if (!conf) {
        close(fd);
        return -errno;
    }

    while (fgets(buf, sizeof(buf), conf)) {
        char *mod_name;
        int ret;

        mod_name = remove_leading_whitespace(buf);
        if (*mod_name == '#' || *mod_name == ';' || *mod_name == '\0')
            continue;

        remove_trailing_whitespace(mod_name);
        ret = kmod_apply_value(ctx, mod_name);
        if (ret < 0)
            err = ret;

        line_no++;
    }

    fclose(conf);
    return err;
}

static int
kmod_apply_filename(struct kmod_ctx *ctx, int dfd, const char *file_name)
{
    int fd;

    if (*file_name == '/' || dfd < 0)
        fd = open(file_name, O_RDONLY | O_CLOEXEC);
    else
        fd = openat(dfd, file_name, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        SOL_DBG("Could not open: %s", file_name);
        return 0;
    }

    return kmod_apply_file(ctx, fd);
}

static int
kmod_conf_cmp(const void *data1, const void *data2)
{
    return strcmp((const char *)data1, (const char *)data2);
}

static void
read_directory_to_vector(DIR *dir, struct sol_ptr_vector *vec)
{
    struct dirent ent, *entbuf = NULL;
    int r;

    r = readdir_r(dir, &ent, &entbuf);
    while (r == 0 && entbuf) {
        char *s, *dot = strrchr(ent.d_name, '.');

        if (!dot || !streq(dot, ".conf")) {
            SOL_DBG("File name does not end in '.conf', ignoring: %s", ent.d_name);
            goto next_dir;
        }

        s = strdup(ent.d_name);
        SOL_NULL_CHECK(s);

        sol_ptr_vector_insert_sorted(vec, s, kmod_conf_cmp);

next_dir:
        r = readdir_r(dir, &ent, &entbuf);
    }
}

static int
kmod_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    int i, ret, err = 0;
    struct kmod_ctx *ctx;

    static const char *dirs[] = {
        "/usr/lib/modules-load.d",
        "/run/modules-load.d",
        "/etc/modules-load.d",
        NULL
    };

    ctx = kmod_new(NULL, NULL);
    if (!ctx) {
        SOL_ERR("Failed to allocate memory for kmod.");
        return EXIT_FAILURE;
    }

    if (kmod_load_resources(ctx) < 0) {
        SOL_ERR("Failed to load kmod's resources: %m");
        goto finish;
    }

    for (i = 0; dirs[i]; i++) {
        DIR *dir;
        int dfd;
        struct sol_ptr_vector conf_files;
        char *iter;
        uint16_t idx;

        dir = opendir(dirs[i]);
        if (!dir)
            continue;

        dfd = dirfd(dir);
        if (dfd < 0) {
            SOL_DBG("Could not obtain directory file descriptor");
            goto next_dir;
        }

        sol_ptr_vector_init(&conf_files);
        read_directory_to_vector(dir, &conf_files);
        SOL_PTR_VECTOR_FOREACH_IDX (&conf_files, iter, idx) {
            ret = kmod_apply_filename(ctx, dfd, iter);
            if (ret < 0)
                err = ret;
            free(iter);
        }
        sol_ptr_vector_clear(&conf_files);

next_dir:
        closedir(dir);
    }

finish:
    kmod_unref(ctx);
    return err;
}

static int
kmod_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

SOL_PLATFORM_LINUX_MICRO_MODULE(KMOD,
    .name = "kmod",
    .init = kmod_init,
    .start = kmod_start,
    );
