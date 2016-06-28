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

#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sol-file-reader.h"
#include "sol-mainloop.h"
#include "sol-platform-impl.h"
#include "sol-platform-linux-micro.h"
#include "sol-platform-linux.h"
#include "sol-platform.h"
#include "sol-str-table.h"
#include "sol-util-file.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#include "sol-platform-linux-micro-builtins-gen.h"

#define SOL_DEBUG_ARG "sol-debug=1"
#define SOL_DEBUG_COMM_ARG "sol-debug-comm="
#define LOCALE_CONF_MAX_CREATE_ATTEMPTS (5)

static enum sol_platform_state platform_state = SOL_PLATFORM_STATE_INITIALIZING;
static int reboot_cmd = RB_AUTOBOOT;
static const char *reboot_exec;
static const struct mount_table {
    const char *fstype;
    const char *source;
    const char *target;
    const char *options;
    unsigned long flags;
    bool fatal;
} mount_table[] = {
    { "sysfs", NULL, "/sys", NULL, MS_NOSUID | MS_NOEXEC | MS_NODEV, true },
    { "proc", NULL,  "/proc", NULL, MS_NOSUID | MS_NOEXEC | MS_NODEV, true },
    { "devtmpfs", NULL,  "/dev", "mode=0755", MS_NOSUID | MS_STRICTATIME, true },
    { "devpts", NULL,  "/dev/pts", "mode=0620", MS_NOSUID | MS_NOEXEC, true },
    { "tmpfs", NULL,  "/dev/shm", "mode=1777", MS_NOSUID | MS_NODEV | MS_STRICTATIME, true },
    { "tmpfs", NULL,  "/run", "mode=0755", MS_NOSUID | MS_NODEV | MS_STRICTATIME, true },
    { "tmpfs", NULL,  "/tmp", NULL, 0, true },
    { "debugfs", NULL,  "/sys/kernel/debug", NULL, 0, false },
    { "securityfs", NULL, "/sys/kernel/security", NULL, MS_NOSUID | MS_NOEXEC | MS_NODEV, false },
};

struct sol_fd_watcher_ctx {
    struct sol_fd *watcher;
    int fd;
};

struct sol_locale_monitor {
    struct sol_fd_watcher_ctx fd_watcher;
    struct sol_timeout *create_timeout;
    uint8_t create_attempts;
};

static struct sol_fd_watcher_ctx hostname_monitor = { NULL, -1 };
static struct sol_fd_watcher_ctx timezone_monitor = { NULL, -1 };
static struct sol_locale_monitor locale_monitor = { { NULL, -1 }, NULL, 0 };

#ifdef ENABLE_DYNAMIC_MODULES
struct service_module {
    char *name;
    const struct sol_platform_linux_micro_module *module;
    void *handle;
};

static struct sol_vector service_modules = SOL_VECTOR_INIT(struct service_module);
#endif

#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0) || defined(ENABLE_DYNAMIC_MODULES)
struct service_instance {
    const struct sol_platform_linux_micro_module *module;
    char *name;
    enum sol_platform_service_state state;
    unsigned int monitors;
};

static struct sol_vector service_instances = SOL_VECTOR_INIT(struct service_instance);
#endif

#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0)
static bool builtin_init[SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT];

static const struct sol_platform_linux_micro_module *
find_builtin_service_module(const char *name)
{
    const struct sol_platform_linux_micro_module *const *itr;
    unsigned int i;

    for (i = 0, itr = SOL_PLATFORM_LINUX_MICRO_MODULE_ALL;
        i < SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT;
        i++, itr++) {
        if (streq(name, (*itr)->name)) {
            if (!builtin_init[i]) {
                if ((*itr)->init) {
                    int err = (*itr)->init((*itr), (*itr)->name);
                    if (err < 0) {
                        SOL_WRN("failed to init builtin module '%s'", name);
                        return NULL;
                    }
                }
                builtin_init[i] = true;
            }
            return *itr;
        }
    }

    SOL_DBG("no builtin service module for '%s'", name);
    return NULL;
}
#endif

#ifdef ENABLE_DYNAMIC_MODULES
static const struct sol_platform_linux_micro_module *
find_external_service_module(const char *name)
{
    const struct service_module *mod;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&service_modules, mod, i) {
        if (streq(mod->name, name))
            return mod->module;
    }

    SOL_DBG("no loaded external service module for '%s'", name);
    return NULL;
}

