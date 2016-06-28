/*
 * This file is part of the Soletta (TM) Project
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
#include <fcntl.h>
#include <linux/netlink.h>
#include <locale.h>
#include <limits.h>
#include <mntent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "sol-file-reader.h"
#include "sol-mainloop.h"
#include "sol-platform-impl.h"
#include "sol-platform-linux.h"
#include "sol-platform.h"
#include "sol-str-table.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#define SOL_MTAB_FILE P_tmpdir "/mtab.sol"
#define LIBUDEV_ID "libudev"

#ifndef TFD_TIMER_CANCELON_SET
#define TFD_TIMER_CANCELON_SET (1 << 1)
#endif

struct sol_platform_linux_fork_run {
    pid_t pid;
    void (*on_child_exit)(void *data, uint64_t pid, int status);
    const void *data;
    struct sol_child_watch *watch;
};

struct umount_data {
    const void *data;
    char *mpoint;
    void (*cb)(void *data, const char *mpoint, int error);
};

struct mount_data {
    const void *data;
    char *dev;
    char *mpoint;
    char *fstype;
    void (*cb)(void *data, const char *mpoint, int status);
};

struct uevent_callback {
    char *action;
    char *subsystem;
    const void *data;
    void (*cb)(void *cb_data, struct sol_uevent *uevent);
};

struct uevent_context {
    bool running;
    struct sol_vector callbacks;
    struct {
        int fd;
        struct sol_fd *watch;
    } uevent;
};

struct timer_fd_context {
    struct sol_fd *watcher;
    int fd;
};

static char hostname[HOST_NAME_MAX + 1];
static char timezone_str[PATH_MAX + 1];
static struct uevent_context uevent_ctx;
static struct sol_ptr_vector fork_runs = SOL_PTR_VECTOR_INIT;
static struct timer_fd_context timer_ctx = { NULL, -1 };

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

const char *
sol_platform_impl_get_hostname(void)
{
    if (gethostname(hostname, sizeof(hostname)) < 0)
        return NULL;
    return hostname;
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

    if (pipe2(pfds, O_CLOEXEC) < 0) {
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
        sol_ptr_vector_del_last(&fork_runs);
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

static int
parse_mount_point_file(const char *file, struct sol_ptr_vector *vector)
{
    struct mntent mbuf;
    struct mntent *m;
    char strings[4096];
    struct sol_buffer *itr, *buff = NULL;
    uint16_t i;
    FILE *tab = setmntent(file, "re");

    if (!tab) {
        if (errno == ENOENT) {
            SOL_INF("No such %s", file);
            return -errno;
        } else {
            SOL_WRN("Unable to open %s file: %s", file,
                sol_util_strerrora(errno));
            return -errno;
        }
    }

    while ((m = getmntent_r(tab, &mbuf, strings, sizeof(strings)))) {
        int r;

        buff = sol_buffer_new();
        SOL_NULL_CHECK_GOTO(buff, err);

        r = sol_buffer_append_printf(buff, "%s", mbuf.mnt_dir);
        SOL_INT_CHECK_GOTO(r, < 0, err);

        r = sol_ptr_vector_append(vector, buff);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }
    endmntent(tab);
    return 0;

err:
    sol_buffer_free(buff);
    SOL_PTR_VECTOR_FOREACH_IDX (vector, itr, i) {
        sol_buffer_fini(itr);
    }
    sol_ptr_vector_clear(vector);
    endmntent(tab);
    return -ENOMEM;
}

int
sol_platform_impl_get_mount_points(struct sol_ptr_vector *vector)
{
    return parse_mount_point_file(SOL_MTAB_FILE, vector);
}

static int
sol_mtab_remove_entry(const char *mpoint, struct sol_buffer *output)
{
    struct sol_vector lines;
    struct sol_str_slice content = SOL_STR_SLICE_EMPTY;
    struct sol_str_slice *itr;
    struct sol_file_reader *reader;
    uint16_t idx;

    reader = sol_file_reader_open(SOL_MTAB_FILE);
    if (!reader) {
        SOL_ERR("Could not read "SOL_MTAB_FILE " file - %s",
            sol_util_strerrora(errno));
        return -errno;
    }

    content = sol_file_reader_get_all(reader);
    lines = sol_str_slice_split(content, "\n", 0);
    SOL_VECTOR_FOREACH_IDX (&lines, itr, idx) {
        if (!strstr(itr->data, mpoint) || strstartswith(itr->data, "#"))
            sol_buffer_append_printf(output, "%s", itr->data);
    }

    sol_vector_clear(&lines);
    sol_file_reader_close(reader);
    return 0;
}

// since we don't have a mtab entry remove function we must implement it ourselves
static int
sol_mtab_cleanup(const char *mpoint)
{
    int err, fd, res = 0;
    size_t len;
    struct sol_buffer output;

    sol_buffer_init(&output);

    err = sol_mtab_remove_entry(mpoint, &output);
    SOL_INT_CHECK(err, < 0, err);

    fd = open(SOL_MTAB_FILE, O_RDWR | O_CLOEXEC | O_TRUNC);
    if (fd < 0) {
        SOL_ERR("Could not open " SOL_MTAB_FILE);
        res = -errno;
        goto finish;
    }

    len = write(fd, output.data, output.used);
    if (!len || len != output.used) {
        SOL_ERR("Could not write "SOL_MTAB_FILE " file - %s",
            sol_util_strerrora(errno));
        res = -errno;
    }

    err = close(fd);
    if (err < 0) {
        SOL_ERR("Could not close "SOL_MTAB_FILE " file - %s",
            sol_util_strerrora(errno));
        res = -errno;
    }

finish:
    sol_buffer_fini(&output);
    return res;
}

SOL_ATTR_NO_RETURN static void
on_umount_fork(void *data)
{
    int err;
    struct umount_data *udata = data;

    err = sol_mtab_cleanup(udata->mpoint);
    if (err < 0)
        sol_platform_linux_fork_run_exit(EXIT_FAILURE);

    err = umount(udata->mpoint);
    if (err != 0) {
        SOL_ERR("Couldn't umount %s - %s", udata->mpoint, sol_util_strerrora(errno));
        sol_platform_linux_fork_run_exit(EXIT_FAILURE);
    }

    sol_platform_linux_fork_run_exit(EXIT_SUCCESS);
}

static void
on_umount_fork_exit(void *data, uint64_t pid, int status)
{
    struct umount_data *udata = data;

    udata->cb((void *)udata->data, udata->mpoint, status);
    free(udata->mpoint);
    free(udata);
}

int
sol_platform_impl_umount(const char *mpoint, void (*cb)(void *data, const char *mpoint, int error), const void *data)
{
    struct umount_data *udata;

    udata = calloc(1, sizeof(struct umount_data));
    SOL_NULL_CHECK(udata, -ENOMEM);

    udata->data = data;
    udata->mpoint = strdup(mpoint);
    SOL_NULL_CHECK_GOTO(udata->mpoint, err);
    udata->cb = cb;

    sol_platform_linux_fork_run(on_umount_fork, on_umount_fork_exit, udata);
    return 0;

err:
    free(udata);
    return -ENOMEM;
}

static int
sol_mtab_add_entry(const char *dev, const char *mpoint, const char *fstype)
{
    struct mntent mbuf = { 0 };
    FILE *sol_mtab;
    int err;

    sol_mtab = setmntent(SOL_MTAB_FILE, "w+");
    if (!sol_mtab) {
        SOL_WRN("Unable to open "SOL_MTAB_FILE " file: %s",
            sol_util_strerrora(errno));
        return -ENOENT;
    }

    mbuf.mnt_fsname = (char *)dev;
    mbuf.mnt_dir = (char *)mpoint;
    mbuf.mnt_type = (char *)fstype;
    mbuf.mnt_opts = (char *)"";

    err = addmntent(sol_mtab, &mbuf);
    if (err != 0)
        SOL_ERR("Could not add mnt entry - %s", sol_util_strerrora(errno));

    endmntent(sol_mtab);
    return err;
}

SOL_ATTR_NO_RETURN static void
on_mount_fork(void *data)
{
    int err;
    struct mount_data *mdata = data;

    err = sol_mtab_add_entry(mdata->dev, mdata->mpoint, mdata->fstype);
    if (err < 0)
        sol_platform_linux_fork_run_exit(EXIT_FAILURE);

    err = mount(mdata->dev, mdata->mpoint, mdata->fstype, 0, NULL);
    if (err != 0) {
        SOL_ERR("Couldn't mount %s to %s - %s", mdata->dev, mdata->mpoint,
            sol_util_strerrora(errno));
        sol_platform_linux_fork_run_exit(EXIT_FAILURE);
    }

    sol_platform_linux_fork_run_exit(EXIT_SUCCESS);
}

static void
on_mount_fork_exit(void *data, uint64_t pid, int status)
{
    struct mount_data *mdata = data;

    mdata->cb((void *)mdata->data, mdata->mpoint, status);
    free(mdata->fstype);
    free(mdata->mpoint);
    free(mdata->dev);
    free(mdata);
}

SOL_API int
sol_platform_linux_mount(const char *dev, const char *mpoint, const char *fstype, void (*cb)(void *data, const char *mpoint, int status), const void *data)
{
    struct mount_data *mdata;

    SOL_NULL_CHECK(dev, -EINVAL);
    SOL_NULL_CHECK(mpoint, -EINVAL);
    SOL_NULL_CHECK(fstype, -EINVAL);
    SOL_NULL_CHECK(cb, -EINVAL);

    mdata = calloc(1, sizeof(struct mount_data));
    SOL_NULL_CHECK(mdata, -ENOMEM);

    mdata->data = data;
    mdata->dev = strdup(dev);
    SOL_NULL_CHECK_GOTO(mdata->dev, dev_err);

    mdata->mpoint = strdup(mpoint);
    SOL_NULL_CHECK_GOTO(mdata->mpoint, mpoint_err);

    mdata->fstype = strdup(fstype);
    SOL_NULL_CHECK_GOTO(mdata->fstype, fstype_err);

    mdata->cb = cb;

    sol_platform_linux_fork_run(on_mount_fork, on_mount_fork_exit, mdata);
    return 0;

fstype_err:
    free(mdata->mpoint);
mpoint_err:
    free(mdata->dev);
dev_err:
    free(mdata);
    return -ENOMEM;
}

static void
sol_uevent_event_dispatch(struct uevent_context *ctx, struct sol_uevent *uevent)
{
    uint16_t idx;
    struct uevent_callback *cb;

    SOL_VECTOR_FOREACH_IDX (&ctx->callbacks, cb, idx) {
        if ((!cb->action || sol_str_slice_str_eq(uevent->action, cb->action)) &&
            (!cb->subsystem || sol_str_slice_str_eq(uevent->subsystem, cb->subsystem))) {
            cb->cb((void *)cb->data, uevent);
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
    if (streqn(msg, LIBUDEV_ID, sizeof(LIBUDEV_ID))) {
        SOL_INF("We're running side-by-side with udevd, skipping udevd generated event");
        return;
    }

    while (i < len) {
        char *curr, *del = NULL;
        size_t value_len;

        curr = msg + i;
        del = strrchr(curr, '=');
        if (!del)
            goto next;

        value_len = del - curr;
        del++;

        if (streqn(curr, "MODALIAS", value_len))
            uevent.modalias = sol_str_slice_from_str(del);
        else if (streqn(curr, "ACTION", value_len))
            uevent.action = sol_str_slice_from_str(del);
        else if (streqn(curr, "SUBSYSTEM", value_len))
            uevent.subsystem = sol_str_slice_from_str(del);
        else if (streqn(curr, "DEVTYPE", value_len))
            uevent.devtype = sol_str_slice_from_str(del);
        else if (streqn(curr, "DEVNAME", value_len))
            uevent.devname = sol_str_slice_from_str(del);

next:
        i += strlen(msg + i) + 1;
    }

    sol_uevent_event_dispatch(ctx, &uevent);
}

static bool
sol_uevent_handler(void *data, int fd, uint32_t cond)
{
    int len;
    struct uevent_context *ctx = data;
    char buffer[512];

    while (true) {
        len = recv(ctx->uevent.fd, buffer, sizeof(buffer), MSG_WAITALL);
        if (len == -1) {
            if (errno == EINTR) {
                SOL_INF("Could not read netlink socket, retrying.");
                continue;
            } else {
                SOL_WRN("Could not read netlink socket. %s", sol_util_strerrora(errno));
                return false;
            }
        } else {
            sol_uevent_read_msg(ctx, buffer, len);
        }

        break;
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

    ctx->uevent.watch = NULL;
    ctx->uevent.fd = -1;
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
    SOL_NULL_CHECK_GOTO(ctx->uevent.watch, error_fd);
    return 0;

error_fd:
    close(ctx->uevent.fd);
    ctx->uevent.fd = -1;
    return -ENOMEM;
}

SOL_API int
sol_platform_linux_uevent_subscribe(const char *action, const char *subsystem,
    void (*cb)(void *data, struct sol_uevent *uevent), const void *data)
{
    struct uevent_callback *cb_obj;

    if (!uevent_ctx.running)
        sol_vector_init(&uevent_ctx.callbacks, sizeof(struct uevent_callback));

    cb_obj = sol_vector_append(&uevent_ctx.callbacks);
    SOL_NULL_CHECK(cb_obj, -ENOMEM);

    if (action) {
        cb_obj->action = strdup(action);
        SOL_NULL_CHECK_GOTO(cb_obj->action, action_err);
    }

    if (subsystem) {
        cb_obj->subsystem = strdup(subsystem);
        SOL_NULL_CHECK_GOTO(cb_obj->subsystem, subsys_err);
    }

    cb_obj->cb = cb;
    cb_obj->data = data;

    if (!uevent_ctx.running && sol_uevent_register(&uevent_ctx) == 0) {
        uevent_ctx.running = true;
    }

    return 0;

subsys_err:
    free(cb_obj->action);
action_err:
    free(cb_obj);
    return -ENOMEM;
}

static void
sol_uevent_cleanup(struct uevent_context *ctx)
{
    if (ctx->uevent.watch) {
        sol_fd_del(ctx->uevent.watch);
        ctx->uevent.watch = NULL;
    }

    if (ctx->uevent.fd != -1) {
        close(ctx->uevent.fd);
        ctx->uevent.fd = -1;
    }

    sol_vector_clear(&ctx->callbacks);
    ctx->running = false;
}

SOL_API int
sol_platform_linux_uevent_unsubscribe(const char *action, const char *subsystem,
    void (*cb)(void *data, struct sol_uevent *uevent), const void *data)
{
    struct uevent_callback *callback;
    uint16_t idx;

    SOL_NULL_CHECK(cb, -ENOENT);

    SOL_VECTOR_FOREACH_REVERSE_IDX (&uevent_ctx.callbacks, callback, idx) {
        if (callback->cb == cb &&
            (!callback->action || streq(callback->action, action)) &&
            (!callback->subsystem || streq(callback->subsystem, subsystem)) &&
            callback->data == data) {
            free(callback->subsystem);
            free(callback->action);
            sol_vector_del(&uevent_ctx.callbacks, idx);
        }
    }

    if (!uevent_ctx.callbacks.len)
        sol_uevent_cleanup(&uevent_ctx);

    return 0;
}


int64_t
sol_platform_impl_get_system_clock(void)
{
    return (int64_t)time(NULL);
}

static bool
system_clock_changed(void *data, int fd, uint32_t active_flags)
{
    char buf[4096];

    if (read(timer_ctx.fd, buf, sizeof(buf)) >= 0)
        return true;
    close(timer_ctx.fd);
    timer_ctx.fd = -1;
    timer_ctx.watcher = NULL;
    sol_platform_register_system_clock_monitor();
    sol_platform_inform_system_clock_changed();
    return false;
}

int
sol_platform_register_system_clock_monitor(void)
{
    int r;
    struct itimerspec spec;

    if (timer_ctx.watcher)
        return 0;

    timer_ctx.fd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC);
    SOL_INT_CHECK(timer_ctx.fd, < 0, -errno);

    memset(&spec, 0, sizeof(struct itimerspec));
    /* Set a dummy value, end of time. */
    spec.it_value.tv_sec = LONG_MAX;
    if (timerfd_settime(timer_ctx.fd,
        TFD_TIMER_ABSTIME | TFD_TIMER_CANCELON_SET, &spec, NULL) < 0) {
        r = -errno;
        SOL_WRN("Could not register a timer to watch for system_clock changes.");
        goto err_exit;
    }

    timer_ctx.watcher = sol_fd_add(timer_ctx.fd,
        SOL_FD_FLAGS_IN, system_clock_changed, NULL);

    if (!timer_ctx.watcher) {
        r = -ENOMEM;
        goto err_exit;
    }

    return 0;

