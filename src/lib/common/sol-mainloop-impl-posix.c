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

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

// TODO MAYBE:
// if HAVE_GLIB then integrate with glib's main loop so gtk works?

#include "sol-mainloop-impl.h"
#include "sol-vector.h"
#include "sol-util.h"

static bool run_loop;

static bool timeout_processing;
static unsigned int timeout_pending_deletion;
static struct sol_ptr_vector timeout_vector = SOL_PTR_VECTOR_INIT;

static bool idler_processing;
static unsigned int idler_pending_deletion;
static struct sol_ptr_vector idler_vector = SOL_PTR_VECTOR_INIT;

static bool child_watch_processing;
static unsigned int child_watch_pending_deletion;
static struct sol_ptr_vector child_watch_vector = SOL_PTR_VECTOR_INIT;

static bool fd_processing;
static bool fd_changed;
static unsigned int fd_pending_deletion;
static struct sol_ptr_vector fd_vector = SOL_PTR_VECTOR_INIT;

static struct pollfd *pollfds;
static unsigned pollfds_count;
static unsigned pollfds_used;
#define POLLFDS_COUNT_BLOCKSIZE (32)

struct sol_timeout_posix {
    struct timespec timeout;
    struct timespec expire;
    const void *data;
    bool (*cb)(void *data);
    bool remove_me;
};

struct sol_idler_posix {
    const void *data;
    bool (*cb)(void *data);
    enum { idler_ready, idler_deleted, idler_ready_on_next_iteration } status;
};

struct sol_child_watch_posix {
    const void *data;
    void (*cb)(void *data, uint64_t pid, int status);
    pid_t pid;
    bool remove_me;
};

struct sol_fd_posix {
    const void *data;
    bool (*cb)(void *data, int fd, unsigned int active_flags);
    int fd;
    unsigned int flags;
    bool remove_me;
    bool invalid;
};

struct child_exit_status {
    pid_t pid;
    int status;
};

static struct sol_vector child_exit_status_vector = SOL_VECTOR_INIT(struct child_exit_status);

static int
timeout_compare(const void *data1, const void *data2)
{
    const struct sol_timeout_posix *a = data1, *b = data2;

    return sol_util_timespec_compare(&a->expire, &b->expire);
}

#ifdef HAVE_PTHREAD_H
#include <pthread.h>

#define SIGPROCMASK pthread_sigmask

static int pipe_fds[2];
static pthread_t main_thread;
static bool have_notified;
static struct sol_fd *ack_handler;

#define MAIN_THREAD_CHECK_RETURN                                        \
    do {                                                                \
        if (pthread_self() != main_thread) {                            \
            SOL_ERR("sol_run() called on different thread than sol_init()"); \
            return;                                                     \
        }                                                               \
    } while (0)

static struct sol_ptr_vector timeout_v_process = SOL_PTR_VECTOR_INIT;
static struct sol_ptr_vector idler_v_process = SOL_PTR_VECTOR_INIT;
static struct sol_ptr_vector child_watch_v_process = SOL_PTR_VECTOR_INIT;
static struct sol_ptr_vector fd_v_process = SOL_PTR_VECTOR_INIT;

#define TIMEOUT_PROCESS timeout_v_process
#define TIMEOUT_ACUM timeout_vector
#define IDLER_PROCESS idler_v_process
#define IDLER_ACUM idler_vector
#define CHILD_WATCH_PROCESS child_watch_v_process
#define CHILD_WATCH_ACUM child_watch_vector
#define FD_PROCESS fd_v_process
#define FD_ACUM fd_vector

/* protects all mainloop bookkeeping data structures */
static pthread_mutex_t ml_lock = PTHREAD_MUTEX_INITIALIZER;

#define mainloop_ds_lock() pthread_mutex_lock(&ml_lock)
#define mainloop_ds_unlock() pthread_mutex_unlock(&ml_lock)

#define run_loop_get() __atomic_load_n(&run_loop, __ATOMIC_SEQ_CST)
#define run_loop_set(val) __atomic_store_n(&run_loop, (val), __ATOMIC_SEQ_CST)

static inline void
ptr_vector_take(struct sol_ptr_vector *to, struct sol_ptr_vector *from)
{
    *to = *from;
    sol_ptr_vector_init(from);
}

static inline void
ptr_vector_update(struct sol_ptr_vector *to, struct sol_ptr_vector *from)
{
    void *itr;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (from, itr, i)
        sol_ptr_vector_append(to, itr);
    sol_ptr_vector_clear(from);
}