static const struct sol_platform_linux_micro_module *
new_external_service_module(const char *name)
{
    struct service_module *mod;
    const struct sol_platform_linux_micro_module **p_sym;
    void *handle;
    char path[PATH_MAX];
    int r;

    r = snprintf(path, sizeof(path), "%s/%s.so",
        LINUXMICROMODULESDIR, name);
    SOL_INT_CHECK(r, >= (int)sizeof(path), NULL);
    SOL_INT_CHECK(r, < 0, NULL);

    handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
    if (!handle) {
        SOL_WRN("could not load module '%s': %s", path, dlerror());
        return NULL;
    }
    p_sym = dlsym(handle, "SOL_PLATFORM_LINUX_MICRO_MODULE");
    if (!p_sym || !*p_sym) {
        SOL_WRN("could not find symbol SOL_PLATFORM_LINUX_MICRO_MODULE in module '%s': %s",
            path, dlerror());
        goto error;
    }
#ifndef SOL_NO_API_VERSION
    if ((*p_sym)->api_version != SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION) {
        SOL_WRN("module '%s' has incorrect api_version: %hu expected %hu",
            path, (*p_sym)->api_version, SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION);
        goto error;
    }
#endif

    mod = sol_vector_append(&service_modules);
    SOL_NULL_CHECK_GOTO(mod, error);
    mod->module = *p_sym;
    mod->handle = handle;
    mod->name = strdup(name);
    SOL_NULL_CHECK_GOTO(mod->name, error_name);

    if (mod->module->init) {
        r = mod->module->init(mod->module, mod->name);
        SOL_INT_CHECK_GOTO(r, < 0, error_init);
    }

    SOL_INF("loaded external service '%s' from '%s'", mod->name, path);
    return mod->module;

error_init:
    free(mod->name);
error_name:
    sol_vector_del_last(&service_modules);
error:
    dlclose(handle);
    return NULL;
}
#endif

#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0) || defined(ENABLE_DYNAMIC_MODULES)
static const struct sol_platform_linux_micro_module *
find_service_module(const char *name)
{
    const struct sol_platform_linux_micro_module *mod;

#if SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0
    mod = find_builtin_service_module(name);
    if (mod)
        return mod;
#endif

#ifdef ENABLE_DYNAMIC_MODULES
    mod = find_external_service_module(name);
    if (mod)
        return mod;

    mod = new_external_service_module(name);
    if (mod)
        return mod;
#endif

    SOL_WRN("unknown service '%s'", name);
    return NULL;
}

static struct service_instance *
find_service_instance(const char *name)
{
    const struct sol_platform_linux_micro_module *mod;
    struct service_instance *inst;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&service_instances, inst, i) {
        if (streq(inst->name, name))
            return inst;
    }

    mod = find_service_module(name);
    if (!mod)
        return NULL;

    inst = sol_vector_append(&service_instances);
    SOL_NULL_CHECK(inst, NULL);

    inst->name = strdup(name);
    SOL_NULL_CHECK_GOTO(inst->name, error_name);

    inst->module = mod;
    inst->state = SOL_PLATFORM_SERVICE_STATE_UNKNOWN;

    return inst;

error_name:
    sol_vector_del_last(&service_instances);
    return NULL;
}
#endif

static void
platform_state_set(enum sol_platform_state state)
{
    if (platform_state == state)
        return;

    platform_state = state;
    sol_platform_inform_state_monitors(state);
}

#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0) || defined(ENABLE_DYNAMIC_MODULES)
static int
load_initial_services_entry(const char *start, size_t len)
{
    char *name;
    bool required = true;
    int err = 0;

    if (len > 1 && start[len - 1] == '?') {
        required = false;
        len--;
    }

    name = strndupa(start, len);
    SOL_DBG("loading initial service '%s'", name);
    err = sol_platform_start_service(name);
    if (err < 0) {
        if (!required) {
            SOL_INF("failed to load initial service '%s'", name);
            err = 0;
        } else {
            SOL_WRN("failed to load initial service '%s'", name);
        }
    }

    return err;
}

static int
load_initial_services_internal(struct sol_file_reader *reader)
{
    struct sol_str_slice file;
    const char *p, *start, *end;
    int err = 0;

    file = sol_file_reader_get_all(reader);
    start = file.data;
    end = start + file.len;
    for (p = start; err == 0 && p < end; p++) {
        if (isspace((uint8_t)*p) && start < p) {
            err = load_initial_services_entry(start, p - start);
            start = p + 1;
        }
    }
    if (err == 0 && start < end)
        err = load_initial_services_entry(start, end - start);

    return err;
}

static int
load_initial_services(void)
{
    const char **itr, *paths[] = {
        PKGSYSCONFDIR "/initial-services",
        LINUXMICROMODULESDIR "/initial-services",
    };
    int err = 0;

    for (itr = paths; itr < paths + sol_util_array_size(paths); itr++) {
        struct sol_file_reader *reader = sol_file_reader_open(*itr);
        if (!reader && errno == ENOENT) {
            SOL_DBG("no initial services to load at '%s'", *itr);
            continue;
        }
        if (!reader) {
            SOL_WRN("could not load initial services '%s': %s", *itr, sol_util_strerrora(errno));
            return -errno;
        }
        err = load_initial_services_internal(reader);
        sol_file_reader_close(reader);
        if (err < 0)
            break;
    }

    return err;
}
#endif

