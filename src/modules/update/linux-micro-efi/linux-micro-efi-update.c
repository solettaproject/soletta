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

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-efi-update");

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "update-common.h"

#include "sol-json.h"
#include "sol-http-client.h"
#include "sol-lib-loader.h"
#include "sol-mainloop.h"
#include "sol-message-digest.h"
#include "sol-update.h"
#include "sol-update-modules.h"
#include "sol-util.h"

#define SOLETTA_EXEC_FILE "/usr/bin/soletta"
#define SOLETTA_EXEC_FILE_OLD "/usr/bin/soletta_old"
//#define SOLETTA_EXEC_FILE "/ssd/soletta"

static bool
install_timeout(void *data)
{
    struct sol_update_handle *handle = data;
    int r, dir_fd, fd;
    DIR *dir;

    handle->timeout = NULL;

    r = rename(SOLETTA_EXEC_FILE, SOLETTA_EXEC_FILE_OLD);
    if (r < 0) {
        SOL_WRN("Could not create backup file: %s", sol_util_strerrora(errno));
        goto end;
    }

    /* We create an 'updating' file on /boot, so EFI startup.nsh knows that
     * we are updating. It will then create 'check_update' file there, and
     * remove 'updating'.
     * Once we restart soletta, we erase 'check-update' if we are not
     * 'SOLETTA_EXEC_FILE_OLD', which means that update failed */
    if (sol_util_write_file("/boot/updating", "1") < 0) {
        SOL_WRN("Could not create '/boot/updating' guard file");
        goto end;
    }

    r = common_move_file(handle->file_path, SOLETTA_EXEC_FILE);
    if (r < 0) {
        SOL_WRN("Could not install update file: %s", sol_util_strerrora(errno));
        goto end;
    }

    if (chmod(SOLETTA_EXEC_FILE,
        S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH | S_IXUSR | S_IXGRP | S_IXOTH) < 0) {

        SOL_WRN("Could not set permissions to updated file: %s",
            sol_util_strerrora(errno));
        goto end;
    }

    /* fsync file and dir so we are sure we can reboot */
    /* TODO check if doing this isn't bogus */
    fd = open(SOLETTA_EXEC_FILE, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        SOL_WRN("Could not open file to ensure installed is synced to storage: %s",
            sol_util_strerrora(errno));
        goto end;
    }

    if (fsync(fd) < 0) {
        SOL_WRN("Could not ensure installed file is synced to storage: %s",
            sol_util_strerrora(errno));
        close(fd);
        goto end;
    }
    close(fd);

    dir = opendir("/usr/bin");
    SOL_NULL_CHECK_MSG_GOTO(dir, end,
        "Could not open [/usr/bin] dir to ensure its synced to storage: %s",
        sol_util_strerrora(errno));
    dir_fd = dirfd(dir);
    if (dir_fd < 0) {
        SOL_WRN("Could not get file descriptor for [/usr/bin] to ensure its synced to storage: %s",
            sol_util_strerrora(errno));
        goto end;
    }

    if (fsync(dir_fd) < 0) {
        SOL_WRN("Could not ensure [/usr/bin] is synced to storage: %s",
            sol_util_strerrora(errno));
        goto end;
    }
    closedir(dir);

    errno = 0;

end:
    handle->cb_install((void *)handle->user_data, -errno);
    delete_handle(handle);

    return false;
}

static struct sol_update_handle *
install(const char *file_path,
    void (*cb)(void *data, int status),
    void *data)
{
    struct sol_update_handle *handle;

    SOL_NULL_CHECK(file_path, NULL);
    SOL_NULL_CHECK(cb, NULL);

    handle = calloc(1, sizeof(struct sol_update_handle));
    SOL_NULL_CHECK(handle, NULL);

    handle->task = TASK_UPDATE;
    handle->cb_install = cb;
    handle->user_data = data;
    handle->file_path = strdup(file_path);
    SOL_NULL_CHECK_GOTO(handle->file_path, err);

    handle->timeout = sol_timeout_add(0, install_timeout, handle);
    SOL_NULL_CHECK_MSG_GOTO(handle->timeout, err, "Could not create timeout");

    return handle;

err:
    free(handle->file_path);
    free(handle);

    return NULL;
}

static void
check_post_install(void)
{
    char *cmdline;
    int r;

    r = sol_util_read_file("/proc/self/cmdline", "%ms", &cmdline);
    SOL_INT_CHECK(r, <= 0);

    if (strstartswith(cmdline, SOLETTA_EXEC_FILE_OLD)) {
        SOL_WRN("Running backuped Soletta. Failed update?");
        goto end;
    }

    unlink("/boot/check-update");

end:
    free(cmdline);
}

static void
init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;

    check_post_install();
}

SOL_UPDATE_DECLARE(LINUX_MICRO_EFI_UPDATE,
    .check = common_check,
    .fetch = common_fetch,
    .cancel = common_cancel,
    .progress = common_get_progress,
    .install = install,
    .init = init
    );
