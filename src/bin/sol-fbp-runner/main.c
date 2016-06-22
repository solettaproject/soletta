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

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "soletta.h"
#include "sol-arena.h"
#include "sol-conffile.h"
#include "sol-file-reader.h"
#include "sol-flow-buildopts.h"
#include "sol-json.h"
#include "sol-log.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#ifdef USE_MEMMAP
#include "sol-memmap-storage.h"
#endif

#include "runner.h"

#define MAX_OPTS 64

#if defined(SOL_FLOW_INSPECTOR_ENABLED) && defined(HTTP_SERVER)
#define WEB_INSPECTOR 1
#endif

static struct {
    const char *name;

    const char *memory_map_file;

    const char *options[MAX_OPTS + 1];
    int options_count;
#ifdef WEB_INSPECTOR
    uint16_t web_inspector_port;
#endif
#ifdef SOL_FLOW_INSPECTOR_ENABLED
    bool inspector;
#endif

    bool check_only;
    bool provide_sim_nodes;
    bool execute_type;
    struct sol_ptr_vector fbp_search_paths;
} args;

static struct runner *the_runner;

#ifdef SOL_FLOW_INSPECTOR_ENABLED
/* defined in inspector.c */
extern void inspector_init(void);
#endif

#ifdef WEB_INSPECTOR
#include "web-inspector.h"
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
#ifdef WEB_INSPECTOR
        "    -W[PORT]      Web-based HTTP Inspector using server-sent-events (SSE).\n"
        "                  It will serve a landing page at all interfaces at the given port,\n"
        "                  or use %d as default, with the actual events at '/events'.\n"
        "                  The flow will NOT run until a client connects to '/events' and it\n"
        "                  forcefully quit the flow if the client disconnects.\n"
        "                  A single client is supported at '/events'.\n"
        "                  This option conflicts with -D.\n"
#endif
        "    -I            Define search path for FBP files\n"
        "\n",
#ifdef WEB_INSPECTOR
        program, HTTP_SERVER_PORT);
#else
        program);
#endif
}

static bool
parse_args(int argc, char *argv[])
{
    int opt, err;
    const char known_opts[] = "cho:stI:"
#ifdef SOL_FLOW_INSPECTOR_ENABLED
        "D"
#endif
#ifdef WEB_INSPECTOR
        "W::"
#endif
    ;

    sol_ptr_vector_init(&args.fbp_search_paths);

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
            sol_quit_with_code(EXIT_SUCCESS);
            return false;
        case 'o':
            if (args.options_count == MAX_OPTS) {
                fputs("Error: Too many options.\n", stderr);
                sol_quit_with_code(EXIT_FAILURE);
                return false;
            }
            args.options[args.options_count++] = optarg;
            break;
        case 't':
            args.execute_type = true;
            break;
#ifdef SOL_FLOW_INSPECTOR_ENABLED
        case 'D':
            args.inspector = true;
            break;
#endif
#ifdef WEB_INSPECTOR
        case 'W':
            if (optarg) {
                unsigned long v;
                char *endptr;

                errno = 0;
                v = strtoul(optarg, &endptr, 10);
                if (endptr == optarg || errno != 0) {
                    printf("Invalid -W port value, must be 16-bit unsigned integer in base-10\n");
                    exit(1);
                }
                if (v > UINT16_MAX) {
                    printf("Invalid -W port value, %lu is too big, maximum is %u\n",
                        v, UINT16_MAX);
                    exit(1);
                }
                args.web_inspector_port = v;
            }

            if (!args.web_inspector_port)
                args.web_inspector_port = HTTP_SERVER_PORT;
            break;
#endif
        case 'I':
            err = sol_ptr_vector_append(&args.fbp_search_paths, optarg);
            if (err < 0) {
                fputs("Error: Out of memory\n", stderr);
                sol_quit_with_code(EXIT_FAILURE);
                return false;
            }
            break;
        default:
            sol_quit_with_code(EXIT_FAILURE);
            return false;
        }
    }

    if (optind == argc) {
        sol_quit_with_code(EXIT_FAILURE);
        return false;
    }

#ifdef WEB_INSPECTOR
    if (args.inspector && args.web_inspector_port) {
        fputs("Error: Cannot use both -D and -W options.\n", stderr);
        sol_quit_with_code(EXIT_FAILURE);
        return false;
    }
#endif

    args.name = argv[optind];
    if (args.execute_type) {
        args.provide_sim_nodes = true;
    }

    sol_set_args(argc - optind, &argv[optind]);

    return true;
}

static bool
load_memory_maps(const struct sol_ptr_vector *maps)
{
#ifdef USE_MEMMAP
    struct sol_memmap_map *map;
    int i;

    SOL_PTR_VECTOR_FOREACH_IDX (maps, map, i) {
        if (sol_memmap_add_map(map) < 0)
            return false;
    }

    return true;
#else
    fputs("Warning: Memory map defined on config file, but Soletta was built without support to it\n", stderr);
    return true;
#endif
}

static void
startup(void)
{
    bool finished = true;
    int result = EXIT_FAILURE;
    struct sol_ptr_vector *memory_maps;

    if (!parse_args(sol_argc(), sol_argv()))
        return;

    if (args.execute_type) {
        the_runner = runner_new_from_type(args.name, args.options);
    } else {
        the_runner = runner_new_from_file(args.name, args.options,
            &args.fbp_search_paths);
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
            fputs("Error: Cannot attach simulation nodes\n", stderr);
            goto end;
        }
    }

    if (sol_conffile_resolve_memmap(&memory_maps)) {
        fputs("Error: Couldn't resolve memory mappings on config file\n", stderr);
        goto end;
    }
    if (memory_maps)
        load_memory_maps(memory_maps);

#ifdef SOL_FLOW_INSPECTOR_ENABLED
    if (args.inspector)
        inspector_init();
#endif

#ifdef WEB_INSPECTOR
    if (args.web_inspector_port) {
        if (web_inspector_run(args.web_inspector_port, the_runner) < 0)
            goto end;
    } else
#endif

    if (runner_run(the_runner) < 0) {
        fputs("Error: Failed to run flow\n", stderr);
        goto end;
    }

    finished = false;

end:
    if (finished)
        sol_quit_with_code(result);
}

static void
shutdown(void)
{
#ifdef WEB_INSPECTOR
    if (args.web_inspector_port)
        web_inspector_shutdown();
#endif

    if (the_runner)
        runner_del(the_runner);

    sol_ptr_vector_clear(&args.fbp_search_paths);
}

SOL_MAIN_DEFAULT(startup, shutdown);
