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
#include <sys/stat.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-kmod");

#include "sol-file-reader.h"
#include "sol-mainloop.h"
#include "sol-platform-linux-micro.h"
#include "sol-util.h"

struct kmod_data {
    struct kmod_ctx *kmod;
};

static struct kmod_data context;

static int
kmod_apply_value(struct kmod_ctx *kmod, struct sol_str_slice modalias)
{
    const int probe_flags = KMOD_PROBE_APPLY_BLACKLIST;
    struct kmod_list *itr, *modlist = NULL;
    int r = 0;
    char *alias = NULL;

    alias = sol_str_slice_to_string(modalias);
    SOL_NULL_CHECK(alias, NULL);

    SOL_INF("Trying to load module for alias: %s", alias);

    r = kmod_module_new_from_lookup(kmod, alias, &modlist);
    if (r < 0) {
        SOL_ERR("Failed to lookup alias '%s': %s", alias,
            sol_util_strerrora(errno));
        goto err;
    }

    if (!modlist) {
        SOL_WRN("No modules found for alias: '%s'", alias);
        r = -ENOENT;
        goto err;
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
            SOL_INF("Module '%s' is already loaded", kmod_module_get_name(mod));
            break;

        default:
            err = kmod_module_probe_insert_module(mod, probe_flags,
                NULL, NULL, NULL, NULL);

            if (err == 0)
                SOL_INF("Inserted module '%s'", kmod_module_get_name(mod));
            else if (err == KMOD_PROBE_APPLY_BLACKLIST)
                SOL_INF("Module '%s' is blacklisted", kmod_module_get_name(mod));
            else {
                SOL_WRN("Failed to insert '%s': %s", kmod_module_get_name(mod),
                    sol_util_strerrora(errno));
                r = err;
            }
        }
    }

    kmod_module_unref_list(modlist);
err:
    free(alias);
    return r;
}

static int
kmod_apply_file(struct kmod_ctx *kmod, int fd)
{
    int err = 0, ret;
    struct sol_file_reader *reader;
    struct sol_str_slice content = SOL_STR_SLICE_EMPTY;
    struct sol_str_slice *itr;
    uint16_t idx;
    struct sol_vector lines;

    reader = sol_file_reader_from_fd(fd);
    if (!reader) {
        SOL_ERR("Could not open config file");
        return err;
    }

    content = sol_file_reader_get_all(reader);
    if (content.len == 0)
        goto finish;

    lines = sol_util_str_split(content, "\n", 0);
    SOL_VECTOR_FOREACH_IDX (&lines, itr, idx) {
        struct sol_str_slice slice;

        slice = sol_str_slice_trim(*itr);
        if (*slice.data == '#' || *slice.data == ';' || *slice.data == '\0')
            continue;

        ret = kmod_apply_value(kmod, slice);
        if (ret < 0)
            err = ret;
    }
    sol_vector_clear(&lines);

finish:
    sol_file_reader_close(reader);
    return err;
}

static int
kmod_apply_filename(struct kmod_ctx *kmod, int dfd, const char *file_name)
{
    int fd, res;

    if (*file_name == '/' || dfd < 0)
        fd = open(file_name, O_RDONLY | O_CLOEXEC);
    else
        fd = openat(dfd, file_name, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        SOL_DBG("Could not open: %s", file_name);
        return 0;
    }

    res = kmod_apply_file(kmod, fd);
    close(fd);
    return res;
}

static int
kmod_conf_cmp(const void *data1, const void *data2)
{
    return strcmp((const char *)data1, (const char *)data2);
}

static int
read_directory_to_vector(DIR *dir, struct sol_ptr_vector *vec)
{
    struct dirent ent, *entbuf = NULL;
    char *itr;
    uint16_t i;
    int r;

    r = readdir_r(dir, &ent, &entbuf);
    while (r == 0 && entbuf) {
        char *s, *dot = strrchr(ent.d_name, '.');

        if (!dot || !streq(dot, ".conf")) {
            SOL_DBG("File name does not end in '.conf', ignoring: %s",
                ent.d_name);
            goto next;
        }

        s = strdup(ent.d_name);
        if (!s) {
            SOL_WRN("Could not allocate memory to hold dir entry: %s",
                ent.d_name);
            goto str_err;
        }

        sol_ptr_vector_insert_sorted(vec, s, kmod_conf_cmp);

next:
        r = readdir_r(dir, &ent, &entbuf);
    }

    return 0;

str_err:
    SOL_PTR_VECTOR_FOREACH_IDX (vec, itr, i) {
        free(itr);
    }
    sol_ptr_vector_clear(vec);
    return -ENOMEM;
}

