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
#include <fcntl.h>
#include <linux/netlink.h>
#include <mntent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sol-file-reader.h"
#include "sol-mainloop.h"
#include "sol-platform-impl.h"
#include "sol-platform-linux.h"
#include "sol-platform.h"
#include "sol-util.h"
#include "sol-vector.h"

#define SOL_MTAB_FILE P_tmpdir "/mtab.sol"
#define SYS_MTAB_FILE "/etc/mtab"
#define LIBUDEV_ID "libudev"

struct sol_platform_linux_fork_run {
    pid_t pid;
    void (*on_child_exit)(void *data, uint64_t pid, int status);
    const void *data;
    struct sol_child_watch *watch;
};

struct umount_data {
    void *data;
    char *mpoint;
    void (*async_cb)(const char *mpoint, void *data, uint64_t pid, int status);
};

struct mount_data {
    void *data;
    char *dev;
    char *mpoint;
    char *fstype;
    char *opts;
    unsigned long flags;
    void (*async_cb)(const char *dev, const char *mpoint, void *data, uint64_t pid, int status);
};

struct uevent_callback {
    const char *action;
    const char *subsystem;
    const void *cb_data;
    void (*uevent_cb)(void *cb_data, struct sol_uevent *uevent);
};

struct uevent_context {
    bool running;
    struct sol_vector callbacks;
    struct {
        int fd;
        struct sol_fd *watch;
    } uevent;
};

static struct uevent_context uevent_ctx;
static struct sol_ptr_vector fork_runs = SOL_PTR_VECTOR_INIT;

static uint16_t
find_handle(const struct sol_platform_linux_fork_run *handle)
{
    struct sol_platform_linux_fork_run *itr = NULL;
    uint16_t i;

    errno = ENOENT;
    SOL_NULL_CHECK(handle, UINT16_MAX);

    SOL_PTR_VECTOR_FOREACH_IDX (&fork_runs, itr, i) {
        if (itr == handle) {
            errno = 0;
            return i;
        }
    }

    return UINT16_MAX;
}

static void
on_child(void *data, uint64_t pid, int status)
{
    struct sol_platform_linux_fork_run *handle = data;
    uint16_t i;

    i = find_handle(handle);
    SOL_INT_CHECK(i, == UINT16_MAX);
    sol_ptr_vector_del(&fork_runs, i);

    if (handle->on_child_exit)
        handle->on_child_exit((void *)handle->data, pid, status);
    free(handle);
}

SOL_API struct sol_platform_linux_fork_run *
sol_platform_linux_fork_run(void (*on_fork)(void *data), void (*on_child_exit)(void *data, uint64_t pid, int status), const void *data)
{
    pid_t pid;
    int pfds[2];

    errno = EINVAL;
    SOL_NULL_CHECK(on_fork, NULL);

    if (pipe(pfds) < 0) {
        SOL_WRN("could not create pipe: %s", sol_util_strerrora(errno));
        return NULL;
    }

    pid = fork();
    if (pid == 0) {
        sigset_t emptyset;
        char msg;

        sigemptyset(&emptyset);
        sigprocmask(SIG_SETMASK, &emptyset, NULL);

        close(pfds[1]);
        while (read(pfds[0], &msg, 1) < 0) {
            if (errno == EINTR)
                continue;
            else {
                SOL_WRN("failed to read from pipe: %s", sol_util_strerrora(errno));
                close(pfds[0]);
                sol_platform_linux_fork_run_exit(EXIT_FAILURE);
            }
        }
        close(pfds[0]);

        errno = 0;
        on_fork((void *)data);
        sol_platform_linux_fork_run_exit(EXIT_SUCCESS);
    } else if (pid < 0) {
        int errno_bkp = errno;
        close(pfds[0]);
        close(pfds[1]);
        errno = errno_bkp;
        SOL_WRN("could not fork: %s", sol_util_strerrora(errno));
        return NULL;
    } else {
        struct sol_platform_linux_fork_run *handle;
        char msg = 0xff;
        int status = 0;

        handle = malloc(sizeof(*handle));
        SOL_NULL_CHECK_GOTO(handle, error_malloc);
        handle->pid = pid;
        handle->on_child_exit = on_child_exit;
        handle->data = data;
        handle->watch = sol_child_watch_add(pid, on_child, handle);
        SOL_NULL_CHECK_GOTO(handle, error_watch);

        status = sol_ptr_vector_append(&fork_runs, handle);
        SOL_INT_CHECK_GOTO(status, < 0, error_append);

        close(pfds[0]);
        while (write(pfds[1], &msg, 1) < 0) {
            if (errno == EINTR)
                continue;
            else {
                SOL_WRN("failed to write to pipe: %s", sol_util_strerrora(errno));
                status = errno;
                close(pfds[1]);
                errno = status;
                goto error_wpipe;
            }
        }
        close(pfds[1]);

        errno = 0;
        return handle;

error_wpipe:
        status = errno;
        sol_ptr_vector_del(&fork_runs, sol_ptr_vector_get_len(&fork_runs) - 1);
        errno = status;
error_append:
        status = errno;
        sol_child_watch_del(handle->watch);
        errno = status;
error_watch:
        status = errno;
        free(handle);
        errno = status;
error_malloc:
        status = errno;
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        errno = status;
        return NULL;
    }
}