static int
setup_pid1(void)
{
    static const struct symlink_table {
        const char *target;
        const char *source;
        bool fatal;
    } symlink_table[] = {
        { "/proc/self/fd", "/dev/fd", true },
        { "/proc/self/fd/0", "/dev/stdin", true },
        { "/proc/self/fd/1", "/dev/stdout", true },
        { "/proc/self/fd/2", "/dev/stderr", true },
        { "/proc/kcore", "/dev/core", false },
    };

    const struct mount_table *mnt;
    const struct symlink_table *sym;
    int err;
    pid_t pid;

    for (mnt = mount_table; mnt < mount_table + sol_util_array_size(mount_table); mnt++) {
        const char *source = mnt->source ? mnt->source : "none";

        SOL_DBG("creating %s", mnt->target);
        err = mkdir(mnt->target, 0755);
        if (err < 0) {
            if (errno == EEXIST || !mnt->fatal) {
                SOL_INF("could not mkdir '%s': %s", mnt->target, sol_util_strerrora(errno));
            } else {
                SOL_CRI("could not mkdir '%s': %s", mnt->target, sol_util_strerrora(errno));
                return -errno;
            }
        }

        SOL_DBG("mounting '%s' from '%s' to '%s', options=%s",
            mnt->fstype, source, mnt->target,
            mnt->options ? mnt->options : "(none)");
        err = mount(source, mnt->target, mnt->fstype, mnt->flags, mnt->options);
        if (err < 0) {
            if (errno == EBUSY || !mnt->fatal) {
                SOL_INF("could not mount '%s' from '%s' to '%s', options=%s: %s",
                    mnt->fstype, source, mnt->target,
                    mnt->options ? mnt->options : "(none)",
                    sol_util_strerrora(errno));
            } else {
                SOL_CRI("could not mount '%s' from '%s' to '%s', options=%s: %s",
                    mnt->fstype, source, mnt->target,
                    mnt->options ? mnt->options : "(none)",
                    sol_util_strerrora(errno));
                return -errno;
            }
        }
    }

    for (sym = symlink_table; sym < symlink_table + sol_util_array_size(symlink_table); sym++) {
        SOL_DBG("symlinking '%s' to '%s'", sym->source, sym->target);
        err = symlink(sym->target, sym->source);
        if (err < 0) {
            if (errno == EEXIST || !sym->fatal) {
                SOL_INF("could not symlink '%s' to '%s': %s",
                    sym->source, sym->target, sol_util_strerrora(errno));
            } else {
                SOL_CRI("could not symlink '%s' to '%s': %s",
                    sym->source, sym->target, sol_util_strerrora(errno));
                return -errno;
            }
        }
    }

    SOL_DBG("creating new session group leader");
    pid = setsid();
    if (pid < 0) {
        SOL_INF("could not create new session group leader: %s",
            sol_util_strerrora(errno));
    } else {
        SOL_DBG("setting controlling terminal");
        err = ioctl(STDIN_FILENO, TIOCSCTTY, 1);
        if (err < 0) {
            SOL_CRI("could not set controlling terminal: %s", sol_util_strerrora(errno));
            return -errno;
        }
    }

    SOL_DBG("PID 1 fully setup");

    return 0;
}

static void
teardown_pid1(void)
{
    FILE *mount_info = NULL;
    bool again;
    uint8_t idx;

    sync();
    mount_info = fopen("/proc/self/mountinfo", "re");
    if (!mount_info) {
        SOL_WRN("Failed to open /proc/self/mountinfo: %s", sol_util_strerrora(errno));
        goto end;
    }

    do {
        rewind(mount_info);
        again = false;

        for (;;) {
            int ret;
            bool should_umount = true;
            char *path;

            ret = fscanf(mount_info,
                "%*s "           /* (1) mount id */
                "%*s "           /* (2) parent id */
                "%*s "           /* (3) major:minor */
                "%*s "           /* (4) root */
                "%ms "           /* (5) mount point */
                "%*s"            /* (6) mount options */
                "%*[^-]"         /* (7) optional fields */
                "- "             /* (8) separator */
                "%*s "           /* (9) file system type */
                "%*s"            /* (10) mount source */
                "%*s"            /* (11) mount options 2 */
                "%*[^\n]",       /* some rubbish at the end */
                &path);

            if (ret != 1) {
                if (ret == EOF)
                    break;

                SOL_WRN("Failed to parse /proc/self/mountinfo: %s", sol_util_strerrora(errno));
                continue;
            }

            for (idx = 0; idx < sol_util_array_size(mount_table); idx++) {
                if (streq(mount_table[idx].target, path)) {
                    should_umount = false;
                    break;
                }
            }

            if (should_umount == false)
                continue;

            if (umount(path) == -1) {
                SOL_WRN("Erro umounting %s - %s", path, sol_util_strerrora(errno));
                continue;
            }
            again = true;
        }
    } while (again);

    fclose(mount_info);

end:
    if (reboot_exec) {
        const char *cmd[] = { reboot_exec, NULL };
        execv(reboot_exec, (char *const *)cmd);
        SOL_CRI("could not execute reboot command '%s': %s",
            reboot_exec, sol_util_strerrora(errno));
    }

    reboot(reboot_cmd);
}

