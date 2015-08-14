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
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-console");

#include "sol-mainloop.h"
#include "sol-platform-linux-micro.h"
#include "sol-util.h"
#include "sol-vector.h"

struct instance {
    struct sol_platform_linux_fork_run *fork_run;
    struct sol_timeout *respawn_timeout;
    char tty[];
};
#define RESPAWN_TIMEOUT_MS 1000

static struct sol_ptr_vector instances = SOL_PTR_VECTOR_INIT;
static char *getty_cmd = NULL;
static char *term = NULL;
static char *baudrate = NULL;
#define BAUDRATE_DEFAULT "115200,38400,9600"
static const char shell[] = "/bin/sh";

static void console_spawn(struct instance *inst);

static const char *
find_getty_cmd(void)
{
    static const char *cmds[] = {
        "/usr/bin/agetty",
        "/usr/sbin/agetty",
        "/bin/agetty",
        "/sbin/agetty",
        "/usr/bin/getty",
        "/usr/sbin/getty",
        "/bin/getty",
        "/sbin/getty",
    };
    const char **itr, **itr_end;

    itr = cmds;
    itr_end = itr + ARRAY_SIZE(cmds);
    for (; itr < itr_end; itr++) {
        if (access(*itr, R_OK | X_OK) == 0)
            return *itr;
    }

    SOL_ERR("no getty command found");
    return NULL;
}

static bool
on_respawn_timeout(void *data)
{
    struct instance *inst = data;

    inst->respawn_timeout = NULL;
    console_spawn(inst);
    return false;
}

static const char *
get_term_for_tty(const char *tty)
{
    if (streqn(tty, "tty", sizeof("tty") - 1)) {
        const char *p = tty + sizeof("tty") - 1;
        if (*p >= '0' && *p <= '9')
            return "linux";
    }
    return "vt102";
}

/*
 * do things getty would do to spawn a shell, basically become the
 * session leader of the given tty, then make stdio/stdout/stderr use
 * it.
 */
static void
do_shell(const char *tty)
{
    char term_buf[128];
    const char *envp[] = {
        term_buf,
        "HOME=/",
        NULL,
    };
    char tty_path[PATH_MAX];
    pid_t pid, tsid;
    int r;

    SOL_INF("no getty, exec shell: %s", shell);

    r = snprintf(term_buf, sizeof(term_buf), "TERM=%s",
        term ? term : get_term_for_tty(tty));
    if (r < 0 || r >= (int)sizeof(term_buf))
        envp[0] = "TERM=vt102";
    else
        envp[0] = term_buf;

    pid = setsid();
    if (pid < 0) {
        int fd;

        SOL_WRN("could not setsid(): %s", sol_util_strerrora(errno));
        pid = getpid();
        fd = open("/dev/tty", O_RDWR | O_NONBLOCK);
        if (fd >= 0) {
            sighandler_t oldsig;
            /* man:tty(4)
             * TIOCNOTTY:
             * Detach the calling process from its controlling terminal.
             *
             * If the process is the session leader, then SIGHUP and
             * SIGCONT signals are sent to the foreground process
             * group and all processes in the current session lose
             * their controlling tty.
             */
            oldsig = signal(SIGHUP, SIG_IGN);
            r = ioctl(fd, TIOCNOTTY);
            close(fd);
            signal(SIGHUP, oldsig);
            SOL_INT_CHECK_GOTO(r, < 0, end);
        }
    }

    r = snprintf(tty_path, sizeof(tty_path), "/dev/%s", tty);
    SOL_INT_CHECK_GOTO(r, < 0, end);
    SOL_INT_CHECK_GOTO(r, >= (int)sizeof(tty_path), end);

    close(STDIN_FILENO);
    r = open(tty_path, O_RDWR | O_NONBLOCK);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    if (r != 0) {
        r = dup2(r, 0);
        SOL_INT_CHECK_GOTO(r, < 0, end);
    }
    r = dup2(STDIN_FILENO, 1);
    SOL_INT_CHECK_GOTO(r, < 0, end);
    r = dup2(STDIN_FILENO, 2);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    fchown(STDIN_FILENO, 0, 0);
    fchmod(STDIN_FILENO, 0620);

    tsid = tcgetsid(STDIN_FILENO);
    if (tsid < 0) {
        r = ioctl(STDIN_FILENO, TIOCSCTTY, 1L);
        SOL_INT_CHECK_GOTO(r, < 0, end);
    }
    r = tcsetpgrp(STDIN_FILENO, pid);
    SOL_INT_CHECK_GOTO(r, < 0, end);

end:
    ioctl(STDIN_FILENO, TIOCSCTTY, 0);
    chdir("/");
    execle(shell, shell, NULL, envp);
}