static inline void
timeout_vector_update(struct sol_ptr_vector *to, struct sol_ptr_vector *from)
{
    struct sol_timeout_posix *itr;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (from, itr, i)
        sol_ptr_vector_insert_sorted(to, itr, timeout_compare);
    sol_ptr_vector_clear(from);
}

#define main_thread_check_notify()              \
    do {                                        \
        if (pthread_self() != main_thread)      \
            main_thread_notify();               \
    } while (0)

/* mostly called with mainloop lock HELD, but
   may be called from signal handler context */
static inline void
main_thread_notify(void)
{
    char tok = 'w';
    int r;

    if (__atomic_test_and_set(&have_notified, __ATOMIC_SEQ_CST))
        return;

    r = write(pipe_fds[1], &tok, sizeof(tok));
    SOL_INT_CHECK(r, != 1);
}

static inline bool
main_thread_ack(void *data, int fd, unsigned int active_flags)
{
    char tok;
    int r;

    SOL_EXP_CHECK(active_flags & SOL_FD_FLAGS_ERR, true);
    SOL_EXP_CHECK(!(active_flags & SOL_FD_FLAGS_IN), true);

    mainloop_ds_lock();

    r = read(fd, &tok, sizeof(tok));
    __atomic_clear(&have_notified, __ATOMIC_SEQ_CST);

    mainloop_ds_unlock();

    SOL_INT_CHECK(r, != 1, true);
    return true;
}

static inline void
threads_init(void)
{
    int err;

    err = sol_create_pipe(pipe_fds, 0);
    SOL_INT_CHECK(err, != 0);

    main_thread = pthread_self();

    ack_handler = sol_fd_add(pipe_fds[0], SOL_FD_FLAGS_IN, main_thread_ack, NULL);
    SOL_NULL_CHECK(ack_handler);
}

static inline void
threads_shutdown(void)
{
    sol_fd_del(ack_handler);
    main_thread = 0;
    close(pipe_fds[1]);
    close(pipe_fds[0]);
}

#else  /* !HAVE_PTHREAD_H */

#define SIGPROCMASK sigprocmask
#define MAIN_THREAD_CHECK_RETURN do { } while (0)
#define TIMEOUT_PROCESS timeout_vector
#define TIMEOUT_ACUM nothing
#define IDLER_PROCESS idler_vector
#define IDLER_ACUM nothing
#define CHILD_WATCH_PROCESS child_watch_vector
#define CHILD_WATCH_ACUM nothing
#define FD_PROCESS fd_vector
#define FD_ACUM nothing

#define mainloop_ds_lock() do { } while (0)
#define mainloop_ds_unlock() do { } while (0)

#define run_loop_get() (run_loop)
#define run_loop_set(val) run_loop = (val)

#define ptr_vector_take(...) do { } while (0)
#define ptr_vector_update(...) do { } while (0)
#define timeout_vector_update(...) do { } while (0)

#define main_thread_check_notify() do { } while (0)
#define main_thread_notify() do { } while (0)
#define threads_init() do { } while (0)
#define threads_shutdown() do { } while (0)

#endif

static struct child_exit_status *
find_child_exit_status(pid_t pid)
{
    struct child_exit_status *itr;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&child_exit_status_vector, itr, i) {
        if (itr->pid == pid)
            return itr;
    }

    return NULL;
}

static void
on_sig_child(const siginfo_t *info)
{
    struct child_exit_status *cs;

    SOL_DBG("child %" PRIu64 " exited with status %d, "
        "stime=%" PRIu64 ", utime=%" PRIu64 ", uid=%" PRIu64,
        (uint64_t)info->si_pid,
        info->si_status,
        (uint64_t)info->si_stime,
        (uint64_t)info->si_utime,
        (uint64_t)info->si_uid);

    cs = find_child_exit_status(info->si_pid);
    if (!cs) {
        cs = sol_vector_append(&child_exit_status_vector);
        SOL_NULL_CHECK(cs);
        cs->pid = info->si_pid;
    }
    cs->status = info->si_status;
}

static void
on_sig_quit(const siginfo_t *info)
{
    SOL_DBG("got signal %d, quit main loop...", info->si_signo);
    sol_quit();
}