SOL_API int
sol_platform_linux_fork_run_send_signal(struct sol_platform_linux_fork_run *handle, int sig)
{
    SOL_INT_CHECK(find_handle(handle), == UINT16_MAX, -errno);
    return kill(handle->pid, sig) == 0;
}

SOL_API int
sol_platform_linux_fork_run_stop(struct sol_platform_linux_fork_run *handle)
{
    int errno_bkp, status = 0;
    uint16_t i;

    i = find_handle(handle);
    SOL_INT_CHECK(i, == UINT16_MAX, -errno);
    sol_ptr_vector_del(&fork_runs, i);

    sol_child_watch_del(handle->watch);

    kill(handle->pid, SIGTERM);
    errno = 0;
    while (waitpid(handle->pid, &status, 0) < 0) {
        if (errno == EINTR) {
            errno = 0;
            continue;
        }

        SOL_WRN("waitpid(%" PRIu64 "): %s",
            (uint64_t)handle->pid, sol_util_strerrora(errno));

        if (errno == ECHILD)
            errno = 0; /* weird, but let's assume success */
        break;
    }

    errno_bkp = errno;

    if (handle->on_child_exit)
        handle->on_child_exit((void *)handle->data, handle->pid,
            WIFEXITED(status) ? WEXITSTATUS(status) : status);
    free(handle);

    errno = errno_bkp;
    return 0;
}

SOL_API uint64_t
sol_platform_linux_fork_run_get_pid(const struct sol_platform_linux_fork_run *handle)
{
    SOL_INT_CHECK(find_handle(handle), == UINT16_MAX, UINT64_MAX);
    return handle->pid;
}

SOL_API void
sol_platform_linux_fork_run_exit(int status)
{
    _exit(status);
}

int
sol_platform_impl_get_os_version(char **version)
{
    int r;
    struct utsname hostinfo;

    SOL_NULL_CHECK(version, -EINVAL);

    r = uname(&hostinfo);
    SOL_INT_CHECK(r, == -1, -errno);

    *version = strdup(hostinfo.release);
    if (!*version)
        return -ENOMEM;

    return 0;
}

static bool
mountopt_str_to_mask(const struct sol_str_slice slice, unsigned long *flags)
{
    unsigned int i;
    struct opt_mapping {
        const char *opt;
        unsigned long flag;
    } table[] = {
        { "bind", MS_BIND },
        { "dirsync", MS_DIRSYNC },
        { "mand", MS_MANDLOCK },
        { "move", MS_MOVE },
        { "noatime", MS_NOATIME },
        { "nodev", MS_NODEV },
        { "nodiratime", MS_NODIRATIME },
        { "noexec", MS_NOEXEC },
        { "nosuid", MS_NOSUID },
        { "ro", MS_RDONLY },
        { "relatime", MS_RELATIME },
        { "remount", MS_REMOUNT },
        { "silent", MS_SILENT },
        { "strictatime", MS_STRICTATIME },
        { "sync", MS_SYNCHRONOUS },
        { "defaults", ~MS_RDONLY | ~MS_NOSUID | ~MS_NODEV | ~MS_NOEXEC | ~MS_SYNCHRONOUS },
    };

    for (i = 0; i < ARRAY_SIZE(table); i++) {
        if (sol_str_slice_str_eq(slice, table[i].opt)) {
            *flags |= table[i].flag;
            return true;
        }
    }

    return false;
}

