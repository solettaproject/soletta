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
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

#include "sol-mainloop-common.h"
#include "sol-mainloop-impl.h"
#include "sol-vector.h"

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

#ifdef PTHREAD
#include <pthread.h>

#define SIGPROCMASK pthread_sigmask

static int pipe_fds[2];
static pthread_t main_thread;
static bool have_notified;
static struct sol_fd *ack_handler;

static struct sol_ptr_vector child_watch_v_process = SOL_PTR_VECTOR_INIT;
static struct sol_ptr_vector fd_v_process = SOL_PTR_VECTOR_INIT;

#define CHILD_WATCH_PROCESS child_watch_v_process
#define CHILD_WATCH_ACUM child_watch_vector
#define FD_PROCESS fd_v_process
#define FD_ACUM fd_vector

/* protects all mainloop bookkeeping data structures */
static pthread_mutex_t ml_lock = PTHREAD_MUTEX_INITIALIZER;

void
sol_mainloop_impl_lock(void)
{
    pthread_mutex_lock(&ml_lock);
}

void
sol_mainloop_impl_unlock(void)
{
    pthread_mutex_unlock(&ml_lock);
}

bool
sol_mainloop_impl_main_thread_check(void)
{
    return pthread_self() == main_thread;
}

/* mostly called with mainloop lock HELD, but
   may be called from signal handler context */
void
sol_mainloop_impl_main_thread_notify(void)
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

    sol_mainloop_impl_lock();

    r = read(fd, &tok, sizeof(tok));
    __atomic_clear(&have_notified, __ATOMIC_SEQ_CST);

    sol_mainloop_impl_unlock();

    SOL_INT_CHECK(r, != 1, true);
    return true;
}