static void
on_sig_debug(const siginfo_t *info)
{
    if (SOL_LOG_LEVEL_POSSIBLE(SOL_LOG_LEVEL_DEBUG)) {
        char errmsg[1024] = "Success";

        if (info->si_errno)
            sol_util_strerror(info->si_errno, errmsg, sizeof(errmsg));

        SOL_DBG("got signal %d, errno %d (%s), code %d. ignored.",
            info->si_signo,
            info->si_errno,
            errmsg,
            info->si_code);
    }
}

struct siginfo_handler {
    void (*cb)(const siginfo_t *info);
    int sig;
};

static const struct siginfo_handler siginfo_handler[] = {
#define SIG(num, handler) { .sig = num, .cb = handler }
    SIG(SIGALRM, NULL),
    SIG(SIGCHLD, on_sig_child),
    SIG(SIGHUP, NULL),
    SIG(SIGINT, on_sig_quit),
    SIG(SIGPIPE, NULL),
    SIG(SIGQUIT, on_sig_quit),
    SIG(SIGTERM, on_sig_quit),
    SIG(SIGUSR1, NULL),
    SIG(SIGUSR2, NULL),
#undef SIG
};
#define SIGINFO_HANDLER_COUNT ARRAY_SIZE(siginfo_handler)

static struct sigaction sa_orig[SIGINFO_HANDLER_COUNT];
static sigset_t sig_blockset, sig_origset;

#define SIGINFO_STORAGE_CAPACITY 64
static siginfo_t siginfo_storage[SIGINFO_STORAGE_CAPACITY];
static unsigned char siginfo_storage_used;

#define SIGINFO_HANDLER_FOREACH(ptr) \
    for (ptr = siginfo_handler; ptr < siginfo_handler + SIGINFO_HANDLER_COUNT; ptr++) if (ptr->sig)

static void
sighandler(int sig, siginfo_t *si, void *context)
{
    siginfo_t def_si = { .si_signo = sig };
    siginfo_t *sis;

    if (siginfo_storage_used >= SIGINFO_STORAGE_CAPACITY) {
        SOL_WRN("no storage to catch signal %d", sig);
        return;
    }
    sis = siginfo_storage + siginfo_storage_used;
    siginfo_storage_used++;

    if (!si)
        si = &def_si;

    memcpy(sis, si, sizeof(*si));
}

static void *
signals_find_handler(int sig)
{
    void (*cb)(const siginfo_t *) = NULL;
    const struct siginfo_handler *sih;

    SIGINFO_HANDLER_FOREACH(sih) {
        if (sih->sig == sig) {
            cb = sih->cb;
            break;
        }
    }

    if (SOL_LOG_LEVEL_POSSIBLE(SOL_LOG_LEVEL_DEBUG)) {
        if (!cb)
            cb = on_sig_debug;
    }

    return cb;
}

static void
signals_process(void)
{
    unsigned char i;

    SIGPROCMASK(SIG_BLOCK, &sig_blockset, NULL);

    for (i = 0; i < siginfo_storage_used; i++) {
        const siginfo_t *info = siginfo_storage + i;
        void (*cb)(const siginfo_t *) = signals_find_handler(info->si_signo);
        if (cb)
            cb(info);
    }
    siginfo_storage_used = 0;

    SIGPROCMASK(SIG_UNBLOCK, &sig_blockset, NULL);

    do {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;
        SOL_DBG("collected finished pid=%" PRIu64 ", status=%d",
            (uint64_t)pid, status);
    } while (1);
}

int
sol_mainloop_impl_init(void)
{
    const struct siginfo_handler *sih;
    unsigned int i;

    sigemptyset(&sig_blockset);
    SIGINFO_HANDLER_FOREACH(sih)
    sigaddset(&sig_blockset, sih->sig);

    sigemptyset(&sig_origset);
    SIGPROCMASK(SIG_BLOCK, NULL, &sig_origset);

    i = 0;
    SIGINFO_HANDLER_FOREACH(sih) {
        struct sigaction sa;

        sa.sa_sigaction = sighandler;
        sa.sa_flags = SA_RESTART | SA_SIGINFO;
        sa.sa_mask = sig_blockset;
        sigaction(sih->sig, &sa, sa_orig + i);
        i++;
    }

    threads_init();

    return 0;
}