static void
kmod_coldplug_find_devices(struct kmod_data *kdata, DIR *dir)
{
    struct dirent ent, *entbuf = NULL;
    int r, dfd;

    dfd = dirfd(dir);
    if (dfd < 0) {
        SOL_DBG("Could not obtain directory file descriptor");
        return;
    }

    r = readdir_r(dir, &ent, &entbuf);
    while (r == 0 && entbuf) {
        struct stat st;

        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) {
            goto next;
        }

        if (fstatat(dfd, ent.d_name, &st, AT_SYMLINK_NOFOLLOW) == -1) {
            SOL_ERR("Could not stat %s", ent.d_name);
            goto next;
        }

        if (S_ISDIR(st.st_mode)) {
            int nfd;
            DIR *ndir;

            nfd = openat(dfd, ent.d_name, O_RDONLY | O_CLOEXEC);
            if (nfd < 0) {
                SOL_ERR("Could not open sys directory %s - %s", ent.d_name,
                    sol_util_strerrora(errno));
                goto next;
            }

            ndir = fdopendir(nfd);
            if (!ndir) {
                SOL_ERR("Could not open sys directory %s", ent.d_name);
                close(nfd);
                goto next;
            }

            kmod_coldplug_find_devices(kdata, ndir);
            closedir(ndir);
        } else if (streq(ent.d_name, "modalias")) {
            int fd;
            char buff[PATH_MAX];
            size_t len;
            struct sol_str_slice slice;

            fd = openat(dfd, ent.d_name, O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                SOL_ERR("Could not obtain uevent file descriptor");
                goto next;
            }

            len = read(fd, buff, sizeof(buff));
            if (len > 0) {
                SOL_DBG("loading kernel module for alias: %s", buff);
                slice = SOL_STR_SLICE_STR(buff, len);
                kmod_apply_value(kdata->kmod, slice);
            }

            close(fd);
        }

next:
        r = readdir_r(dir, &ent, &entbuf);
    }
}

static int
kmod_start_coldplug(struct kmod_data *kdata)
{
    DIR *dir;

    dir = opendir("/sys/devices");
    if (!dir) {
        SOL_ERR("Could not open /sys/devices directory: coldplug unavailable");
        return -errno;
    }
    kmod_coldplug_find_devices(kdata, dir);
    closedir(dir);
    return 0;
}

static int
kmod_settings_apply(struct kmod_data *kdata)
{
    int i, ret, err = 0;
    static const char *dirs[] = {
        "/usr/lib/modules-load.d",
        "/run/modules-load.d",
        "/etc/modules-load.d",
        NULL
    };

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
        err = read_directory_to_vector(dir, &conf_files);
        SOL_INT_CHECK_GOTO(err, < 0, next_dir);

        SOL_PTR_VECTOR_FOREACH_IDX (&conf_files, iter, idx) {
            ret = kmod_apply_filename(kdata->kmod, dfd, iter);
            if (ret < 0)
                err = ret;
            free(iter);
        }
        sol_ptr_vector_clear(&conf_files);

next_dir:
        closedir(dir);
    }

    return err;
}

static void
uevent_cb(void *data, struct sol_uevent *uevent)
{
    struct kmod_data *kdata = data;

    if (!uevent->modalias.len) {
        SOL_DBG("No modalias given, skipping");
        return;
    }

    kmod_apply_value(kdata->kmod, uevent->modalias);
}

static int
kmod_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    int err;

    err = sol_platform_linux_uevent_subscribe("add", NULL, uevent_cb, &context);
    if (err < 0)
        return err;

    err = kmod_start_coldplug(&context);
    if (err < 0)
        return err;

    return kmod_settings_apply(&context);
}

static int
kmod_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;

    context.kmod = kmod_new(NULL, NULL);
    if (!context.kmod) {
        SOL_ERR("Failed to allocate memory for kmod.");
        return -ENOMEM;
    }

    if (kmod_load_resources(context.kmod) < 0) {
        SOL_ERR("Failed to load kmod's resources: %s",
            sol_util_strerrora(errno));
        goto finish;
    }

    return 0;

finish:
    kmod_unref(context.kmod);
    return -ENOMEM;
}

static void
kmod_shutdown(const struct sol_platform_linux_micro_module *module, const char *service)
{
    kmod_unref(context.kmod);
    context.kmod = NULL;
    sol_platform_linux_uevent_unsubscribe("add", NULL, uevent_cb, NULL);
}

SOL_PLATFORM_LINUX_MICRO_MODULE(KMOD,
    .name = "kmod",
    .init = kmod_init,
    .shutdown = kmod_shutdown,
    .start = kmod_start,
    );
