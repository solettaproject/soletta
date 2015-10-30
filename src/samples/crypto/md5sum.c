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

/*
 * This is a regular Soletta application, there are no linux-micro
 * specific bits in it, just a timer, an optional gpio writer and some
 * monitors for platform and service states. The purpose is to show
 * that it can be considered a /init (PID1) binary if Soletta is
 * compiled with linux-micro platform and if it runs as PID1, then
 * /proc, /sys, /dev are all mounted as well as other bits of the
 * system are configured.
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>

#include "sol-buffer.h"
#include "sol-file-reader.h"
#include "sol-str-table.h"
#include "sol-mainloop.h"
#include "sol-message-digest.h"
#include "sol-util.h"

enum log_level {
    LOG_LEVEL_DEFAULT = 0,
    LOG_LEVEL_QUIET,
    LOG_LEVEL_STATUS
};

struct entry {
    char *filename;
    struct sol_message_digest *md;
    struct sol_buffer digest;
};

static struct sol_ptr_vector entries = SOL_PTR_VECTOR_INIT;
static uint32_t pending;

static const char algorithm[] = "md5";
static bool checking = false;
static bool checking_strict = false;
static bool checking_warn = false;
static enum log_level log_level = LOG_LEVEL_DEFAULT;

static const char stdin_filename[] = "-";

static void
store_digest(void *data, struct sol_message_digest *handle, struct sol_blob *digest)
{
    struct entry *entry = data;
    int r = sol_buffer_append_as_base16(&entry->digest,
        sol_str_slice_from_blob(digest), false);

    if (r < 0)
        fputs("ERROR: could not store digest as hexadecimal\n", stderr);

    pending--;
    if (!pending)
        sol_quit();
}

static struct entry *
entry_new(const char *filename)
{
    struct entry *entry;
    struct sol_message_digest_config cfg = {
        SOL_SET_API_VERSION(.api_version = SOL_MESSAGE_DIGEST_CONFIG_API_VERSION, )
        .algorithm = algorithm,
        .on_digest_ready = store_digest,
    };

    entry = calloc(1, sizeof(struct entry));
    if (!entry)
        return NULL;

    cfg.data = entry;
    entry->filename = strdup(filename);
    entry->md = sol_message_digest_new(&cfg);
    if (!entry->md) {
        fprintf(stderr,
            "ERROR: could not create message digest for algorithm %s\n",
            algorithm);
        free(entry);
        return NULL;
    }

    sol_buffer_init(&entry->digest);
    return entry;
}

static void
entry_del(struct entry *entry)
{
    sol_message_digest_del(entry->md);
    sol_buffer_fini(&entry->digest);
    free(entry->filename);
    free(entry);
}

static struct sol_fd *stdin_watch;

static int
check_stdin(void)
{
    printf("TODO check stdin\n");
    return 0;
}

static bool
on_stdin_hash(void *data, int fd, unsigned int flags)
{
    struct entry *entry = data;
    struct sol_blob *b = NULL;
    bool is_last = false;
    int err;

    if (flags & SOL_FD_FLAGS_IN) {
        char *mem = malloc(PATH_MAX);
        ssize_t r;
        if (!mem) {
            fputs("ERROR: cannot allocate memory to read stdin.\n", stderr);
            return true;
        }
        b = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, mem, PATH_MAX);
        if (!b) {
            fputs("ERROR: cannot allocate blob for stdin.\n", stderr);
            free(mem);
            return true;
        }
        while ((r = read(fd, mem, PATH_MAX)) < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "ERROR: cannot read from stdin: %s\n",
                sol_util_strerrora(errno));
            r = 0;
            break;
        }
        b->size = r;
        is_last = !r;

        err = sol_message_digest_feed(entry->md, b, is_last);
        sol_blob_unref(b);
        if (err < 0) {
            fprintf(stderr, "ERROR: cannot feed message digest: %s\n",
                sol_util_strerrora(-err));
            sol_quit_with_code(EXIT_FAILURE);
        }
    }

    if (flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL) && !is_last) {
        b = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, NULL, 0);
        if (!b) {
            fputs("ERROR: cannot allocate blob for stdin.\n", stderr);
            return true;
        }
        is_last = true;
        err = sol_message_digest_feed(entry->md, b, is_last);
        sol_blob_unref(b);
        if (err < 0) {
            fprintf(stderr, "ERROR: cannot feed message digest: %s\n",
                sol_util_strerrora(-err));
            sol_quit_with_code(EXIT_FAILURE);
        }
    }

    if (is_last) {
        stdin_watch = NULL;
        return false;
    }

    return true;
}

static int
hash_stdin(void)
{
    struct entry *entry;
    int r;

    if (stdin_watch)
        return 0;

    entry = entry_new(stdin_filename);
    if (!entry)
        return -ENOMEM;

    r = sol_util_fd_set_flag(STDIN_FILENO, O_NONBLOCK);
    if (r < 0)
        fputs("WARNING: cannot set stdin to non-blocking.\n", stderr);

    stdin_watch = sol_fd_add(STDIN_FILENO,
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_ERR,
        on_stdin_hash, entry);
    if (!stdin_watch) {
        entry_del(entry);
        return -ENOMEM;
    }

    r = sol_ptr_vector_append(&entries, entry);
    if (r < 0) {
        sol_fd_del(stdin_watch);
        stdin_watch = NULL;
        entry_del(entry);
        return -ENOMEM;
    }
    pending++;

    return 0;
}

static int
check_file(const char *filename)
{
    printf("TODO check file\n");
    return 0;
}

static int
hash_file(const char *filename)
{
    struct entry *entry;
    struct sol_file_reader *fr;
    struct sol_blob *blob;
    int r;

    fr = sol_file_reader_open(filename);
    if (!fr)
        return -errno;

    blob = sol_file_reader_to_blob(fr);
    if (!blob)
        return -ENOMEM;

    entry = entry_new(filename);
    if (!entry) {
        sol_blob_unref(blob);
        return -ENOMEM;
    }

    r = sol_ptr_vector_append(&entries, entry);
    if (r < 0) {
        sol_blob_unref(blob);
        entry_del(entry);
        return -ENOMEM;
    }
    pending++;

    r = sol_message_digest_feed(entry->md, blob, true);
    sol_blob_unref(blob);
    return r;
}

static int
process_stdin(void)
{
    if (checking)
        return check_stdin();

    return hash_stdin();
}

static int
process_input(const char *filename)
{
    if (streq(filename, stdin_filename))
        return process_stdin();

    if (checking)
        return check_file(filename);

    return hash_file(filename);
}

static void
startup(void)
{
    char **argv = sol_argv();
    int argc = sol_argc();

    while (1) {
        static const struct option opts[] = {
            { "binary", no_argument, NULL, 'b' },
            { "text", no_argument, NULL, 't' },
            { "tag", no_argument, NULL, 'T' },
            { "check", no_argument, NULL, 'c' },
            { "quiet", no_argument, NULL, 'q' },
            { "status", no_argument, NULL, 's' },
            { "strict", no_argument, NULL, 'S' },
            { "warn", no_argument, NULL, 'w' },
            { "version", no_argument, NULL, 'v' },
            { "help", no_argument, NULL, 'h' },
            { 0, 0, 0, 0 }
        };
        int opt_idx = 0, c;

        c = getopt_long(argc, argv, "btcw", opts, &opt_idx);
        if (c == -1)
            break;
        switch (c) {
        case 'b':
            fputs("WARNING: ignored unsupported option -b/--binary.\n", stderr);
            break;
        case 't':
            fputs("WARNING: ignored unsupported option -t/--text.\n", stderr);
            break;
        case 'T':
            fputs("ERROR: unsupported option --tag.\n", stderr);
            sol_quit_with_code(EXIT_FAILURE);
            return;
        case 'c':
            checking = true;
            break;
        case 'q':
            log_level = LOG_LEVEL_QUIET;
            break;
        case 's':
            log_level = LOG_LEVEL_STATUS;
            break;
        case 'S':
            checking_strict = true;
            break;
        case 'w':
            checking_warn = true;
            break;
        case 'h':
            printf(
                "Usage:\n"
                "\t%s [OPTION]... [FILE]...\n"
                "\n"
                "With no FILE, or when FILE is -, read standard input.\n"
                "\n"
                "\t-b, --binary         read in binary mode (ignored).\n"
                "\t-c, --check          read MD5 sums from the FILEs and check them\n"
                "\t    --tag            create a BSD-style checksum (not supported).\n"
                "\t-t, --text           read in text mode (ignored).\n"
                "\n"
                "The following four options are useful only when verifying checksums:\n"
                "\t    --quiet          don't print OK for each successfully verified file\n"
                "\t    --status         don't output anything, status code shows success\n"
                "\t    --strict         exit non-zero for improperly formatted checksum lines\n"
                "\t-w, --warn           warn about improperly formatted checksum lines\n"
                "\t    --version        output version information and exit\n"
                "\t    --help           display this help and exit\n"
                "\n",
                argv[0]);
            sol_quit();
            return;
        case 'v':
            printf("%s soletta %s\n", argv[0], VERSION);
            sol_quit();
            return;

        default:
            sol_quit_with_code(EXIT_FAILURE);
            return;
        }
    }

    if (optind >= argc)
        process_input(stdin_filename);
    else {
        int i;
        for (i = optind; i < argc; i++) {
            process_input(argv[i]);
        }
    }

    if (!pending)
        sol_quit_with_code(EXIT_FAILURE);
}

static void
shutdown(void)
{
    struct entry *entry;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&entries, entry, i) {
        if (!checking) {
            printf("%s  %s\n", (char *)entry->digest.data, entry->filename);
        } else {
            printf("TODO check %s %s\n", (char *)entry->digest.data, entry->filename);
        }

        entry_del(entry);
    }
    sol_ptr_vector_clear(&entries);
}

SOL_MAIN_DEFAULT(startup, shutdown);