static bool
sol_platform_linux_micro_should_debug(char **gdb_comm)
{
    struct sol_file_reader *fr;
    struct sol_str_slice file;
    struct sol_vector tokens;
    struct sol_str_slice *token;
    uint16_t i;
    bool res = false;

    fr = sol_file_reader_open("/proc/cmdline");
    if (!fr) {
        SOL_ERR("Could not open /proc/cmdline");
        return false;
    }

    file = sol_file_reader_get_all(fr);
    // remove the \n in the end of the file (this is a single line file - always)
    file.len--;
    tokens = sol_str_slice_split(file, " ", 0);
    SOL_VECTOR_FOREACH_IDX (&tokens, token, i) {
        if (sol_str_slice_str_eq(*token, SOL_DEBUG_ARG)) {
            res = true;
        } else if (strstartswith(token->data, SOL_DEBUG_COMM_ARG)) {
            size_t id_len = sizeof(SOL_DEBUG_COMM_ARG);
            *gdb_comm = strndup(token->data + id_len, token->len - id_len);
        }
    }
    sol_vector_clear(&tokens);

    if (res && !*gdb_comm) {
        SOL_ERR("No comm set, trying to set default one: /dev/ttyS0");
        *gdb_comm = strdup("/dev/ttyS0");
        if (!*gdb_comm) {
            SOL_ERR("Could not allocate comm string memory, not debugging.");
            res = false;
        }
    } else if (!res && *gdb_comm) {
        SOL_INF("No %s provided, %s must be use in conjunction with %s",
            SOL_DEBUG_ARG, SOL_DEBUG_ARG, SOL_DEBUG_COMM_ARG);
        free(*gdb_comm);
        *gdb_comm = NULL;
    }

    sol_file_reader_close(fr);
    return res;
}

static void
gdb_wait(pid_t gdb_pid)
{
    while (true) {
        int status;
        pid_t child = wait(&status);
        if (child < 0) {
            if (errno == EINTR)
                continue;
            SOL_WRN("wait() failed: %s", sol_util_strerrora(errno));
            // no more child process, they are all dead, restart gdbserver --
            // it is just to be complete, it should never happen as we check if
            // gdbserver is dead to restart it
            return;
        } else {
            SOL_DBG("child pid=%" PRIu64 " status=%d", (uint64_t)child, status);
            if (child == gdb_pid) {
                SOL_INF("gdbserver exited, restart it");
                return;
            }
        }
    }
}

static void
gdb_exec(const char *gdb_comm)
{
    char **argv;
    int argc;
    size_t i;
    const char *paths[] = {
        "/usr/bin/gdbserver",
        "/bin/gdbserver",
    };

    if (setenv("SOL_LOAD_INITIAL_SERVICES", "1", 1) == -1) {
        SOL_ERR("Could not set SOL_LOAD_INITIAL_SERVICES");
        _exit(EXIT_FAILURE);
    }

    argc = sol_argc();
    argv = sol_argv();
    if (argc < 1 || !argv || !argv[0]) {
        SOL_ERR("Invalid argc=%d, argv=%p, argv[0]=%p", argc, argv,
            argv ? argv[0] : NULL);
        _exit(EXIT_FAILURE);
    }

    for (i = 0; i < sol_util_array_size(paths); i++) {
        if (execl(paths[i], paths[i], gdb_comm, argv[0], NULL) == -1)
            SOL_DBG("failed to exec %s - %s", paths[i],
                sol_util_strerrora(errno));
    }

    SOL_WRN("no gdb server found, execute the application without it");
    execv(argv[0], argv);
    SOL_CRI("could not execute the application %s: %s", argv[0],
        sol_util_strerrora(errno));
    _exit(EXIT_FAILURE);
}

SOL_ATTR_NO_RETURN static void
gdb_debug(const char *gdb_comm)
{
    while (true) {
        pid_t gdb_pid = fork();
        if (gdb_pid < 0) {
            SOL_WRN("could not fork(): %s", sol_util_strerrora(errno));
            // give the system some time to breath
            sleep(1);
        } else if (gdb_pid > 0) {
            gdb_wait(gdb_pid);
        } else {
            gdb_exec(gdb_comm);
        }
    }
}

