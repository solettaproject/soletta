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
#include <linux/netlink.h>
#include <stdio.h>
#include <sys/socket.h>
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
    struct {
        int fd;
        struct sol_fd *watch;
    } uevent;
};

static struct kmod_data context;

static int
kmod_apply_value(struct kmod_ctx *kmod, char *alias)
{
    const int probe_flags = KMOD_PROBE_APPLY_BLACKLIST;
    struct kmod_list *itr, *modlist = NULL;
    int r = 0;

    SOL_DBG("Trying to load module for alias: %s", alias);

    r = kmod_module_new_from_lookup(kmod, alias, &modlist);
    if (r < 0) {
        SOL_ERR("Failed to lookup alias '%s': %s", alias,
            sol_util_strerrora(errno));
        return r;
    }

    if (!modlist) {
        SOL_INF("No modules found for alias: '%s'", alias);
        return -ENOENT;
    }

    kmod_list_foreach(itr, modlist) {
        struct kmod_module *mod;
        int state, err;

        mod = kmod_module_get_module(itr);
        state = kmod_module_get_initstate(mod);

        switch (state) {
        case KMOD_MODULE_BUILTIN:
            SOL_DBG("Module '%s' is builtin", kmod_module_get_name(mod));
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
                SOL_WRN("Failed to insert '%s': %s", kmod_module_get_name(mod),
                    sol_util_strerrora(errno));
                r = err;
            }
        }

        kmod_module_unref(mod);
    }

    kmod_module_unref_list(modlist);
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

    reader = sol_file_reader_fd(fd);
    if (!reader) {
        SOL_ERR("Could not open config file");
        return err;
    }

    content = sol_file_reader_get_all(reader);
    if (content.len == 0)
        goto finish;

    lines = sol_util_str_split(content, "\n", 0);
    SOL_VECTOR_FOREACH_IDX (&lines, itr, idx) {
        char *alias = NULL;

        sol_str_slice_remove_leading_whitespace(itr);
        if (*itr->data == '#' || *itr->data == ';' || *itr->data == '\0')
            continue;

        sol_str_slice_remove_trailing_whitespace(itr);
        alias = strndupa(itr->data, itr->len);
        if (!alias)
            continue;

        ret = kmod_apply_value(kmod, alias);
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
    int fd;

    if (*file_name == '/' || dfd < 0)
        fd = open(file_name, O_RDONLY | O_CLOEXEC);
    else
        fd = openat(dfd, file_name, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        SOL_DBG("Could not open: %s", file_name);
        return 0;
    }

    return kmod_apply_file(kmod, fd);
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
            SOL_DBG("File name does not end in '.conf', ignoring: %s",
                ent.d_name);
            goto next;
        }

        s = strdup(ent.d_name);
        if (!s) {
            SOL_WRN("Could not allocate memory to hold dir entry: %s, skipping.",
                ent.d_name);
            goto next;
        }

        sol_ptr_vector_insert_sorted(vec, s, kmod_conf_cmp);

next:
        r = readdir_r(dir, &ent, &entbuf);
    }
}

static void
uevent_read_msg(struct kmod_data *kdata, char *msg, int len)
{
    char *modalias = NULL, *action = NULL;
    int i = 0;

    while (i < len) {
        char *curr, *del;
        size_t value_len;

        curr = msg + i;
        del = strrchr(curr, '=');
        value_len = del - curr;

        if (del && !strncmp(curr, "MODALIAS", value_len))
            modalias = strdupa(++del);
        else if (del && !strncmp(curr, "ACTION", value_len))
            action = strdupa(++del);

        i += strlen(msg + i) + 1;
    }

    if ((action && modalias) && !strcmp(action, "add")) {
        kmod_apply_value(kdata->kmod, modalias);
    }
}

static bool
kmod_uevent_handler(void *data, int fd, unsigned int cond)
{
    int len;
    struct kmod_data *kdata = data;
    char buffer[512];

    len = recv(kdata->uevent.fd, buffer, sizeof(buffer), MSG_WAITALL);
    if (len == -1) {
        SOL_ERR("Could not read netlink socket. %s", sol_util_strerrora(errno));
        return false;
    } else {
        uevent_read_msg(kdata, buffer, len);
    }

    return true;
}

static int
kmod_uevent_start(struct kmod_data *kdata)
{
    struct sockaddr_nl nls;

    memset(&nls, 0, sizeof(struct sockaddr_nl));
    nls.nl_family = AF_NETLINK;
    nls.nl_pid = getpid();
    nls.nl_groups = -1;

    kdata->uevent.fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (kdata->uevent.fd == -1) {
        SOL_ERR("Could not open uevent netlink socket.");
        return -errno;
    }

    if (bind(kdata->uevent.fd, (void *)&nls, sizeof(struct sockaddr_nl))) {
        SOL_ERR("Could not bind to uevent socket.");
        close(kdata->uevent.fd);
        kdata->uevent.fd = 0;
        return -errno;
    }

    kdata->uevent.watch = sol_fd_add(kdata->uevent.fd, SOL_FD_FLAGS_IN,
        kmod_uevent_handler, kdata);
    return 0;
}

static void
kmod_coldplug_find_devices(struct kmod_data *kdata, DIR *dir)
{
    struct dirent ent, *entbuf = NULL;
    int r, dfd;

    dfd = dirfd(dir);
    if (dfd < 0) {
        SOL_DBG("Could not obtain directory file descriptor");
        goto next;
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

            nfd = openat(dfd, ent.d_name, O_RDONLY);
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
        } else if (!strcmp(ent.d_name, "modalias")) {
            int fd;
            char buff[PATH_MAX];

            fd = openat(dfd, ent.d_name, O_RDONLY);
            if (fd < 0) {
                SOL_ERR("Could not obtain uevent file descriptor");
                goto next;
            }

            if (read(fd, buff, sizeof(buff)) > 0) {
                SOL_DBG("loading kernel module for alias: %s", buff);
                kmod_apply_value(kdata->kmod, buff);
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
        SOL_ERR("Could not run dir");
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
        read_directory_to_vector(dir, &conf_files);
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

static int
kmod_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    int err;

    err = kmod_uevent_start(&context);
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

    if (context.uevent.watch)
        sol_fd_del(context.uevent.watch);

    if (context.uevent.fd)
        close(context.uevent.fd);
}

SOL_PLATFORM_LINUX_MICRO_MODULE(KMOD,
    .name = "kmod",
    .init = kmod_init,
    .shutdown = kmod_shutdown,
    .start = kmod_start,
    );