static unsigned long
get_mountflags(char *mnt_opts, struct sol_buffer *custom_opts, bool *noauto)
{
    struct sol_vector opts;
    struct sol_str_slice *itr;
    unsigned long flags = 0;
    uint16_t idx;
    bool first_custom = true;
    struct sol_str_slice slice = sol_str_slice_from_str(mnt_opts);

    opts = sol_util_str_split(slice, ",", 0);
    SOL_VECTOR_FOREACH_IDX (&opts, itr, idx) {
        if (mountopt_str_to_mask(*itr, &flags)) {
            continue;
        } else if (sol_str_slice_str_eq(*itr, "noauto")) {
            *noauto = true;
            continue;
        }

        if (!first_custom)
            sol_buffer_append_char(custom_opts, ',');
        sol_buffer_append_slice(custom_opts, *itr);
        first_custom = false;

    }
    return flags;
}

static int
parse_mount_point_file(const char *file, struct sol_ptr_vector *vector)
{
    struct mntent mbuf;
    struct mntent *m;
    char strings[4096];
    struct sol_mount_point *mp;
    uint16_t i;
    FILE *tab = setmntent(file, "re");

    if (!tab) {
        if (errno == ENOENT) {
            SOL_INF("No such %s", file);
            return -errno;
        } else {
            SOL_WRN("Unable to open %s file: %s", file,
                sol_util_strerrora(-errno));
            return -errno;
        }
    }

    while ((m = getmntent_r(tab, &mbuf, strings, sizeof(strings)))) {
        struct sol_buffer custom_opts;
        struct sol_mount_point *mpoint;
        bool noauto = false;

        mpoint = calloc(1, sizeof(struct sol_mount_point));
        if (!mpoint) {
            SOL_ERR("Could not allocate mount point memory");
            goto err;
        }

        if (mbuf.mnt_fsname)
            mpoint->dev = strdup(mbuf.mnt_fsname);

        if (mbuf.mnt_dir)
            mpoint->mpoint = strdup(mbuf.mnt_dir);

        if (mbuf.mnt_type)
            mpoint->fstype = strdup(mbuf.mnt_type);

        sol_buffer_init(&custom_opts);
        mpoint->flags = get_mountflags(mbuf.mnt_opts, &custom_opts, &noauto);
        mpoint->noauto = noauto;

        if (custom_opts.used)
            mpoint->opts = sol_buffer_steal(&custom_opts, NULL);

        sol_buffer_fini(&custom_opts);

        if (sol_ptr_vector_append(vector, mpoint) < 0) {
            SOL_ERR("Could not append mount point to vector");
            goto err;
        }
    }
    endmntent(tab);
    return 0;

err:
    SOL_PTR_VECTOR_FOREACH_IDX (vector, mp, i) {
        sol_mount_point_free(mp);
    }
    sol_ptr_vector_clear(vector);
    return -ENOMEM;
}

int
sol_platform_impl_get_mount_points(bool hplug_only, struct sol_ptr_vector *vector)
{
    struct sol_ptr_vector mtab, sol_mtab, delete;
    struct sol_mount_point *mp, *mp2;
    uint16_t i, j;
    int err;

    if (!hplug_only) {
        return parse_mount_point_file(SYS_MTAB_FILE, vector);
    }

    sol_ptr_vector_init(&mtab);
    err = parse_mount_point_file(SYS_MTAB_FILE, &mtab);
    SOL_INT_CHECK_GOTO(err, < 0, err_mtab);

    sol_ptr_vector_init(&sol_mtab);
    err = parse_mount_point_file(SOL_MTAB_FILE, &sol_mtab);
    SOL_INT_CHECK_GOTO(err, < 0, err_sol_mtab);

    sol_ptr_vector_init(&delete);
    SOL_PTR_VECTOR_FOREACH_IDX (&sol_mtab, mp, i) {
        SOL_PTR_VECTOR_FOREACH_IDX (&mtab, mp2, j) {
            if (streq(mp2->dev, mp->dev) && streq(mp2->mpoint, mp->mpoint)) {
                sol_ptr_vector_append(vector, mp2);
                sol_ptr_vector_remove(&mtab, mp2);
            } else {
                sol_ptr_vector_append(&delete, mp2);
            }
        }
        sol_mount_point_free(mp);
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&delete, mp, i) {
        sol_mount_point_free(mp);
    }

    sol_ptr_vector_clear(&delete);
    sol_ptr_vector_clear(&sol_mtab);
    sol_ptr_vector_clear(&mtab);

    return 0;

err_sol_mtab:
    sol_ptr_vector_clear(&sol_mtab);
err_mtab:
    sol_ptr_vector_clear(&mtab);
    return err;
}