int
sol_platform_impl_init(void)
{
    bool want_load_initial_services = false;
    pid_t pid = getpid();

    if (pid == 1 && getppid() == 0) {
        char *gdb_comm = NULL;
        int err = setup_pid1();
        SOL_INT_CHECK(err, < 0, err);

        want_load_initial_services = true;
        if (sol_platform_linux_micro_should_debug(&gdb_comm)) {
            gdb_debug(gdb_comm);
        }
    } else {
        const char *s = getenv("SOL_LOAD_INITIAL_SERVICES");
        if (s && streq(s, "1"))
            want_load_initial_services = true;
    }

    if (want_load_initial_services) {
#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0) || defined(ENABLE_DYNAMIC_MODULES)
        int err;

        platform_state_set(SOL_PLATFORM_STATE_INITIALIZING);
        err = load_initial_services();
        SOL_INT_CHECK(err, < 0, err);
#endif
    }

    platform_state_set(SOL_PLATFORM_STATE_RUNNING);
    return 0;
}

static void
service_instances_cleanup(void)
{
#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0) || defined(ENABLE_DYNAMIC_MODULES)
    struct service_instance *inst;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&service_instances, inst, i) {
        if (inst->state != SOL_PLATFORM_SERVICE_STATE_UNKNOWN &&
            inst->state != SOL_PLATFORM_SERVICE_STATE_INACTIVE &&
            inst->state != SOL_PLATFORM_SERVICE_STATE_DEACTIVATING &&
            inst->state != SOL_PLATFORM_SERVICE_STATE_FAILED) {
            if (inst->module->stop)
                inst->module->stop(inst->module, inst->name, true);
        }
        free(inst->name);
    }
    sol_vector_clear(&service_instances);
#endif
}

static void
service_modules_cleanup(void)
{
#ifdef ENABLE_DYNAMIC_MODULES
    struct service_module *mod;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&service_modules, mod, i) {
        if (mod->module->shutdown)
            mod->module->shutdown(mod->module, mod->name);
        free(mod->name);
        dlclose(mod->handle);
    }
    sol_vector_clear(&service_modules);
#endif
}

static void
builtins_cleanup(void)
{
#if SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0
    const struct sol_platform_linux_micro_module *const *itr;
    unsigned int i;

    for (i = 0, itr = SOL_PLATFORM_LINUX_MICRO_MODULE_ALL;
        i < SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT;
        i++, itr++) {
        if (builtin_init[i] && (*itr)->shutdown)
            (*itr)->shutdown((*itr), (*itr)->name);
    }
#endif
}

void
sol_platform_impl_shutdown(void)
{
    platform_state_set(SOL_PLATFORM_STATE_STOPPING);

    service_instances_cleanup();
    service_modules_cleanup();
    sol_platform_unregister_hostname_monitor();
    sol_platform_unregister_system_clock_monitor();
    sol_platform_unregister_timezone_monitor();
    sol_platform_unregister_locale_monitor();
    builtins_cleanup();

    if (getpid() == 1 && getppid() == 0)
        teardown_pid1();
}

int
sol_platform_impl_get_state(void)
{
    return platform_state;
}

int
sol_platform_impl_add_service_monitor(const char *service)
{
#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0) || defined(ENABLE_DYNAMIC_MODULES)
    struct service_instance *inst;
    int r = 0;

    if (platform_state == SOL_PLATFORM_STATE_STOPPING) {
        SOL_WRN("doing shutdown process");
        return -EINVAL;
    }

    inst = find_service_instance(service);
    if (!inst)
        return -ENOENT;

    if (inst->monitors == 0 && inst->module->start_monitor)
        r = inst->module->start_monitor(inst->module, inst->name);

    if (r == 0)
        inst->monitors++;

    return r;
#else
    return -ENOENT;
#endif
}

int
sol_platform_impl_del_service_monitor(const char *service)
{
#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0) || defined(ENABLE_DYNAMIC_MODULES)
    struct service_instance *inst;
    int r = 0;

    if (platform_state == SOL_PLATFORM_STATE_STOPPING) {
        SOL_WRN("doing shutdown process");
        return -EINVAL;
    }

    inst = find_service_instance(service);
    if (!inst)
        return -ENOENT;

    if (inst->monitors == 1 && inst->module->stop_monitor)
        r = inst->module->stop_monitor(inst->module, inst->name);

    if (r == 0 && inst->monitors > 0)
        inst->monitors--;

    return r;
#else
    return -ENOENT;
#endif
}

