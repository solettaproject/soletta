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

#include <alloca.h>
#include <errno.h>
#include <mntent.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-fstab");

#include "sol-platform-linux-micro.h"
#include "sol-util.h"
#include "sol-vector.h"

static void
join_unknown_mnt_opts(struct sol_ptr_vector *vec, char *to)
{
    char *ptr = to, *p;
    uint16_t i;

    if (sol_ptr_vector_get_len(vec) == 0) {
        *ptr = '\0';
        return;
    }

    SOL_PTR_VECTOR_FOREACH_IDX (vec, p, i) {
        ptr = mempcpy(ptr, p, strlen(p));
        *ptr++ = ',';
    }
    *(ptr - 1) = '\0'; /* remove last , */
}

static unsigned long
get_mountflags(char *mnt_opts, bool *should_mount)
{
    char *p, *remaining;
    struct sol_ptr_vector specific_opts;
    unsigned long options = 0;

    sol_ptr_vector_init(&specific_opts);

    remaining = strdupa(mnt_opts);

    while (remaining) {
        p = strchr(remaining, ',');
        if (p) {
            *p = '\0';
            p++;
        }

        if (streq(remaining, "bind")) {
            options |= MS_BIND;
        } else if (streq(remaining, "dirsync")) {
            options |= MS_DIRSYNC;
        } else if (streq(remaining, "mand")) {
            options |= MS_MANDLOCK;
        } else if (streq(remaining, "move")) {
            options |= MS_MOVE;
        } else if (streq(remaining, "noatime")) {
            options |= MS_NOATIME;
        } else if (streq(remaining, "nodev")) {
            options |= MS_NODEV;
        } else if (streq(remaining, "nodiratime")) {
            options |= MS_NODIRATIME;
        } else if (streq(remaining, "noexec")) {
            options |= MS_NOEXEC;
        } else if (streq(remaining, "nosuid")) {
            options |= MS_NOSUID;
        } else if (streq(remaining, "ro")) {
            options |= MS_RDONLY;
        } else if (streq(remaining, "relatime")) {
            options |= MS_RELATIME;
        } else if (streq(remaining, "remount")) {
            options |= MS_REMOUNT;
        } else if (streq(remaining, "silent")) {
            options |= MS_SILENT;
        } else if (streq(remaining, "strictatime")) {
            options |= MS_STRICTATIME;
        } else if (streq(remaining, "sync")) {
            options |= MS_SYNCHRONOUS;
        } else if (streq(remaining, "defaults")) {
            /* defaults: rw, suid, dev, exec, auto, nouser, and async */
            options &= ~MS_RDONLY;
            options &= ~MS_NOSUID;
            options &= ~MS_NODEV;
            options &= ~MS_NOEXEC;
            options &= ~MS_SYNCHRONOUS;
        } else if (streq(remaining, "noauto")) {
            *should_mount = false;
            sol_ptr_vector_clear(&specific_opts);
            return 0;
        } else {
            sol_ptr_vector_append(&specific_opts, remaining);
        }

        if (!p)
            break;

        remaining = p;
    }

    join_unknown_mnt_opts(&specific_opts, mnt_opts);

    sol_ptr_vector_clear(&specific_opts);

    return options;
}

static int
fstab_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    struct mntent *m;
    struct mntent mbuf;
    char strings[4096];
    int err = 0;
    FILE *fstab = setmntent("/etc/fstab", "re");

    if (!fstab) {
        if (errno == ENOENT) {
            SOL_INF("No /etc/fstab");
            return 0;
        } else {
            SOL_WRN("Unable to open /etc/fstab file: %s", sol_util_strerrora(-errno));
            return -errno;
        }
    }

    while ((m = getmntent_r(fstab, &mbuf, strings, sizeof(strings)))) {
        int ret;
        bool should_mount = true;
        unsigned long mountflags = get_mountflags(mbuf.mnt_opts, &should_mount);

        if (!should_mount)
            continue;

        ret = mount(mbuf.mnt_fsname, mbuf.mnt_dir, mbuf.mnt_type, mountflags, mbuf.mnt_opts);
        if (ret != 0) {
            SOL_WRN("Couldn't mount %s to %s", mbuf.mnt_fsname, mbuf.mnt_dir);
            err = ret;
            continue;
        }
    }

    sol_platform_linux_micro_inform_service_state(service,
        err == 0 ? SOL_PLATFORM_SERVICE_STATE_ACTIVE : SOL_PLATFORM_SERVICE_STATE_FAILED);

    endmntent(fstab);

    return err;
}

static int
fstab_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

SOL_PLATFORM_LINUX_MICRO_MODULE(FSTAB,
    .name = "fstab",
    .init = fstab_init,
    .start = fstab_start,
    );
