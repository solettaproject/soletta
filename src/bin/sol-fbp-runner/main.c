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

#include "sol-arena.h"
#include "sol-file-reader.h"
#include "sol-flow-buildopts.h"
#include "sol-json.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"

#ifdef USE_MEMMAP
#include "sol-memmap-storage.h"
#endif

#include "runner.h"

#define MAX_OPTS 64

static struct {
    const char *name;

    const char *memory_map_file;

    const char *options[MAX_OPTS + 1];
    int options_count;

    bool check_only;
    bool provide_sim_nodes;
    bool execute_type;
    struct sol_ptr_vector fbp_search_paths;
} args;

static struct runner *the_runner;
static struct sol_arena *str_arena;

#ifdef USE_MEMMAP
static struct sol_ptr_vector memory_maps = SOL_PTR_VECTOR_INIT;
#endif

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
#ifdef USE_MEMMAP
        "    -m            Uses memory map MAP .json file to map 'persistence' fields on\n"
        "                  persistent storage, like NVRAM and EEPROM"
#endif
        "    -s            Provide simulation nodes for flows with exported ports.\n"
        "    -t            Instead of reading a file, execute a node type with the name\n"
        "                  passed as first argument. Implies -s.\n"
        "    -o name=value Provide option when creating the root node, can have multiple.\n"
#ifdef SOL_FLOW_INSPECTOR_ENABLED
        "    -D            Debug the flow by printing connections and packets to stdout.\n"
#endif
        "    -I            Define search path for FBP files\n"
        "\n",
        program);
}