void
sol_mainloop_impl_shutdown(void)
{
    const struct siginfo_handler *sih;
    void *ptr;
    uint16_t i;

    threads_shutdown();

    SOL_PTR_VECTOR_FOREACH_IDX (&timeout_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&timeout_vector);

    SOL_PTR_VECTOR_FOREACH_IDX (&idler_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&idler_vector);

    SOL_PTR_VECTOR_FOREACH_IDX (&child_watch_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&child_watch_vector);

    SOL_PTR_VECTOR_FOREACH_IDX (&fd_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&fd_vector);

    free(pollfds);
    pollfds = NULL;
    pollfds_count = 0;

    i = 0;
    SIGINFO_HANDLER_FOREACH(sih) {
        sigaction(sih->sig, sa_orig + i, NULL);
        i++;
    }
    sigprocmask(SIG_SETMASK, &sig_origset, NULL);
}

/* called with mainloop lock HELD */
static inline void
timeout_cleanup(void)
{
    struct sol_timeout_posix *timeout;
    uint16_t i;

    if (!timeout_pending_deletion)
        return;

    // Walk backwards so deletion doesn't impact the indices.
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&timeout_vector, timeout, i) {
        if (!timeout->remove_me)
            continue;

        sol_ptr_vector_del(&timeout_vector, i);
        free(timeout);
        timeout_pending_deletion--;
        if (!timeout_pending_deletion)
            break;
    }
}

static inline void
timeout_process(void)
{
    struct timespec now;
    unsigned int i;

    mainloop_ds_lock();
    ptr_vector_take(&TIMEOUT_PROCESS, &TIMEOUT_ACUM);
    timeout_processing = true;
    mainloop_ds_unlock();

    now = sol_util_timespec_get_current();
    for (i = 0; i < TIMEOUT_PROCESS.base.len; i++) {
        struct sol_timeout_posix *timeout = sol_ptr_vector_get(&TIMEOUT_PROCESS, i);
        if (!run_loop_get())
            break;
        if (timeout->remove_me)
            continue;
        if (sol_util_timespec_compare(&timeout->expire, &now) > 0)
            break;

        if (!timeout->cb((void *)timeout->data)) {
            mainloop_ds_lock();
            if (!timeout->remove_me) {
                timeout->remove_me = true;
                timeout_pending_deletion++;
            }
            mainloop_ds_unlock();
            continue;
        }

        sol_util_timespec_sum(&now, &timeout->timeout, &timeout->expire);
        sol_ptr_vector_del(&TIMEOUT_PROCESS, i);
        sol_ptr_vector_insert_sorted(&TIMEOUT_PROCESS, timeout, timeout_compare);
        i--;
    }

    mainloop_ds_lock();
    timeout_vector_update(&TIMEOUT_ACUM, &TIMEOUT_PROCESS);
    timeout_cleanup();
    timeout_processing = false;
    mainloop_ds_unlock();
}

/* called with mainloop lock HELD */
static inline struct sol_timeout_posix *
timeout_first(void)
{
    struct sol_timeout_posix *timeout;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&timeout_vector, timeout, i) {
        if (timeout->remove_me)
            continue;
        return timeout;
    }
    return NULL;
}

/* called with mainloop lock HELD */
static inline void
idler_cleanup(void)
{
    struct sol_idler_posix *idler;
    uint16_t i;

    if (!idler_pending_deletion)
        return;

    // Walk backwards so deletion doesn't impact the indices.
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&idler_vector, idler, i) {
        if (idler->status != idler_deleted)
            continue;

        sol_ptr_vector_del(&idler_vector, i);
        free(idler);
        idler_pending_deletion--;
        if (!idler_pending_deletion)
            break;
    }
}

static inline void
idler_process(void)
{
    struct sol_idler_posix *idler;
    uint16_t i;

    mainloop_ds_lock();
    ptr_vector_take(&IDLER_PROCESS, &IDLER_ACUM);
    idler_processing = true;
    mainloop_ds_unlock();

    SOL_PTR_VECTOR_FOREACH_IDX (&IDLER_PROCESS, idler, i) {
        if (!run_loop_get())
            break;
        if (idler->status != idler_ready) {
            if (idler->status == idler_ready_on_next_iteration)
                idler->status = idler_ready;
            continue;
        }
        if (!idler->cb((void *)idler->data)) {
            mainloop_ds_lock();
            if (idler->status != idler_deleted) {
                idler->status = idler_deleted;
                idler_pending_deletion++;
            }
            mainloop_ds_unlock();
        }
        timeout_process();
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&IDLER_PROCESS, idler, i) {
        if (idler->status == idler_ready_on_next_iteration) {
            idler->status = idler_ready;
        }
    }

    mainloop_ds_lock();
    ptr_vector_update(&IDLER_ACUM, &IDLER_PROCESS);
    idler_cleanup();
    idler_processing = false;
    mainloop_ds_unlock();
}