int
sol_platform_impl_start_service(const char *service)
{
#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0) || defined(ENABLE_DYNAMIC_MODULES)
    struct service_instance *inst;

    if (platform_state == SOL_PLATFORM_STATE_STOPPING) {
        SOL_WRN("doing shutdown process");
        return -EINVAL;
    }

    inst = find_service_instance(service);
    if (!inst)
        return -ENOENT;

    if (!inst->module->start) {
        SOL_DBG("service '%s' doesn't support 'start' operation", service);
        return -ENOTSUP;
    }

    inst->state = SOL_PLATFORM_SERVICE_STATE_ACTIVATING;
    return inst->module->start(inst->module, inst->name);
#else
    return -ENOENT;
#endif
}

int
sol_platform_impl_stop_service(const char *service)
{
#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0) || defined(ENABLE_DYNAMIC_MODULES)
    struct service_instance *inst;

    if (platform_state == SOL_PLATFORM_STATE_STOPPING) {
        SOL_WRN("doing shutdown process");
        return -EINVAL;
    }

    inst = find_service_instance(service);
    if (!inst)
        return -ENOENT;

    if (!inst->module->stop) {
        SOL_DBG("service '%s' doesn't support 'stop' operation", service);
        return -ENOTSUP;
    }

    inst->state = SOL_PLATFORM_SERVICE_STATE_DEACTIVATING;
    return inst->module->stop(inst->module, inst->name, false);
#else
    return -ENOENT;
#endif
}

int
sol_platform_impl_restart_service(const char *service)
{
#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0) || defined(ENABLE_DYNAMIC_MODULES)
    struct service_instance *inst;

    if (platform_state == SOL_PLATFORM_STATE_STOPPING) {
        SOL_WRN("doing shutdown process");
        return -EINVAL;
    }

    inst = find_service_instance(service);
    if (!inst)
        return -ENOENT;

    if (!inst->module->restart) {
        int r;

        SOL_DBG("service '%s' doesn't support 'restart' operation, doing stop->start", service);

        if (inst->module->stop) {
            inst->state = SOL_PLATFORM_SERVICE_STATE_DEACTIVATING;
            r = inst->module->stop(inst->module, inst->name, false);
            SOL_INT_CHECK(r, < 0, r);
        }

        if (inst->module->start) {
            inst->state = SOL_PLATFORM_SERVICE_STATE_ACTIVATING;
            r = inst->module->start(inst->module, inst->name);
            SOL_INT_CHECK(r, < 0, r);
            return 0;
        }

        return -ENOTSUP;
    }

    inst->state = SOL_PLATFORM_SERVICE_STATE_RELOADING;
    return inst->module->restart(inst->module, inst->name);
#else
    return -ENOENT;
#endif
}

int
sol_platform_impl_set_target(const char *target)
{
    if (streq(target, SOL_PLATFORM_TARGET_POWER_OFF))
        reboot_cmd = RB_POWER_OFF;
    else if (streq(target, SOL_PLATFORM_TARGET_REBOOT))
        reboot_cmd = RB_AUTOBOOT;
    else if (streq(target, SOL_PLATFORM_TARGET_SUSPEND))
        reboot_cmd = RB_SW_SUSPEND;
    else if (streq(target, SOL_PLATFORM_TARGET_DEFAULT))
        reboot_exec = "/sbin/init";
    else if (streq(target, SOL_PLATFORM_TARGET_RESCUE))
        reboot_exec = "/sbin/rescue";
    else if (streq(target, SOL_PLATFORM_TARGET_EMERGENCY))
        reboot_exec = "/sbin/emergency";
    else {
        SOL_WRN("Unsupported target: %s", target);
        return -ENOTSUP;
    }

    platform_state_set(SOL_PLATFORM_STATE_STOPPING);

    sol_quit();
    return 0;
}

static int
validate_machine_id(char id[SOL_STATIC_ARRAY_SIZE(33)])
{
    if (!sol_util_uuid_str_is_valid(sol_str_slice_from_str(id)))
        return -EINVAL;

    return 0;
}

int
sol_platform_impl_get_machine_id(char id[SOL_STATIC_ARRAY_SIZE(33)])
{
    static const char *etc_path = "/etc/machine-id",
        *run_path = "/run/machine-id";
    int r;

    r = sol_util_read_file(etc_path, "%32s", id);
    if (r < 0) {
        /* We can only tolerate the file not existing or being
         * malformed on /etc/, otherwise it's got more serious
         * problems and it's better to fail */
        if (r == -ENOENT || r == EOF)
            return r; /* else proceed to run_path */
    } else
        return validate_machine_id(id);

    r = sol_util_read_file(run_path, "%32s", id);
    if (r < 0) {
        return r;
    } else
        return validate_machine_id(id);
}

int
sol_platform_impl_get_serial_number(char **number)
{
    int r;
    char id[37];

    /* root access required for this */
    r = sol_util_read_file("/sys/class/dmi/id/product_uuid", "%36s", id);
    SOL_INT_CHECK(r, < 0, r);

    *number = strdup(id);
    if (!*number)
        return -errno;

    return r;
}