static int
sol_mtab_remove_entry(const char *mpoint, struct sol_buffer *output)
{
    int fd = 0;
    struct sol_vector lines;
    struct sol_str_slice content = SOL_STR_SLICE_EMPTY;
    struct sol_str_slice *itr;
    struct sol_file_reader *reader;
    uint16_t idx;

    fd = open(SOL_MTAB_FILE, O_RDONLY | O_CLOEXEC);
    if (!fd) {
        SOL_ERR("Could not open "SOL_MTAB_FILE " - %s",
            sol_util_strerrora(errno));
        return -errno;
    }

    reader = sol_file_reader_fd(fd);
    if (!reader) {
        SOL_ERR("Could not read "SOL_MTAB_FILE " file - %s",
            sol_util_strerrora(errno));
        return -errno;
    }

    content = sol_file_reader_get_all(reader);
    lines = sol_util_str_split(content, "\n", 0);
    SOL_VECTOR_FOREACH_IDX (&lines, itr, idx) {
        char buff[512];
        snprintf(buff, itr->len, "%s", itr->data);
        if (!strstr(buff, mpoint))
            sol_buffer_append_printf(output, "%s", buff);
    }

    sol_vector_clear(&lines);
    sol_file_reader_close(reader);
    close(fd);
    return 0;
}

// since we don't have a mtab entry remove function we must implement it ourselves
static int
sol_mtab_cleanup(const char *mpoint)
{
    int res, err, fd;
    struct sol_buffer output;

    res = fd = 0;
    sol_buffer_init(&output);

    err = sol_mtab_remove_entry(mpoint, &output);
    SOL_INT_CHECK(err, < 0, err);

    fd = open(SOL_MTAB_FILE, O_RDWR | O_CLOEXEC | O_TRUNC);
    if (!fd) {
        SOL_ERR("Could not open " SOL_MTAB_FILE);
        return -errno;
    }

    err = write(fd, output.data, sizeof(output.data));
    if (err < -1) {
        SOL_ERR("Could not write "SOL_MTAB_FILE " file - %s",
            sol_util_strerrora(errno));
        res = -errno;
    }

    err = close(fd);
    if (err < -1) {
        SOL_ERR("Could not close "SOL_MTAB_FILE " file - %s",
            sol_util_strerrora(errno));
        res = -errno;
    }

    sol_buffer_fini(&output);
    return res;
}

static int
run_umount(const char *mpoint)
{
    int err;

    err = sol_mtab_cleanup(mpoint);
    SOL_INT_CHECK(err, < 0, err);

    err = umount(mpoint);
    if (err != 0) {
        SOL_ERR("Couldn't umount %s - %s", mpoint, sol_util_strerrora(errno));
        return -errno;
    }

    return err;
}

static void
on_umount_fork(void *data)
{
    int err;
    struct umount_data *udata = data;

    err = run_umount(udata->mpoint);
    sol_platform_linux_fork_run_exit(err);
}

static void
on_umount_fork_exit(void *data, uint64_t pid, int status)
{
    struct umount_data *udata = data;

    udata->async_cb(udata->mpoint, udata->data, pid, status);
    free(udata->mpoint);
    free(udata);
}

int
sol_platform_impl_umount(const char *mpoint, void (*async_cb)(const char *mpoint, void *data, uint64_t pid, int status), void *data)
{
    struct umount_data *udata;

    SOL_NULL_CHECK(mpoint, -EINVAL);

    if (!async_cb)
        return run_umount(mpoint);

    udata = calloc(1, sizeof(struct umount_data));
    SOL_NULL_CHECK(udata, -ENOMEM);

    udata->data = data;
    udata->mpoint = strdup(mpoint);
    udata->async_cb = async_cb;

    sol_platform_linux_fork_run(on_umount_fork, on_umount_fork_exit, udata);
    return 0;
}