/* called with mainloop lock HELD */
static inline struct sol_idler_posix *
idler_first(void)
{
    struct sol_idler_posix *idler;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&idler_vector, idler, i) {
        if (idler->status == idler_deleted)
            continue;
        return idler;
    }
    return NULL;
}

/* called with mainloop lock HELD */
static inline void
child_watch_cleanup(void)
{
    struct sol_child_watch_posix *child_watch;
    uint16_t i;

    if (!child_watch_pending_deletion)
        return;

    // Walk backwards so deletion doesn't impact the indices.
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&child_watch_vector, child_watch, i) {
        if (!child_watch->remove_me)
            continue;

        sol_ptr_vector_del(&child_watch_vector, i);
        free(child_watch);
        child_watch_pending_deletion--;
        if (!child_watch_pending_deletion)
            break;
    }
}

static inline void
child_watch_process(void)
{
    struct sol_child_watch_posix *child_watch;
    uint16_t i;

    mainloop_ds_lock();
    ptr_vector_take(&CHILD_WATCH_PROCESS, &CHILD_WATCH_ACUM);
    child_watch_processing = true;
    mainloop_ds_unlock();

    SOL_PTR_VECTOR_FOREACH_IDX (&CHILD_WATCH_PROCESS, child_watch, i) {
        const struct child_exit_status *cs;
        if (!run_loop_get())
            break;
        if (child_watch->remove_me)
            continue;
        cs = find_child_exit_status(child_watch->pid);
        if (!cs)
            continue;
        child_watch->cb((void *)child_watch->data, cs->pid, cs->status);
        mainloop_ds_lock();
        if (!child_watch->remove_me) {
            child_watch->remove_me = true;
            child_watch_pending_deletion++;
        }
        mainloop_ds_unlock();

        timeout_process();
    }

    sol_vector_clear(&child_exit_status_vector);

    mainloop_ds_lock();
    ptr_vector_update(&CHILD_WATCH_ACUM, &CHILD_WATCH_PROCESS);
    child_watch_cleanup();
    child_watch_processing = false;
    mainloop_ds_unlock();
}

static short int
fd_flags_to_poll_events(unsigned int flags)
{
    short int events = 0;

#define MAP(a, b) if (flags & a) events |= b

    MAP(SOL_FD_FLAGS_IN, POLLIN);
    MAP(SOL_FD_FLAGS_OUT, POLLOUT);
    MAP(SOL_FD_FLAGS_PRI, POLLPRI);
    MAP(SOL_FD_FLAGS_ERR, POLLERR);
    MAP(SOL_FD_FLAGS_HUP, POLLHUP);
    MAP(SOL_FD_FLAGS_NVAL, POLLNVAL);

#undef MAP

    return events;
}

static unsigned int
poll_events_to_fd_flags(short int events)
{
    unsigned int flags = 0;

#define MAP(a, b) if (events & b) flags |= a

    MAP(SOL_FD_FLAGS_IN, POLLIN);
    MAP(SOL_FD_FLAGS_OUT, POLLOUT);
    MAP(SOL_FD_FLAGS_PRI, POLLPRI);
    MAP(SOL_FD_FLAGS_ERR, POLLERR);
    MAP(SOL_FD_FLAGS_HUP, POLLHUP);
    MAP(SOL_FD_FLAGS_NVAL, POLLNVAL);

#undef MAP

    return flags;
}

/* called with mainloop lock HELD */
static inline void
fd_prepare(void)
{
    const struct sol_fd_posix *handler;
    unsigned int fds, new_count, nfds;
    uint16_t i;

    if (!fd_changed)
        return;

    fds = sol_ptr_vector_get_len(&fd_vector);
    new_count = ((fds / POLLFDS_COUNT_BLOCKSIZE) + 1) * POLLFDS_COUNT_BLOCKSIZE;

    if (pollfds_count != new_count) {
        void *tmp = realloc(pollfds, (size_t)new_count * sizeof(struct pollfd));
        SOL_NULL_CHECK(tmp);
        pollfds = tmp;
        pollfds_count = new_count;
    }

    nfds = 0;
    SOL_PTR_VECTOR_FOREACH_IDX (&fd_vector, handler, i) {
        struct pollfd *pfd;
        if (handler->remove_me || handler->invalid)
            continue;
        pfd = pollfds + nfds;
        nfds++;
        pfd->fd = handler->fd;
        pfd->events = fd_flags_to_poll_events(handler->flags);
    }

    pollfds_used = nfds;
    fd_changed = false;
}