SOL_API void
sol_platform_linux_micro_inform_service_state(const char *service, enum sol_platform_service_state state)
{
#if (SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT > 0) || defined(ENABLE_DYNAMIC_MODULES)
    struct service_instance *inst;

    if (platform_state == SOL_PLATFORM_STATE_STOPPING) {
        SOL_WRN("doing shutdown process");
        return;
    }

    inst = find_service_instance(service);
    if (!inst)
        return;

    inst->state = state;
#endif
    sol_platform_inform_service_monitors(service, state);
}

int
sol_platform_impl_set_hostname(const char *name)
{
    size_t len = strlen(name);

    if (len > HOST_NAME_MAX) {
        SOL_WRN("Hostname can not be bigger than %d - Hostname:%s",
            HOST_NAME_MAX, name);
        return -EINVAL;
    }

    if (sethostname(name, len) < 0)
        return -errno;
    return 0;
}

static bool
hostname_changed(void *data, int fd, uint32_t active_flags)
{
    sol_platform_inform_hostname_monitors();
    return true;
}

static void
close_fd_monitor(struct sol_fd_watcher_ctx *monitor)
{
    if (!monitor->watcher)
        return;
    sol_fd_del(monitor->watcher);
    close(monitor->fd);
    monitor->fd = -1;
    monitor->watcher = NULL;
}

int
sol_platform_register_hostname_monitor(void)
{
    if (hostname_monitor.watcher)
        return 0;

    hostname_monitor.fd = open("/proc/sys/kernel/hostname", O_RDONLY | O_CLOEXEC);

    if (hostname_monitor.fd < 0)
        return -errno;

    hostname_monitor.watcher = sol_fd_add(hostname_monitor.fd,
        SOL_FD_FLAGS_HUP, hostname_changed, NULL);
    if (!hostname_monitor.watcher) {
        close(hostname_monitor.fd);
        return -ENOMEM;
    }

    return 0;
}

int
sol_platform_unregister_hostname_monitor(void)
{
    close_fd_monitor(&hostname_monitor);
    return 0;
}

int
sol_platform_impl_set_system_clock(int64_t timestamp)
{
    struct timespec spec;

    /* FIXME: We should check if NTP is running and enabled, it if is enable we should not set the time ! */
    spec.tv_sec = (time_t)timestamp;
    spec.tv_nsec = 0;

    if (clock_settime(CLOCK_REALTIME, &spec) < 0) {
        SOL_WRN("Could not set the system time to:%" PRId64, timestamp);
        return -errno;
    }

    return 0;
}

int
sol_platform_impl_set_timezone(const char *timezone)
{
    int r;
    char path[PATH_MAX];
    struct stat st;

    if (timezone[0] == '/' || timezone[0] == '\0') {
        SOL_WRN("Timezone is empty!");
        return -EINVAL;
    }

    r = snprintf(path, sizeof(path), "/usr/share/zoneinfo/%s", timezone);

    if (r < 0 || r >= (int)sizeof(path)) {
        SOL_WRN("Could not create the timezone path for: %s", timezone);
        return -ENOMEM;
    }

    if (stat(path, &st) < 0) {
        SOL_WRN("The zone %s is not present at /usr/share/zoneinfo/", timezone);
        return -errno;
    }

    if (!S_ISREG(st.st_mode)) {
        SOL_WRN("The timezone: %s is not a regular file.", timezone);
        return -EINVAL;
    }

    if (unlink("/etc/localtime") < 0) {
        SOL_WRN("Could not unlink the /etc/localtime when trying to set the timzone to:%s", timezone);
        return -errno;
    }

    if (symlink(path, "/etc/localtime") < 0) {
        SOL_WRN("Could not create the symlink to the timezone %s", timezone);
        return -errno;
    } else {
        /* Check if it was linked to the right place to prevent TOCTOU */
        char buf[PATH_MAX];
        ssize_t len;

        if ((len = readlink("/etc/localtime", buf, sizeof(buf) - 1)) < 0) {
            r = -errno;
            goto symlink_error;
        }
        buf[len] = '\0';
        if (strcmp(path, buf)) {
            r = -EINVAL;
            goto symlink_error;
        }
    }

    return 0;

symlink_error:
    SOL_WRN("Failed to verify link /etc/localtime for timezone: %s",
        timezone);
    if (unlink("/etc/localtime") < 0)
        SOL_WRN("Could not unlink /etc/localtime");
    return r;
}

static bool
timezone_changed(void *data, int fd, uint32_t active_flags)
{
    sol_platform_inform_timezone_changed();
    close(timezone_monitor.fd);
    timezone_monitor.fd = -1;
    timezone_monitor.watcher = NULL;
    sol_platform_register_timezone_monitor();
    return false;
}