static int
sol_mtab_add_entry(const char *dev, const char *mpoint, const char *fstype, unsigned long flags)
{
    struct mntent mbuf = { 0 };
    FILE *sol_mtab;
    int err;

    sol_mtab = setmntent(SOL_MTAB_FILE, "w+");
    if (!sol_mtab) {
        SOL_WRN("Unable to open "SOL_MTAB_FILE " file: %s",
            sol_util_strerrora(-errno));
        return -ENOENT;
    }

    mbuf.mnt_fsname = (char *)dev;
    mbuf.mnt_dir = (char *)mpoint;
    mbuf.mnt_type = (char *)fstype;
    mbuf.mnt_opts = (char *)"";

    err = addmntent(sol_mtab, &mbuf);
    if (err != 0)
        SOL_ERR("Could not add mnt entry - %s", sol_util_strerrora(-errno));

    endmntent(sol_mtab);
    return err;
}

static int
run_mount(const char *dev, const char *mpoint, const char *fstype, unsigned long flags, const char *opts)
{
    int err;

    err = sol_mtab_add_entry(dev, mpoint, fstype, flags);
    SOL_INT_CHECK(err, < 0, err);

    err = mount(dev, mpoint, fstype, flags, opts);
    if (err != 0) {
        SOL_ERR("Couldn't mount %s to %s - %s", dev, mpoint,
            sol_util_strerrora(errno));
        return -errno;
    }

    return err;
}

static void
on_mount_fork(void *data)
{
    int err;
    struct mount_data *mdata = data;

    err = run_mount(mdata->dev, mdata->mpoint, mdata->fstype, mdata->flags, mdata->opts);
    sol_platform_linux_fork_run_exit(err);
}

static void
on_mount_fork_exit(void *data, uint64_t pid, int status)
{
    struct mount_data *mdata = data;

    mdata->async_cb(mdata->dev, mdata->mpoint, mdata->data, pid, status);
    free(mdata->opts);
    free(mdata->fstype);
    free(mdata->mpoint);
    free(mdata->dev);
    free(mdata);
}

int
sol_platform_impl_mount(const char *dev, const char *mpoint, const char *fstype, unsigned long flags, const char *opts, void (*async_cb)(const char *dev, const char *mpoint, void *data, uint64_t pid, int status), void *data)
{
    struct mount_data *mdata;

    SOL_NULL_CHECK(dev, -EINVAL);
    SOL_NULL_CHECK(mpoint, -EINVAL);
    SOL_NULL_CHECK(fstype, -EINVAL);

    if (!async_cb)
        return run_mount(dev, mpoint, fstype, flags, opts);

    mdata = calloc(1, sizeof(struct mount_data));
    SOL_NULL_CHECK(mdata, -ENOMEM);

    mdata->data = data;
    mdata->dev = strdup(dev);
    mdata->mpoint = strdup(mpoint);
    mdata->fstype = strdup(fstype);

    if (opts)
        mdata->opts = strdup(opts);
    
    mdata->flags = flags;
    mdata->async_cb = async_cb;

    sol_platform_linux_fork_run(on_mount_fork, on_mount_fork_exit, mdata);
    return 0;
}

static void
sol_uevent_event_dispatch(struct uevent_context *ctx, struct sol_uevent *uevent)
{
    uint16_t idx;
    struct uevent_callback *cb;

    SOL_VECTOR_FOREACH_IDX (&ctx->callbacks, cb, idx) {
        if ((!cb->action || (cb->action && sol_str_slice_str_eq(uevent->action, cb->action))) &&
            (!cb->subsystem || (cb->subsystem && sol_str_slice_str_eq(uevent->subsystem, cb->subsystem)))) {
            cb->uevent_cb((void *)cb->cb_data, uevent);
        }
    }
}

