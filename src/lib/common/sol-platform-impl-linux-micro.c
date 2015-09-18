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
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sol-file-reader.h"
#include "sol-mainloop.h"
#include "sol-platform-impl.h"
#include "sol-platform-linux-micro.h"
#include "sol-platform.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "sol-platform-linux-micro-builtins-gen.h"

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
    if ((*p_sym)->api_version != SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION) {
        SOL_WRN("module '%s' has incorrect api_version: %lu expected %lu",
            path, (*p_sym)->api_version, SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION);
        goto error;
    }

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
    sol_vector_del(&service_modules, service_modules.len - 1);
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
    sol_vector_del(&service_instances, service_instances.len - 1);
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
        if (isspace(*p) && start < p) {
            err = load_initial_services_entry(start, p - start);
            start = p + 1;
        }
    }
    if (err == 0 && start < end)
        err = load_initial_services_entry(start, end - start); ;

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

    for (itr = paths; itr < paths + ARRAY_SIZE(paths); itr++) {
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

    for (mnt = mount_table; mnt < mount_table + ARRAY_SIZE(mount_table); mnt++) {
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

    for (sym = symlink_table; sym < symlink_table + ARRAY_SIZE(symlink_table); sym++) {
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

            for (idx = 0; idx < ARRAY_SIZE(mount_table); idx++) {
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

int
sol_platform_impl_init(void)
{
    bool want_load_initial_services = false;

    if (getpid() == 1 && getppid() == 0) {
        int err = setup_pid1();
        SOL_INT_CHECK(err, < 0, err);

        want_load_initial_services = true;
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
    if (streq(target, SOL_PLATFORM_TARGET_POWEROFF))
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
validate_machine_id(char id[static 33])
{
    if (!sol_util_uuid_str_valid(id))
        return -EINVAL;

    return 0;
}

int
sol_platform_impl_get_machine_id(char id[static 33])
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