static int
add_watch(struct sol_fd_watcher_ctx *monitor, uint32_t inotify_flags,
    const char *path, bool (*cb)(void *data, int fd, uint32_t active_flags))
{
    int r;

    if (monitor->watcher)
        return 0;

    monitor->fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);

    if (monitor->fd < 0) {
        return -errno;
    }

    if (inotify_add_watch(monitor->fd, path, inotify_flags) < 0) {
        r = -errno;
        goto err_exit;
    }

    monitor->watcher = sol_fd_add(monitor->fd,
        SOL_FD_FLAGS_IN, cb, NULL);
    if (!monitor->watcher) {
        r = -ENOMEM;
        goto err_exit;
    }

    return 0;

err_exit:
    close(monitor->fd);
    monitor->fd = -1;
    return r;
}

int
sol_platform_register_timezone_monitor(void)
{
    return add_watch(&timezone_monitor, IN_MODIFY | IN_DONT_FOLLOW,
        "/etc/localtime", timezone_changed);
}

int
sol_platform_unregister_timezone_monitor(void)
{
    close_fd_monitor(&timezone_monitor);
    return 0;
}

int
sol_platform_impl_set_locale(char **locales)
{
    enum sol_platform_locale_category i;
    FILE *f;
    int r;

    f = fopen("/etc/locale.conf", "we");

    if (!f) {
        r = -errno;
        if (r == -ENOENT) {
            SOL_WRN("The locale file (/etc/locale.conf) was not found in the system.");
            return 0;
        } else
            return r;
    }

    for (i = SOL_PLATFORM_LOCALE_LANGUAGE; i <= SOL_PLATFORM_LOCALE_TIME; i++) {
        if (!locales[i])
            continue;
        r = fprintf(f, "%s=%s\n", sol_platform_locale_to_c_str_category(i),
            locales[i]);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

exit:
    if (fclose(f) < 0)
        return -errno;
    return 0;
}

static bool
timeout_locale(void *data)
{
    int r;

    r = sol_platform_register_locale_monitor();
    if (!r) {
        SOL_DBG("Watching /etc/locale.conf again");
        goto unregister;
    }

    if (++locale_monitor.create_attempts == LOCALE_CONF_MAX_CREATE_ATTEMPTS) {
        sol_platform_inform_locale_monitor_error();
        SOL_WRN("/etc/locale.conf was not created. Giving up.");
        goto unregister;
    }

    SOL_DBG("/etc/locale.conf was not created yet, trying again in some time");
    return true;
unregister:
    locale_monitor.create_timeout = NULL;
    sol_platform_inform_locale_changed();
    return false;
}

static bool
locale_changed(void *data, int fd, uint32_t active_flags)
{
    char buf[4096];
    struct inotify_event *event;
    char *ptr;
    ssize_t len;
    bool dispatch_callback, deleted;

    deleted = dispatch_callback = false;

    while (1) {
        len = read(fd, buf, sizeof(buf));

        if (len == -1 && errno != EAGAIN && errno != EINTR) {
            SOL_WRN("Could read the locale.conf inotify. Reason: %d", errno);
            sol_platform_inform_locale_monitor_error();
            close_fd_monitor(&locale_monitor.fd_watcher);
            return false;
        }

        if (len <= 0)
            break;

        for (ptr = buf; ptr < buf + len;
            ptr += sizeof(struct inotify_event) + event->len) {

            event = (struct inotify_event *)ptr;

            if (event->mask & IN_MODIFY) {
                SOL_DBG("locale.conf changed");
                dispatch_callback = true;
            }
            if (event->mask & IN_DELETE_SELF) {
                SOL_DBG("locale.conf was moved");
                deleted = true;
            }
        }
    }

    if (deleted) {
        close_fd_monitor(&locale_monitor.fd_watcher);
        //One second from now, check if a new locale has been created.
        locale_monitor.create_timeout =
            sol_timeout_add(1000, timeout_locale, NULL);
        locale_monitor.create_attempts = 0;
        if (!locale_monitor.create_timeout) {
            SOL_WRN("Could not create a timer to check if"
                " a new /etc/locale.conf has been created.");
            sol_platform_inform_locale_monitor_error();
        }
    } else if (dispatch_callback)
        sol_platform_inform_locale_changed();
    return !deleted;
}

int
sol_platform_register_locale_monitor(void)
{
    return add_watch(&locale_monitor.fd_watcher, IN_MODIFY | IN_DELETE_SELF,
        "/etc/locale.conf", locale_changed);
}

int
sol_platform_unregister_locale_monitor(void)
{
    close_fd_monitor(&locale_monitor.fd_watcher);
    if (locale_monitor.create_timeout)
        sol_timeout_del(locale_monitor.create_timeout);
    return 0;
}