/* called with mainloop lock HELD */
static inline void
fd_cleanup(void)
{
    struct sol_fd_posix *fd;
    uint16_t i;

    if (!fd_pending_deletion)
        return;

    // Walk backwards so deletion doesn't impact the indices.
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&fd_vector, fd, i) {
        if (!fd->remove_me)
            continue;

        sol_ptr_vector_del(&fd_vector, i);
        free(fd);
        fd_pending_deletion--;
        if (!fd_pending_deletion)
            break;
    }
}

#ifndef HAVE_PPOLL
int
ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts, const sigset_t *sigmask)
{
    int timeout_ms, ret;
    sigset_t origmask;

    if (timeout_ts)
        timeout_ms = sol_util_msec_from_timespec(timeout_ts);
    else
        timeout_ms = -1;

    if (sigmask)
        sigprocmask(SIG_SETMASK, sigmask, &origmask);

    ret = poll(fds, nfds, timeout_ms);

    if (sigmask)
        sigprocmask(SIG_SETMASK, &origmask, NULL);

    return ret;
}
#endif

static inline void
fd_process(void)
{
    struct sol_fd_posix *handler;
    struct timespec now, diff;
    sigset_t emptyset;
    uint16_t i, j;
    bool use_diff;
    int nfds;

    if (!run_loop_get())
        return;

    mainloop_ds_lock();

    fd_prepare();

    if (idler_first()) {
        use_diff = true;
        diff.tv_sec = 0;
        diff.tv_nsec = 0;
    } else {
        struct sol_timeout_posix *timeout = timeout_first();
        if (!timeout)
            use_diff = false;
        else {
            use_diff = true;
            now = sol_util_timespec_get_current();
            sol_util_timespec_sub(&timeout->expire, &now, &diff);
            if (diff.tv_sec < 0) {
                diff.tv_sec = 0;
                diff.tv_nsec = 0;
            }
        }
    }
    sigemptyset(&emptyset);

    mainloop_ds_unlock();

    nfds = ppoll(pollfds, pollfds_used, use_diff ? &diff : NULL, &emptyset);

    mainloop_ds_lock();
    ptr_vector_take(&FD_PROCESS, &FD_ACUM);
    fd_processing = true;
    mainloop_ds_unlock();

    j = 0;
    SOL_PTR_VECTOR_FOREACH_IDX (&FD_PROCESS, handler, i) {
        unsigned int active_flags;
        const struct pollfd *pfd;

        if (!run_loop_get())
            break;

        if (nfds <= 0)
            break;

        if (handler->remove_me || handler->invalid)
            continue;

        for (; j < pollfds_used; j++) {
            if (pollfds[j].fd == handler->fd)
                break;
        }
        if (j >= pollfds_used)
            break;

        pfd = pollfds + j;
        j++;

        active_flags = poll_events_to_fd_flags(pfd->revents);
        if (!active_flags)
            continue;

        nfds--;
        if (!handler->cb((void *)handler->data, handler->fd, active_flags)) {
            mainloop_ds_lock();
            if (!handler->remove_me) {
                handler->remove_me = true;
                fd_pending_deletion++;
                fd_changed = true;
            }
            mainloop_ds_unlock();
        }

        timeout_process();
    }

    mainloop_ds_lock();
    ptr_vector_update(&FD_ACUM, &FD_PROCESS);
    fd_cleanup();
    fd_processing = false;
    mainloop_ds_unlock();
}

void
sol_mainloop_impl_run(void)
{
    MAIN_THREAD_CHECK_RETURN;

    run_loop_set(true);
    while (run_loop_get()) {
        timeout_process();
        fd_process();
        signals_process();
        child_watch_process();
        idler_process();
    }
}

void
sol_mainloop_impl_quit(void)
{
    run_loop_set(false);
    main_thread_notify();
}