err_exit:
    close(timer_ctx.fd);
    timer_ctx.fd = -1;
    return r;
}

int
sol_platform_unregister_system_clock_monitor(void)
{
    if (!timer_ctx.watcher)
        return 0;

    sol_fd_del(timer_ctx.watcher);
    close(timer_ctx.fd);
    timer_ctx.watcher = NULL;
    timer_ctx.fd = -1;
    return 0;
}

const char *
sol_platform_impl_get_timezone(void)
{
    ssize_t n;
    size_t offset;

    n = readlink("/etc/localtime", timezone_str, sizeof(timezone_str) - 1);
    if (n < 0) {
        SOL_WRN("Could not readlink /etc/localtime");
        return NULL;
    }
    timezone_str[n] = '\0';
    if (strstartswith(timezone_str, "../usr/share/zoneinfo/"))
        offset = strlen("../usr/share/zoneinfo/");
    else if (strstartswith(timezone_str, "/usr/share/zoneinfo/"))
        offset = strlen("/usr/share/zoneinfo/");
    else {
        SOL_WRN("The timzone is not a link to /usr/share/zoneinfo/");
        return NULL;
    }
    memmove(timezone_str, timezone_str + offset, strlen(timezone_str) - offset + 1);
    return timezone_str;
}

const char *
sol_platform_impl_get_locale(enum sol_platform_locale_category category)
{
    int ctype;

    ctype = sol_platform_locale_to_c_category(category);
    SOL_INT_CHECK(ctype, < 0, NULL);
    return setlocale(ctype, NULL);
}