static bool
parse_args(int argc, char *argv[])
{
    int opt, err;
    const char known_opts[] = "cho:stI:"
#ifdef SOL_FLOW_INSPECTOR_ENABLED
        "D"
#endif
#ifdef USE_MEMMAP
        "m:"
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
        case 'I':
            err = sol_ptr_vector_append(&args.fbp_search_paths, optarg);
            if (err < 0) {
                printf("Out of memory\n");
                exit(1);
            }
            break;
#ifdef USE_MEMMAP
        case 'm':
            if (access(optarg, R_OK) == -1) {
                fprintf(stderr, "Can't access memory map file '%s': %s",
                    optarg, sol_util_strerrora(errno));
                return false;
            }
            args.memory_map_file = optarg;
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

#ifdef USE_MEMMAP
static void
clear_memory_maps(void)
{
    const struct sol_str_table_ptr *iter;
    struct sol_memmap_map *map;
    int i;

    SOL_PTR_VECTOR_FOREACH_IDX (&memory_maps, map, i) {
        for (iter = map->entries; iter->key; iter++) {
            free((void *)iter->val);
        }

        free(map);
    }

    sol_ptr_vector_clear(&memory_maps);
}

static bool
load_memory_map_file(const char *json_file)
{
    struct sol_file_reader *fr = NULL;
    struct sol_json_scanner scanner, entries;
    struct sol_str_slice contents;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;
    struct sol_vector entries_vector = SOL_VECTOR_INIT(struct sol_str_table_ptr);
    struct sol_memmap_map *map;
    struct sol_memmap_entry *memmap_entry;
    struct sol_str_table_ptr *ptr_table_entry;
    uint32_t version;
    size_t entries_vector_size;
    void *data;
    int i;
    char *path;

    fr = sol_file_reader_open(json_file);
    if (!fr) {
        SOL_ERR("Couldn't open json file '%s': %s\n", json_file, sol_util_strerrora(errno));
        return false;
    }

    contents = sol_file_reader_get_all(fr);
    sol_json_scanner_init(&scanner, contents.data, contents.len);

    SOL_JSON_SCANNER_ARRAY_LOOP (&scanner, &token, SOL_JSON_TYPE_OBJECT_START, reason) {
        SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&scanner, &token, &key, &value, reason) {
            if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "path")) {
                sol_json_token_remove_quotes(&value);
                path = strndupa(value.start,
                    sol_json_token_get_size(&value));
                if (!path) {
                    SOL_WRN("Couldn't get map path");
                    goto error;
                }
            } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "version")) {
                if (sol_json_token_get_uint32(&value, &version) < 0) {
                    SOL_WRN("Couldn't get memory map version");
                    goto error;
                }
            } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "entries")) {
                sol_json_scanner_init_from_token(&entries, &value);

                SOL_JSON_SCANNER_ARRAY_LOOP (&entries, &token, SOL_JSON_TYPE_OBJECT_START, reason) {
                    uint32_t size = 0, offset = 0, bit_offset = 0, bit_size = 0;
                    char *name = NULL;

                    SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&entries, &token, &key, &value, reason) {
                        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "name")) {
                            sol_json_token_remove_quotes(&value);
                            name = strndupa(value.start,
                                sol_json_token_get_size(&value));
                            if (!name) {
                                SOL_WRN("Couldn't get entry name");
                                goto error;
                            }
                        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "offset")) {
                            if (sol_json_token_get_uint32(&value, &offset) < 0) {
                                SOL_WRN("Couldn't get entry offset");
                                goto error;
                            }
                        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "size")) {
                            if (sol_json_token_get_uint32(&value, &size) < 0) {
                                SOL_WRN("Couldn't get entry size");
                                goto error;
                            }
                        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "bit_offset")) {
                            if (sol_json_token_get_uint32(&value, &bit_offset) < 0) {
                                SOL_WRN("Couldn't get entry size");
                                goto error;
                            }
                            if (bit_offset > 7) {
                                SOL_WRN("Entry bit offset cannot be greater than 7");
                                goto error;
                            }
                        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "bit_size")) {
                            if (sol_json_token_get_uint32(&value, &bit_size) < 0) {
                                SOL_WRN("Couldn't get entry bit size");
                                goto error;
                            }
                        }
                    }

                    if (bit_size > (size * 8)) {
                        SOL_WRN("Invalid bit size for entry. Must not be greater"
                            "than size * 8 [%d]", size * 8);
                        goto error;
                    }
                    if (!size) {
                        SOL_WRN("Invalid size for entry");
                        goto error;
                    }

                    ptr_table_entry = sol_vector_append(&entries_vector);
                    SOL_NULL_CHECK_GOTO(ptr_table_entry, error);

                    memmap_entry = calloc(sizeof(struct sol_memmap_entry), 1);
                    SOL_NULL_CHECK_GOTO(memmap_entry, error);

                    memmap_entry->offset = offset;
                    memmap_entry->size = size;
                    memmap_entry->bit_offset = bit_offset;
                    memmap_entry->bit_size = bit_size;

                    ptr_table_entry->key = sol_arena_strdup(str_arena, name);
                    if (!ptr_table_entry->key) {
                        SOL_WRN("Could not copy entry name");
                        goto error;
                    }
                    ptr_table_entry->len = strlen(name);
                    ptr_table_entry->val = memmap_entry;
                }
            }
        }

        /* Add ptr_table guard element */
        ptr_table_entry = sol_vector_append(&entries_vector);
        SOL_NULL_CHECK_GOTO(ptr_table_entry, error);

        entries_vector_size = sizeof(struct sol_str_table_ptr) * entries_vector.len;
        map = calloc(1, sizeof(struct sol_memmap_map) + entries_vector_size);
        SOL_NULL_CHECK_GOTO(map, error);

        map->version = version;
        map->path = sol_arena_strdup(str_arena, path);
        SOL_NULL_CHECK_GOTO(map->path, error_path);
        data = sol_vector_take_data(&entries_vector);
        memmove(map->entries, data, entries_vector_size);
        free(data);

        if (sol_memmap_add_map(map) < 0)
            goto error_path;
        if (sol_ptr_vector_append(&memory_maps, map) < 0)
            goto error_path;
    }

    sol_file_reader_close(fr);

    return true;

error_path:
    free(map);
error:
    SOL_VECTOR_FOREACH_IDX (&entries_vector, ptr_table_entry, i) {
        free((void *)ptr_table_entry->val);
    }
    sol_vector_clear(&entries_vector);

    sol_file_reader_close(fr);

    return false;
}

#endif

static bool
startup(void *data)
{
    bool finished = true;
    int result = EXIT_FAILURE;

    str_arena = sol_arena_new();
    if (!str_arena) {
        fprintf(stderr, "Cannot create str arena\n");
        goto end;
    }

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
            fprintf(stderr, "Cannot attach simulation nodes\n");
            goto end;
        }
    }

    if (args.memory_map_file) {
#ifdef USE_MEMMAP
        if (!load_memory_map_file(args.memory_map_file)) {
            fprintf(stderr, "Could not load memory map file\n");
            goto end;
        }
#else
        fprintf(stderr, "Memory map file defined, but Soletta has been built without Memory map support.\n");
#endif
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

    if (str_arena)
        sol_arena_del(str_arena);
#ifdef USE_MEMMAP
    clear_memory_maps();
#endif

    sol_ptr_vector_clear(&args.fbp_search_paths);
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