void *
sol_mainloop_impl_timeout_add(unsigned int timeout_ms, bool (*cb)(void *data), const void *data)
{
    struct timespec now;
    int ret;
    struct sol_timeout_posix *timeout = malloc(sizeof(struct sol_timeout_posix));

    SOL_NULL_CHECK(timeout, NULL);

    mainloop_ds_lock();

    timeout->timeout.tv_sec = timeout_ms / MSEC_PER_SEC;
    timeout->timeout.tv_nsec = (timeout_ms % MSEC_PER_SEC) * NSEC_PER_MSEC;
    timeout->cb = cb;
    timeout->data = data;
    timeout->remove_me = false;

    now = sol_util_timespec_get_current();
    sol_util_timespec_sum(&now, &timeout->timeout, &timeout->expire);
    ret = sol_ptr_vector_insert_sorted(&timeout_vector, timeout, timeout_compare);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);

    main_thread_check_notify();
    mainloop_ds_unlock();

    return timeout;

clean:
    mainloop_ds_unlock();
    free(timeout);
    return NULL;
}

bool
sol_mainloop_impl_timeout_del(void *handle)
{
    struct sol_timeout_posix *timeout = handle;

    mainloop_ds_lock();

    timeout->remove_me = true;
    timeout_pending_deletion++;
    if (!timeout_processing)
        timeout_cleanup();

    mainloop_ds_unlock();

    return true;
}

void *
sol_mainloop_impl_idle_add(bool (*cb)(void *data), const void *data)
{
    int ret;
    struct sol_idler_posix *idler = malloc(sizeof(struct sol_idler_posix));

    SOL_NULL_CHECK(idler, NULL);

    mainloop_ds_lock();

    idler->cb = cb;
    idler->data = data;

    idler->status = idler_processing ? idler_ready_on_next_iteration : idler_ready;
    ret = sol_ptr_vector_append(&idler_vector, idler);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);

    main_thread_check_notify();
    mainloop_ds_unlock();

    return idler;

clean:
    mainloop_ds_unlock();
    free(idler);
    return NULL;
}

bool
sol_mainloop_impl_idle_del(void *handle)
{
    struct sol_idler_posix *idler = handle;

    mainloop_ds_lock();

    idler->status = idler_deleted;
    idler_pending_deletion++;
    if (!idler_processing)
        idler_cleanup();

    mainloop_ds_unlock();

    return true;
}

void *
sol_mainloop_impl_fd_add(int fd, unsigned int flags, bool (*cb)(void *data, int fd, unsigned int active_flags), const void *data)
{
    struct sol_fd_posix *handle = malloc(sizeof(struct sol_fd_posix));
    int ret;

    SOL_NULL_CHECK(handle, NULL);

    mainloop_ds_lock();

    handle->fd = fd;
    handle->flags = flags;
    handle->cb = cb;
    handle->data = data;
    handle->remove_me = false;
    handle->invalid = false;

    ret = sol_ptr_vector_append(&fd_vector, handle);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);
    fd_changed = true;

    main_thread_check_notify();
    mainloop_ds_unlock();

    return handle;

clean:
    mainloop_ds_unlock();
    free(handle);
    return NULL;
}

bool
sol_mainloop_impl_fd_del(void *handle)
{
    struct sol_fd_posix *fd = handle;

    mainloop_ds_lock();

    fd->remove_me = true;
    fd_pending_deletion++;
    fd_changed = true;
    if (!fd_processing)
        fd_cleanup();

    mainloop_ds_unlock();

    return true;
}

void *
sol_mainloop_impl_child_watch_add(uint64_t pid, void (*cb)(void *data, uint64_t pid, int status), const void *data)
{
    struct sol_child_watch_posix *child_watch = malloc(sizeof(*child_watch));
    int ret;

    SOL_NULL_CHECK(child_watch, NULL);

    mainloop_ds_lock();

    child_watch->pid = pid;
    child_watch->cb = cb;
    child_watch->data = data;
    child_watch->remove_me = false;

    ret = sol_ptr_vector_append(&child_watch_vector, child_watch);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);

    main_thread_check_notify();
    mainloop_ds_unlock();

    return child_watch;

clean:
    mainloop_ds_unlock();
    free(child_watch);
    return NULL;
}

bool
sol_mainloop_impl_child_watch_del(void *handle)
{
    struct sol_child_watch_posix *child_watch = handle;

    mainloop_ds_lock();

    child_watch->remove_me = true;
    child_watch_pending_deletion++;
    if (!child_watch_processing)
        child_watch_cleanup();

    mainloop_ds_unlock();

    return true;
}