static void
sol_uevent_read_msg(struct uevent_context *ctx, char *msg, int len)
{
    struct sol_uevent uevent = {
        .modalias = SOL_STR_SLICE_EMPTY,
        .action = SOL_STR_SLICE_EMPTY,
        .subsystem = SOL_STR_SLICE_EMPTY,
        .devtype = SOL_STR_SLICE_EMPTY,
        .devname = SOL_STR_SLICE_EMPTY,
    };
    int i = 0;

    /** lets avoid it to misbehave when people trying to test it with systemd running */
    if (!strncmp(msg, LIBUDEV_ID, sizeof(LIBUDEV_ID))) {
        SOL_INF("We're running side-by-side with udevd, skipping udevd generated event");
        return;
    }

    while (i < len) {
        char *curr, *del;
        size_t value_len;

        curr = msg + i;
        del = strrchr(curr, '=');
        value_len = del - curr;

        if (!strncmp(curr, "MODALIAS", value_len))
            uevent.modalias = sol_str_slice_from_str(++del);
        else if (!strncmp(curr, "ACTION", value_len))
            uevent.action = sol_str_slice_from_str(++del);
        else if (!strncmp(curr, "SUBSYSTEM", value_len))
            uevent.subsystem = sol_str_slice_from_str(++del);
        else if (!strncmp(curr, "DEVTYPE", value_len))
            uevent.devtype = sol_str_slice_from_str(++del);
        else if (!strncmp(curr, "DEVNAME", value_len))
            uevent.devname = sol_str_slice_from_str(++del);

        i += strlen(msg + i) + 1;
    }

    sol_uevent_event_dispatch(ctx, &uevent);
}

static bool
sol_uevent_handler(void *data, int fd, unsigned int cond)
{
    int len;
    struct uevent_context *ctx = data;
    char buffer[512];

    len = recv(ctx->uevent.fd, buffer, sizeof(buffer), MSG_WAITALL);
    if (len == -1) {
        SOL_ERR("Could not read netlink socket. %s", sol_util_strerrora(errno));
        return false;
    } else {
        sol_uevent_read_msg(ctx, buffer, len);
    }

    return true;
}

static int
sol_uevent_register(struct uevent_context *ctx)
{
    struct sockaddr_nl nls;

    memset(&nls, 0, sizeof(struct sockaddr_nl));
    nls.nl_family = AF_NETLINK;
    nls.nl_pid = getpid();
    nls.nl_groups = -1;

    ctx->uevent.fd = socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC,
        NETLINK_KOBJECT_UEVENT);
    if (ctx->uevent.fd == -1) {
        SOL_ERR("Could not open uevent netlink socket.");
        return -errno;
    }

    if (bind(ctx->uevent.fd, (void *)&nls, sizeof(struct sockaddr_nl))) {
        SOL_ERR("Could not bind to uevent socket.");
        close(ctx->uevent.fd);
        ctx->uevent.fd = 0;
        return -errno;
    }

    ctx->uevent.watch = sol_fd_add(ctx->uevent.fd, SOL_FD_FLAGS_IN,
        sol_uevent_handler, ctx);
    return 0;
}

SOL_API int
sol_platform_linux_uevent_subscribe(const char *action, const char *subsystem,
    void (*uevent_cb)(void *cb_data, struct sol_uevent *uevent),
    const void *cb_data)
{
    struct uevent_callback *cb;

    if (!uevent_ctx.running)
        sol_vector_init(&uevent_ctx.callbacks, sizeof(struct uevent_callback));

    cb = sol_vector_append(&uevent_ctx.callbacks);
    SOL_NULL_CHECK(cb, -1);

    cb->action = action;
    cb->subsystem = subsystem;
    cb->uevent_cb = uevent_cb;
    cb->cb_data = cb_data;

    if (!uevent_ctx.running) {
        sol_uevent_register(&uevent_ctx);
    }

    uevent_ctx.running = true;
    return 0;
}

static void
sol_uevent_cleaup(struct uevent_context *ctx)
{
    if (ctx->uevent.watch)
        sol_fd_del(ctx->uevent.watch);

    if (ctx->uevent.fd)
        close(ctx->uevent.fd);

    sol_vector_clear(&ctx->callbacks);
    ctx->running = false;
}

SOL_API int
sol_platform_linux_uevent_unsubscribe(const char *action, const char *subsystem,
    void (*uevent_cb)(void *cb_data, struct sol_uevent *uevent))
{
    struct uevent_callback *callback;
    uint16_t idx;
    int res = -1;

    SOL_NULL_CHECK(uevent_cb, -1);

    SOL_VECTOR_FOREACH_REVERSE_IDX (&uevent_ctx.callbacks, callback, idx) {
        if (callback->uevent_cb == uevent_cb) {
            sol_vector_del(&uevent_ctx.callbacks, idx);
            res = 0;
        }
    }

    if (!uevent_ctx.callbacks.len)
        sol_uevent_cleaup(&uevent_ctx);

    return res;
}