static void
on_fork(void *data)
{
    struct instance *inst = data;

    if (!getty_cmd || streq(getty_cmd, shell))
        do_shell(inst->tty);
    else {
        const char *use_term = term;
        const char *use_baudrate = baudrate;
        if (!use_term)
            use_term = get_term_for_tty(inst->tty);
        if (!use_baudrate)
            use_baudrate = BAUDRATE_DEFAULT;
        SOL_DBG("exec %s -L %s %s %s",
            getty_cmd, use_baudrate, inst->tty, use_term);
        execl(getty_cmd,
            getty_cmd, "-L", use_baudrate, inst->tty, use_term, NULL);
    }
    sol_platform_linux_fork_run_exit(EXIT_FAILURE);
}

static void
on_fork_exit(void *data, uint64_t pid, int status)
{
    struct instance *inst = data;

    SOL_DBG("tty=%s pid=%" PRIu64 " exited with status=%d. Respawn on timeout...",
        inst->tty, pid, status);

    if (inst->respawn_timeout)
        sol_timeout_del(inst->respawn_timeout);

    inst->respawn_timeout = sol_timeout_add(RESPAWN_TIMEOUT_MS,
        on_respawn_timeout,
        inst);
    inst->fork_run = NULL;
}

static void
parse_var(const char *start, size_t len)
{
    static const struct spec {
        const char *prefix;
        size_t prefixlen;
        char **storage;
    } specs[] = {
#define SPEC(str, storage)                      \
    { str, sizeof(str) - 1, &storage \
    }
        SPEC("getty=", getty_cmd),
        SPEC("baudrate=", baudrate),
        SPEC("term=", term),
#undef SPEC
    };
    const struct spec *itr, *itr_end;

    itr = specs;
    itr_end = itr + ARRAY_SIZE(specs);
    for (; itr < itr_end; itr++) {
        if (itr->prefixlen < len &&
            memcmp(itr->prefix, start, itr->prefixlen) == 0) {

            free(*(itr->storage));
            *(itr->storage) = strndup(start + itr->prefixlen,
                len - itr->prefixlen);
            break;
        }
    }
}

static void
parse_kcmdline_entry(const char *start, size_t len)
{
    const char prefix[] = "sol-console.";
    const size_t prefixlen = strlen(prefix);

    if (len < prefixlen)
        return;

    if (memcmp(start, prefix, prefixlen) != 0)
        return;

    start += prefixlen;
    len -= prefixlen;
    parse_var(start, len);
}

static int
load_kcmdline(void)
{
    char buf[4096] = {};
    const char *p, *end, *start;
    int err;

    err = sol_util_read_file("/proc/cmdline", "%4095[^\n]", buf);
    if (err < 1)
        return err;

    start = buf;
    end = start + strlen(buf);
    for (p = start; p < end; p++) {
        if (isblank(*p) && start < p) {
            parse_kcmdline_entry(start, p - start);
            start = p + 1;
        }
    }
    if (start < end)
        parse_kcmdline_entry(start, end - start);

    return 0;
}

static void
console_spawn(struct instance *inst)
{
    inst->fork_run = sol_platform_linux_fork_run(on_fork,
        on_fork_exit, inst);
}

static void
add_active_console(const char *start, size_t len)
{
    struct instance *inst;
    int r;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&instances, inst, i) {
        size_t cur_len = strlen(inst->tty);
        if (cur_len == len && memcmp(inst->tty, start, len) == 0)
            return;
    }

    inst = malloc(sizeof(struct instance) + len + 1);
    SOL_NULL_CHECK(inst);

    memcpy(inst->tty, start, len);
    inst->tty[len] = '\0';

    r = sol_ptr_vector_append(&instances, inst);
    SOL_INT_CHECK_GOTO(r, < 0, err_append);

    console_spawn(inst);
    return;

