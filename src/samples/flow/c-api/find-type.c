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

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <dlfcn.h>

#include "sol-flow.h"
#include "sol-flow-resolver.h"
#include "sol-util-file.h"
#include "soletta.h"

/**
 * @file find-type.c
 *
 * Example how to find a node type given its name, then print its
 * description such as ports and options.
 */

static void
show_help(void)
{
    char **argv = sol_argv();
    const char *progname = argv[0];

    printf(
        "Usage:\n"
        "\t%s type-name1 [type-name2...]\n"
        "\t\twill find type based on its name (ie: 'wallclock/minute').\n"
        "\t\tor will try to query names in configuration files (ie: sol-flow*.json).\n"
        "\n"
        "\t%s --of-module=modname\n"
        "\t%s -m=modname\n"
        "\t\tlist all node types names built inside dynamic module modname (ie: 'iio')\n"
        "\n"
        "\t%s --builtins\n"
        "\t%s -b\n"
        "\t\tlist all node types names built inside libsoletta.\n"
        "\n",
        progname, progname, progname, progname, progname);
}

/*
 * Types known at compile time may differ from those available at
 * runtime via shared libraries or dynamically loadable
 * modules. To guard against those issues, Soletta keeps a member
 * field 'api_version' at the start of the structure. This can be
 * compiled out for performance purposes, or when dynamic
 * libraries and modules are disabled.
 *
 * if it is enabled, then check before accessing members.
 */
