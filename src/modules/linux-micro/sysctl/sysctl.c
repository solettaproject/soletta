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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-sysctl");

#include "sol-platform-linux-micro.h"
#include "sol-util.h"
#include "sol-vector.h"

static int
sysctl_apply_value(int psfd, char *key, const char *value)
{
    char *p;
    int fd;
    size_t value_len;

    if (!*key || *key == '.')
        return -ENOENT;
    if (!*value)
        return -EINVAL;

    for (p = key; *p; p++) {
        if (*p == '.')
            *p = '/';
        else if (!(isalnum(*p) || *p == '_' || *p == '-'))
            return -EINVAL;
    }

    fd = openat(psfd, key, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        SOL_WRN("Unknown sysctl: %s (%s)", key, sol_util_strerrora(errno));
        return -errno;
    }

    value_len = strlen(value);
    if (write(fd, value, value_len) != (ssize_t)value_len) {
        SOL_WRN("Could not apply sysctl: %s=%s (%s)", key, value, sol_util_strerrora(errno));
        close(fd);
        return -errno;
    }

    close(fd);
    return 0;
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
sysctl_apply_file(int psfd, int fd)
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
        char *key, *value;
        int ret;

        key = remove_leading_whitespace(buf);
        if (*key == '#' || *key == ';' || *key == '\0')
            continue;

        value = strchr(key, '=');
        if (!value) {
            SOL_ERR("Line %d has no '=' delimiter", line_no);
            goto out;
        } else {
            *value = '\0';
            value = remove_leading_whitespace(value + 1);
        }

        remove_trailing_whitespace(key);
        remove_trailing_whitespace(value);

        ret = sysctl_apply_value(psfd, key, value);
        if (ret < 0)
            err = ret;

        line_no++;
    }

out:
    fclose(conf);
    return err;
}

static int
sysctl_apply_filename(int psfd, int dfd, const char *file_name)
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

    /* fd is closed by sysctl_apply_file() */
    return sysctl_apply_file(psfd, fd);
}

static int
sysctl_conf_cmp(const void *data1, const void *data2)
{
    return strcmp((const char *)data1, (const char *)data2);
}

static void
read_directory_to_vector(DIR *dir, struct sol_ptr_vector *vec)
{
    struct dirent ent, *entbuf;
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

        sol_ptr_vector_insert_sorted(vec, s, sysctl_conf_cmp);
next:
        r = readdir_r(dir, &ent, &entbuf);
    }
}

static int
sysctl_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    static const char *dirs[] = {   /* Path list from sysctl.conf(5) */
        "/run/sysctl.d",
        "/etc/sysctl.d",
        "/usr/local/lib/sysctl.d",
        "/usr/lib/sysctl.d",
        "/lib/sysctl.d",
        NULL
    };
    int i, ret, err = 0;
    int psfd;

    psfd = open("/proc/sys/", O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (psfd < 0) {
        SOL_WRN("/proc/sys not mounted or not a directory");
        return -errno;
    }

    ret = sysctl_apply_filename(psfd, -1, "/etc/sysctl.conf");
    if (ret < 0)
        err = ret;

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
            ret = sysctl_apply_filename(psfd, dfd, iter);
            if (ret < 0)
                err = ret;
            free(iter);
        }
        sol_ptr_vector_clear(&conf_files);

next_dir:
        closedir(dir);
    }

    close(psfd);
    return err;
}

static int
sysctl_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

SOL_PLATFORM_LINUX_MICRO_MODULE(SYSCTL,
    .name = "sysctl",
    .init = sysctl_init,
    .start = sysctl_start,
    );