int
sol_platform_impl_apply_locale(enum sol_platform_locale_category category, const char *locale)
{
    int ctype;
    char *loc;

    ctype = sol_platform_locale_to_c_category(category);
    SOL_INT_CHECK(ctype, < 0, -EINVAL);
    loc = setlocale(ctype, locale);
    SOL_NULL_CHECK(loc, -EINVAL);
    return 0;
}

static enum sol_platform_locale_category
system_locale_to_sol_locale(const struct sol_str_slice loc)
{
    static const struct sol_str_table map[] = {
        SOL_STR_TABLE_ITEM("LANG", SOL_PLATFORM_LOCALE_LANGUAGE),
        SOL_STR_TABLE_ITEM("LC_ADDRESS", SOL_PLATFORM_LOCALE_ADDRESS),
        SOL_STR_TABLE_ITEM("LC_COLLATE", SOL_PLATFORM_LOCALE_COLLATE),
        SOL_STR_TABLE_ITEM("LC_CTYPE", SOL_PLATFORM_LOCALE_CTYPE),
        SOL_STR_TABLE_ITEM("LC_IDENTIFICATION", SOL_PLATFORM_LOCALE_IDENTIFICATION),
        SOL_STR_TABLE_ITEM("LC_MEASUREMENT", SOL_PLATFORM_LOCALE_MEASUREMENT),
        SOL_STR_TABLE_ITEM("LC_MESSAGES", SOL_PLATFORM_LOCALE_MESSAGES),
        SOL_STR_TABLE_ITEM("LC_MONETARY", SOL_PLATFORM_LOCALE_MONETARY),
        SOL_STR_TABLE_ITEM("LC_NAME", SOL_PLATFORM_LOCALE_NAME),
        SOL_STR_TABLE_ITEM("LC_NUMERIC", SOL_PLATFORM_LOCALE_NUMERIC),
        SOL_STR_TABLE_ITEM("LC_PAPER", SOL_PLATFORM_LOCALE_PAPER),
        SOL_STR_TABLE_ITEM("LC_TELEPHONE", SOL_PLATFORM_LOCALE_TELEPHONE),
        SOL_STR_TABLE_ITEM("LC_TIME", SOL_PLATFORM_LOCALE_TIME),
        { }
    };

    return sol_str_table_lookup_fallback(map, loc, SOL_PLATFORM_LOCALE_UNKNOWN);
}