static inline void
threads_init(void)
{
    int err;

    err = pipe(pipe_fds);
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

#else  /* !PTHREAD */

#define SIGPROCMASK sigprocmask

#define CHILD_WATCH_PROCESS child_watch_vector
#define CHILD_WATCH_ACUM child_watch_vector
#define FD_PROCESS fd_vector
#define FD_ACUM fd_vector

void
sol_mainloop_impl_lock(void)
{
}

void
sol_mainloop_impl_unlock(void)
{
}

bool
sol_mainloop_impl_main_thread_check(void)
{
    return true;
}

void
sol_mainloop_impl_main_thread_notify(void)
{
}

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

#ifdef PTHREAD
void sol_mainloop_posix_signals_block(void);
void sol_mainloop_posix_signals_unblock(void);

/* used externally by worker threads management */
void
sol_mainloop_posix_signals_block(void)
{
    pthread_sigmask(SIG_BLOCK, &sig_blockset, NULL);
}

void
sol_mainloop_posix_signals_unblock(void)
{
    pthread_sigmask(SIG_UNBLOCK, &sig_blockset, NULL);
}
#endif

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
sol_mainloop_impl_platform_init(void)
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
sol_mainloop_impl_platform_shutdown(void)
{
    const struct siginfo_handler *sih;
    void *ptr;
    uint16_t i;

    threads_shutdown();

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
    SIGPROCMASK(SIG_SETMASK, &sig_origset, NULL);
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

    sol_mainloop_impl_lock();
    sol_ptr_vector_steal(&CHILD_WATCH_PROCESS, &CHILD_WATCH_ACUM);
    child_watch_processing = true;
    sol_mainloop_impl_unlock();

    SOL_PTR_VECTOR_FOREACH_IDX (&CHILD_WATCH_PROCESS, child_watch, i) {
        const struct child_exit_status *cs;
        if (!sol_mainloop_common_loop_check())
            break;
        if (child_watch->remove_me)
            continue;
        cs = find_child_exit_status(child_watch->pid);
        if (!cs)
            continue;
        child_watch->cb((void *)child_watch->data, cs->pid, cs->status);
        sol_mainloop_impl_lock();
        if (!child_watch->remove_me) {
            child_watch->remove_me = true;
            child_watch_pending_deletion++;
        }
        sol_mainloop_impl_unlock();

        sol_mainloop_common_timeout_process();
    }

    sol_vector_clear(&child_exit_status_vector);

    sol_mainloop_impl_lock();
    sol_ptr_vector_update(&CHILD_WATCH_ACUM, &CHILD_WATCH_PROCESS);
    child_watch_cleanup();
    child_watch_processing = false;
    sol_mainloop_impl_unlock();
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

    if (!sol_mainloop_common_loop_check())
        return;

    sol_mainloop_impl_lock();

    fd_prepare();

    if (sol_mainloop_common_idler_first()) {
        use_diff = true;
        diff.tv_sec = 0;
        diff.tv_nsec = 0;
    } else {
        struct sol_timeout_common *timeout = sol_mainloop_common_timeout_first();
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

    sol_mainloop_impl_unlock();

    nfds = ppoll(pollfds, pollfds_used, use_diff ? &diff : NULL, &emptyset);

    sol_mainloop_impl_lock();
    sol_ptr_vector_steal(&FD_PROCESS, &FD_ACUM);
    fd_processing = true;
    sol_mainloop_impl_unlock();

    j = 0;
    SOL_PTR_VECTOR_FOREACH_IDX (&FD_PROCESS, handler, i) {
        unsigned int active_flags;
        const struct pollfd *pfd;

        if (!sol_mainloop_common_loop_check())
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
            sol_mainloop_impl_lock();
            if (!handler->remove_me) {
                handler->remove_me = true;
                fd_pending_deletion++;
                fd_changed = true;
            }
            sol_mainloop_impl_unlock();
        }

        sol_mainloop_common_timeout_process();
    }

    sol_mainloop_impl_lock();
    sol_ptr_vector_update(&FD_ACUM, &FD_PROCESS);
    fd_cleanup();
    fd_processing = false;
    sol_mainloop_impl_unlock();
}

void
sol_mainloop_impl_iter(void)
{
    sol_mainloop_common_timeout_process();
    fd_process();
    signals_process();
    child_watch_process();
    sol_mainloop_common_idler_process();
}

void *
sol_mainloop_impl_fd_add(int fd, unsigned int flags, bool (*cb)(void *data, int fd, unsigned int active_flags), const void *data)
{
    struct sol_fd_posix *handle = malloc(sizeof(struct sol_fd_posix));
    int ret;

    SOL_NULL_CHECK(handle, NULL);

    sol_mainloop_impl_lock();

    handle->fd = fd;
    handle->flags = flags;
    handle->cb = cb;
    handle->data = data;
    handle->remove_me = false;
    handle->invalid = false;

    ret = sol_ptr_vector_append(&fd_vector, handle);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);
    fd_changed = true;

    sol_mainloop_common_main_thread_check_notify();
    sol_mainloop_impl_unlock();

    return handle;

clean:
    sol_mainloop_impl_unlock();
    free(handle);
    return NULL;
}

bool
sol_mainloop_impl_fd_del(void *handle)
{
    struct sol_fd_posix *fd = handle;

    sol_mainloop_impl_lock();

    fd->remove_me = true;
    fd_pending_deletion++;
    fd_changed = true;
    if (!fd_processing)
        fd_cleanup();

    sol_mainloop_impl_unlock();

    return true;
}

void *
sol_mainloop_impl_child_watch_add(uint64_t pid, void (*cb)(void *data, uint64_t pid, int status), const void *data)
{
    struct sol_child_watch_posix *child_watch = malloc(sizeof(*child_watch));
    int ret;

    SOL_NULL_CHECK(child_watch, NULL);

    sol_mainloop_impl_lock();

    child_watch->pid = pid;
    child_watch->cb = cb;
    child_watch->data = data;
    child_watch->remove_me = false;

    ret = sol_ptr_vector_append(&child_watch_vector, child_watch);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);

    sol_mainloop_common_main_thread_check_notify();
    sol_mainloop_impl_unlock();

    return child_watch;

clean:
    sol_mainloop_impl_unlock();
    free(child_watch);
    return NULL;
}

bool
sol_mainloop_impl_child_watch_del(void *handle)
{
    struct sol_child_watch_posix *child_watch = handle;

    sol_mainloop_impl_lock();

    child_watch->remove_me = true;
    child_watch_pending_deletion++;
    if (!child_watch_processing)
        child_watch_cleanup();

    sol_mainloop_impl_unlock();

    return true;
}
