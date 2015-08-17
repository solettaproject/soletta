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
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "sol-flow-buildopts.h"
#include "sol-log.h"
#include "sol-mainloop.h"

#include "runner.h"

#define MAX_OPTS 64

static struct {
    const char *name;

    const char *options[MAX_OPTS + 1];
    int options_count;

    bool check_only;
    bool provide_sim_nodes;
    bool execute_type;
} args;

static struct runner *the_runner;

#ifdef SOL_FLOW_INSPECTOR_ENABLED
/* defined in inspector.c */
extern void inspector_init(void);
#endif

static void
usage(const char *program)
{
    fprintf(stderr,
        "usage: %s [options] input_file [-- flow_arg1 flow_arg2 ...]\n"
        "\n"
        "Executes the flow described in input_file.\n\n"
        "Options:\n"
        "    -c            Check syntax only. The program will exit as soon as the flow\n"
        "                  is built and the syntax is verified.\n"
        "    -s            Provide simulation nodes for flows with exported ports.\n"
        "    -t            Instead of reading a file, execute a node type with the name\n"
        "                  passed as first argument. Implies -s.\n"
        "    -o name=value Provide option when creating the root node, can have multiple.\n"
#ifdef SOL_FLOW_INSPECTOR_ENABLED
        "    -D            Debug the flow by printing connections and packets to stdout.\n"
#endif
        "\n",
        program);
}

static bool
parse_args(int argc, char *argv[])
{
    int opt;
    const char known_opts[] = "cho:st"
#ifdef SOL_FLOW_INSPECTOR_ENABLED
        "D"
#endif
    ;

    while ((opt = getopt(argc, argv, known_opts)) != -1) {
        switch (opt) {
        case 'c':
            args.check_only = true;
            break;
        case 's':
            args.provide_sim_nodes = true;
            break;
        case 'h':
            usage(argv[0]);
            exit(EXIT_SUCCESS);
            break;
        case 'o':
            if (args.options_count == MAX_OPTS) {
                printf("Too many options.\n");
                exit(EXIT_FAILURE);
            }
            args.options[args.options_count++] = optarg;
            break;
        case 't':
            args.execute_type = true;
            break;
#ifdef SOL_FLOW_INSPECTOR_ENABLED
        case 'D':
            inspector_init();
            break;
#endif
        default:
            return false;
        }
    }

    if (optind == argc)
        return false;

    args.name = argv[optind];
    if (args.execute_type) {
        args.provide_sim_nodes = true;
    }

    sol_args_set(argc - optind, &argv[optind]);

    return true;
}

static bool
startup(void *data)
{
    bool finished = true;
    int result = EXIT_FAILURE;

    if (args.execute_type) {
        the_runner = runner_new_from_type(args.name, args.options);
    } else {
        the_runner = runner_new_from_file(args.name, args.options);
    }

    if (!the_runner)
        goto end;

    if (args.check_only) {
        printf("'%s' - Syntax OK\n", args.name);
        result = EXIT_SUCCESS;
        goto end;
    }

    if (args.provide_sim_nodes) {
        int err;
        err = runner_attach_simulation(the_runner);
        if (err < 0) {
            fprintf(stderr, "Cannot attach simulation nodes\n");
            goto end;
        }
    }

    if (runner_run(the_runner) < 0) {
        fprintf(stderr, "Failed to run\n");
        goto end;
    }

    finished = false;

end:
    if (finished)
        sol_quit_with_code(result);

    return false;
}

static void
shutdown(void)
{
    if (the_runner)
        runner_del(the_runner);
}

int
main(int argc, char *argv[])
{
    int r;

    if (!parse_args(argc, argv)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (sol_init() < 0) {
        fprintf(stderr, "Cannot initialize soletta.\n");
        return EXIT_FAILURE;
    }

    sol_idle_add(startup, NULL);

    r = sol_run();

    shutdown();
    sol_shutdown();

    return r;
}