int
sol_platform_impl_load_locales(char **locale_cache)
{
    FILE *f;
    int r;
    char *line, *sep;
    struct sol_str_slice key, value;
    enum sol_platform_locale_category category;

    f = fopen("/etc/locale.conf", "re");

    if (!f) {
        if (errno == ENOENT) {
            SOL_INF("The locale file (/etc/locale.conf) was not found in the system.");
            return 0;
        } else
            return -errno;
    }

    for (category = SOL_PLATFORM_LOCALE_LANGUAGE; category <= SOL_PLATFORM_LOCALE_TIME; category++) {
        free(locale_cache[category]);
        locale_cache[category] = NULL;
    }

    line = NULL;
    while (fscanf(f, "%m[^\n]\n", &line) != EOF) {
        unsigned int i, len = strlen(line);

        /* It is possible to have comments in locale.conf, ignore them */
        for (i = 0; i < len; i++) {
            if (line[i] == '#')
                goto ignore;
            if (line[i] != ' ')
                break;
        }

        sep = strchr(line, '=');
        if (!sep) {
            SOL_WRN("The locale entry: %s does not have the separator '='",
                line);
            r = -EINVAL;
            goto err_val;
        }

        key.data = line;
        value.data = sep + 1;
        key.len = sep - key.data;
        value.len = len - key.len - 1;
        if (!value.len)
            goto ignore;

        category = system_locale_to_sol_locale(key);
        if (category != SOL_PLATFORM_LOCALE_UNKNOWN) {
            struct sol_buffer buf;

            r = sol_util_unescape_quotes(value, &buf);
            SOL_INT_CHECK_GOTO(r, < 0, err_val);
            locale_cache[category] = sol_buffer_steal_or_copy(&buf, NULL);
            sol_buffer_fini(&buf);
            SOL_NULL_CHECK_GOTO(locale_cache[category], err_nomem);
        }

ignore:
        free(line);
        line = NULL;
    }

    r = ferror(f);
    if (r != 0)
        r = -errno;
    fclose(f);
    return r;

err_nomem:
    r = -ENOMEM;
err_val:
    free(line);
    fclose(f);
    return r;
}