err_append:
    free(inst);
}

static int
load_active_consoles(void)
{
    char buf[4096] = {};
    const char *p, *end, *start;
    int err;

    err = sol_util_read_file("/sys/class/tty/console/active", "%4095[^\n]", buf);
    if (err < 1)
        return err;

    start = buf;
    end = start + strlen(buf);
    SOL_DBG("active consoles: '%s'", buf);
    for (p = start; p < end; p++) {
        if (isblank(*p) && start < p) {
            add_active_console(start, p - start);
            start = p + 1;
        }
    }
    if (start < end)
        add_active_console(start, end - start);

    return 0;
}

static int
console_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    int err = 0;

    if (sol_ptr_vector_get_len(&instances) > 0)
        goto end;

    err = load_kcmdline();
    if (err < 0)
        goto error;

    if (!getty_cmd) {
        const char *cmd = find_getty_cmd();
        if (cmd)
            getty_cmd = strdup(cmd);
    }

    if (!baudrate)
        baudrate = strdup(BAUDRATE_DEFAULT);

    err = load_active_consoles();
    if (err < 0)
        goto error;

end:
    sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_ACTIVE);
    return 0;

error:
    sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_FAILED);
    return err;
}

static int
console_stop(const struct sol_platform_linux_micro_module *module, const char *service, bool force_immediate)
{
    struct instance *inst;
    uint16_t i;

    if (sol_ptr_vector_get_len(&instances) == 0)
        goto end;

    SOL_PTR_VECTOR_FOREACH_IDX (&instances, inst, i) {
        if (inst->fork_run) {
            sol_platform_linux_fork_run_stop(inst->fork_run);
            inst->fork_run = NULL;
        }

        if (inst->respawn_timeout) {
            sol_timeout_del(inst->respawn_timeout);
            inst->respawn_timeout = NULL;
        }

        free(inst);
    }
    sol_ptr_vector_clear(&instances);

end:
    if (getty_cmd) {
        free(getty_cmd);
        getty_cmd = NULL;
    }

    if (term) {
        free(term);
        term = NULL;
    }

    if (baudrate) {
        free(baudrate);
        baudrate = NULL;
    }

    sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_INACTIVE);
    return 0;
}

static int
console_restart(const struct sol_platform_linux_micro_module *module, const char *service)
{
    sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_ACTIVE);
    return 0;
}

static int
console_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

/*
 * spawn getty/agetty or /bin/sh on active consoles.
 *
 * active consoles are defined in the kernel command line with the syntax:
 *
 *      console=tty0
 *          starts a console on /dev/tty0
 *
 *      console=ttyS0
 *      console=ttyS0,9600n8
 *          starts a console on /dev/ttyS0 (serial line), the second
 *          version specifies the baudrate, parity and number of bits.
 *
 *     console=tty0 console=ttyS0
 *          multiple entries are allowed, the first is used as the
 *          /dev/console while the others replicate kernel messages.
 *
 * See https://www.kernel.org/doc/Documentation/serial-console.txt
 *
 * The following kernel command line extensions are supported:
 *
 *      sol-console.getty=/usr/bin/getty
 *      sol-console.getty=/bin/sh
 *          specify getty command to be used, if not given then
 *          various well-known paths are searched. The special entry
 *          /bin/sh is used to start a shell without getty, this is
 *          useful for constrained systems where getty and login would
 *          add too much overhead.
 *
 *      sol-console.term=vt100
 *          specify the $TERM to use for getty or shell. Defaults to
 *          linux if tty<N> or vt102 otherwise.
 *
 *      sol-console.baudrate=115200,9600
 *          specify the baudrate to give to getty.
 *
 */

SOL_PLATFORM_LINUX_MICRO_MODULE(CONSOLE,
    .name = "console",
    .init = console_init,
    .start = console_start,
    .restart = console_restart,
    .stop = console_stop,
    );