#ifndef SOL_NO_API_VERSION
#define CHECK_TYPE_API_VERSION(type, ...) \
    do { \
        if (type->api_version != SOL_FLOW_NODE_TYPE_API_VERSION) { \
            fprintf(stderr, "ERROR: type=%p has api_version=%" PRIu16 \
                " while %" PRIu16 " was expected.\n", \
                type, type->api_version, SOL_FLOW_NODE_TYPE_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0)
#else
#define CHECK_TYPE_API_VERSION(type, ...) do { } while (0)
#endif


/* like sol_flow_node_type, the sol_flow_node_type_description is also
 * versioned and must be checked.
 */
#ifndef SOL_NO_API_VERSION
#define CHECK_TYPE_DESC_API_VERSION(type, ...) \
    do { \
        if (!type->description) { \
            fprintf(stderr, "ERROR: type=%p has no description.\n", type); \
            return __VA_ARGS__; \
        } \
        if (type->description->api_version != SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION) { \
            fprintf(stderr, "ERROR: type=%p description->api_version=%" PRIu16 \
                " while %" PRIu16 " was expected.\n", \
                type, type->description->api_version, SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0)
#else
#define CHECK_TYPE_DESC_API_VERSION(type, ...) do { } while (0)
#endif

static bool
cb_print_type_name(void *data, const struct sol_flow_node_type *type)
{
    const char *prefix = data;

    /* these checks are needed since we're calling dlopen() ourselves
     * to manually load the flow modules in show_module_types() and
     * this function is shared for show_builtins() and that one.
     */
    CHECK_TYPE_API_VERSION(type, true);
    CHECK_TYPE_DESC_API_VERSION(type, true);

    printf("%s: %s\n", prefix, type->description->name);
    return true;
}

/*
 * sol_flow_foreach_builtin_node_type() will call you back will all
 * built-in types, that is, the types that were compiled inside
 * libsoletta.so. Other modules may still be available, see
 * show_module_types().
 */
static void
show_builtins(void)
{
    sol_flow_foreach_builtin_node_type(cb_print_type_name, "builtin");
}

/* Dynamically loadable modules can provide extra set of node types by
 * providing a public symbol sol_flow_foreach_module_node_type() with
 * the same signature as used by sol_flow_foreach_builtin_node_type().
 *
 * These modules are usually installed at
 * ${PREFIX}/lib/soletta/modules/flow/${modname}.so, but we also
 * support users specifying a path here.
 */
static void
show_module_types(const char *modname)
{
    void (*foreach)(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data);
    char install_rootdir[PATH_MAX], path[PATH_MAX];
    void *h;
    int r;

    if (!modname || modname[0] == '\0')
        return;

    if (modname[0] == '.' || modname[0] == '/') {
        size_t len = strlen(modname);
        if (len >= sizeof(path)) {
            fprintf(stderr, "ERROR: path is too long: %s\n", modname);
            return;
        }
        memcpy(path, modname, len + 1);
        goto load;
    }

    /*
     * Soletta is relocatable and provides sol_util_get_rootdir() that
     * tries to find the installation path from current binary or
     * libsoletta.so
     */
    r = sol_util_get_rootdir(install_rootdir, sizeof(install_rootdir));
    if (r < 0 || r >= (int)sizeof(install_rootdir)) {
        fputs("ERROR: could not get libsoletta installation dir.\n", stderr);
        return;
    }

    /* these usually come from build system if building from inside soletta,
     * otherwise check pkg-config --variable=modulesdir soletta
     */
#ifndef MODULESDIR
#define MODULESDIR "/usr/lib/soletta/modules"
#endif
#ifndef FLOWMODULESDIR
#define FLOWMODULESDIR MODULESDIR "/flow"
#endif

    r = snprintf(path, sizeof(path), "%s" FLOWMODULESDIR "/%s.so",
        install_rootdir, modname);
    if (r < 0 || r >= (int)sizeof(install_rootdir)) {
        fprintf(stderr, "ERROR: path is too long %s" FLOWMODULESDIR "/%s.so\n",
            install_rootdir, modname);
        return;
    }

load:
    h = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "ERROR: could not load %s: %s\n",
            path, dlerror());
        return;
    }

    foreach = dlsym(h, "sol_flow_foreach_module_node_type");
    if (!foreach) {
        fprintf(stderr, "ERROR: could not find symbol "
            "sol_flow_foreach_module_node_type() inside %s: %s\n",
            path, dlerror());
        goto end;
    }

    foreach(cb_print_type_name, modname);

end:
    dlclose(h);
}

static void
print_type(const struct sol_flow_node_type *type)
{
    const struct sol_flow_node_type_description *tdesc;

    CHECK_TYPE_API_VERSION(type);
    CHECK_TYPE_DESC_API_VERSION(type);

    tdesc = type->description;

    printf(
        "type {\n"
        "\tname: %s\n"
        "\tcetegory: %s\n"
        "\tdescription: %s\n"
        "\tauthor: %s\n"
        "\turl: %s\n"
        "\tlicense: %s\n"
        "\tversion: %s\n",
        tdesc->name,
        tdesc->category,
        tdesc->description,
        tdesc->author,
        tdesc->url,
        tdesc->license,
        tdesc->version);

    if (tdesc->options) {
        const struct sol_flow_node_options_description *odesc = tdesc->options;
        const struct sol_flow_node_options_member_description *omemb;

        printf(
            "\toptions%s: [\n",
            odesc->required ? " [required]" : "");

        for (omemb = odesc->members; omemb->name != NULL; omemb++) {
            printf("\t\t%s: %s%s # %s\n",
                omemb->name, omemb->data_type,
                omemb->required ? " [required]" : "",
                omemb->description);
        }

        puts("\t]");
    }

    if (!tdesc->ports_in)
        puts("\tports_in: none");
    else {
        const struct sol_flow_port_description *const *pdesc = tdesc->ports_in;

        puts("\tports_in: [");
        for (; *pdesc != NULL; pdesc++) {
            const struct sol_flow_port_description *d = *pdesc;

            printf("\t\t%s", d->name);

            if (d->array_size)
                printf("[%" PRIu16 "]", d->array_size);

            printf(": %s%s # %s\n", d->data_type,
                d->required ? " [required]" : "", d->description);
        }
        puts("\t]");
    }

    if (!tdesc->ports_out)
        puts("\tports_out: none");
    else {
        const struct sol_flow_port_description *const *pdesc = tdesc->ports_out;

        puts("\tports_out: [");
        for (; *pdesc != NULL; pdesc++) {
            const struct sol_flow_port_description *d = *pdesc;

            printf("\t\t%s", d->name);

            if (d->array_size)
                printf("[%" PRIu16 "]", d->array_size);

            printf(": %s%s # %s\n", d->data_type,
                d->required ? " [required]" : "", d->description);
        }
        puts("\t]");
    }

    puts("}\n");
}

static void
show_resolved_type(const char *name)
{
    const struct sol_flow_node_type *type;
    const struct sol_flow_node_type_description *tdesc;
    struct sol_flow_node_named_options resolved_opts = {};
    int err;

    err = sol_flow_resolve(sol_flow_get_builtins_resolver(), name, &type, &resolved_opts);
    if (err < 0) {
        err = sol_flow_resolve(NULL, name,
            &type, &resolved_opts);
        if (err < 0) {
            fprintf(stderr, "ERROR: Couldn't resolve type '%s'\n", name);
            return;
        }
    }

    tdesc = type->description;

    printf("resolved '%s' as type=%p '%s'", name, type, tdesc->name);

    /*
     * A configuration file may define a new name for a type (alias)
     * and set of default options to use, returned as 'resolved_opts'
     * variable that is a 'named options' vector.
     *
     * For example,
     * src/samples/flow/grove-kit/sol-flow-intel-edison-rev-c.json
     * defines 'Relay' as 'gpio/writer' with options active_low=false
     * and pin=7.
     *
     * These 'named options' can be converted in the actual options to
     * be passed to sol_flow_node_new() by means of
     * sol_flow_node_options_new().
     *
     * Print them to be informative.
     */
    if (resolved_opts.count) {
        const struct sol_flow_node_named_options_member *itr, *itr_end;

        itr = resolved_opts.members;
        itr_end = itr + resolved_opts.count;

        fputs(" options={", stdout);
        for (; itr < itr_end; itr++) {
            printf("%s=", itr->name);
            switch (itr->type) {
            case SOL_FLOW_NODE_OPTIONS_MEMBER_BOOL:
                printf("%s", itr->boolean ? "true" : "false");
                break;
            case SOL_FLOW_NODE_OPTIONS_MEMBER_BYTE:
                printf("%#" PRIx8, itr->byte);
                break;
            case SOL_FLOW_NODE_OPTIONS_MEMBER_DIRECTION_VECTOR:
                printf("x:%g|y:%g|z:%g|min:%g|max:%g",
                    itr->direction_vector.x,
                    itr->direction_vector.y,
                    itr->direction_vector.z,
                    itr->direction_vector.min,
                    itr->direction_vector.max);
                break;
            case SOL_FLOW_NODE_OPTIONS_MEMBER_DRANGE_SPEC:
                printf("min:%g|max:%g|step:%g",
                    itr->drange_spec.min,
                    itr->drange_spec.max,
                    itr->drange_spec.step);
                break;
            case SOL_FLOW_NODE_OPTIONS_MEMBER_FLOAT:
                printf("%g", itr->f);
                break;
            case SOL_FLOW_NODE_OPTIONS_MEMBER_INT:
                printf("%" PRId32, itr->i);
                break;
            case SOL_FLOW_NODE_OPTIONS_MEMBER_IRANGE_SPEC:
                printf("min:%" PRId32 "|max:%" PRId32 "|step:%" PRId32,
                    itr->irange_spec.min,
                    itr->irange_spec.max,
                    itr->irange_spec.step);
                break;
            case SOL_FLOW_NODE_OPTIONS_MEMBER_RGB:
                printf(
                    "red:%" PRId32 "|green:%" PRId32 "|blue:%" PRId32 "|"
                    "red_max:%" PRId32 "|green_max:%" PRId32 "|blue_max:%" PRId32,
                    itr->rgb.red,
                    itr->rgb.green,
                    itr->rgb.blue,
                    itr->rgb.red_max,
                    itr->rgb.green_max,
                    itr->rgb.blue_max);
                break;
            case SOL_FLOW_NODE_OPTIONS_MEMBER_STRING:
                printf("\"%s\"", itr->string ? itr->string : "");
                break;
            default:
                fprintf(stderr, "\nERROR: doesn't suppport printing named option of type '%d'\n", itr->type);
            }
            if (itr + 1 < itr_end)
                fputs(", ", stdout);
        }
        putchar('}');

        sol_flow_node_named_options_fini(&resolved_opts);
    }

    putchar('\n');

    print_type(type);
}

static void
startup(void)
{
    char **argv = sol_argv();
    int c, opt_idx, argc = sol_argc();
    static const struct option opts[] = {
        { "builtins", no_argument, NULL, 'b' },
        { "of-module", required_argument, NULL, 'm' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(argc, argv, "brm:h", opts, &opt_idx)) != -1) {
        switch (c) {
        case 'b':
            show_builtins();
            break;
        case 'm':
            show_module_types(optarg);
            break;
        case 'h':
            show_help();
            sol_quit();
            return;
        default:
            show_help();
            sol_quit_with_code(EXIT_FAILURE);
            return;
        }
    }

    for (c = optind; c < argc; c++)
        show_resolved_type(argv[c]);

    sol_quit(); /* no need to wait for mainloop */
}

static void
shutdown(void)
{
}

SOL_MAIN_DEFAULT(startup, shutdown);