int
sol_platform_impl_locale_to_c_category(enum sol_platform_locale_category category)
{
    switch (category) {
    case SOL_PLATFORM_LOCALE_ADDRESS:
        return LC_ADDRESS;
    case SOL_PLATFORM_LOCALE_IDENTIFICATION:
        return LC_IDENTIFICATION;
    case SOL_PLATFORM_LOCALE_MESSAGES:
        return LC_MESSAGES;
    case SOL_PLATFORM_LOCALE_PAPER:
        return LC_PAPER;
    case SOL_PLATFORM_LOCALE_NAME:
        return LC_NAME;
    case SOL_PLATFORM_LOCALE_TELEPHONE:
        return LC_TELEPHONE;
    case SOL_PLATFORM_LOCALE_MEASUREMENT:
        return LC_MEASUREMENT;
    default:
        return -EINVAL;
    }
}

const char *
sol_platform_impl_locale_to_c_str_category(enum sol_platform_locale_category category)
{
    switch (category) {
    case SOL_PLATFORM_LOCALE_ADDRESS:
        return "LC_ADDRESS";
    case SOL_PLATFORM_LOCALE_IDENTIFICATION:
        return "LC_IDENTIFICATION";
    case SOL_PLATFORM_LOCALE_MEASUREMENT:
        return "LC_MEASUREMENT";
    case SOL_PLATFORM_LOCALE_MESSAGES:
        return "LC_MESSAGES";
    case SOL_PLATFORM_LOCALE_NAME:
        return "LC_NAME";
    case SOL_PLATFORM_LOCALE_PAPER:
        return "LC_PAPER";
    case SOL_PLATFORM_LOCALE_TELEPHONE:
        return "LC_TELEPHONE";
    default:
        return NULL;
    }
}
