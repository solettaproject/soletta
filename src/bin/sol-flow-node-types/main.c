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
#include <stdio.h>
#include <errno.h>
#include <dlfcn.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-mainloop.h"
#include "sol-log-internal.h"
#include "sol-flow.h"
#include "sol-flow-internal.h"
#include "sol-util.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-node-types");

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
struct ctx {
    FILE *fp;
    bool is_first;
};

static void
json_print_str(FILE *fp, const char *str)
{
    const char *itr;

    if (!str) {
        fputs("null", fp);
        return;
    }

    fputc('"', fp);
    for (itr = str; *itr != '\0'; itr++) {
        char tmp[2] = { '\\', 0 };
        switch (*itr) {
        case '"':
        case '\\':
            tmp[1] = *itr;
            break;
        case '\b':
            tmp[1] = 'b';
            break;
        case '\f':
            tmp[1] = 'f';
            break;
        case '\n':
            tmp[1] = 'n';
            break;
        case '\r':
            tmp[1] = 'r';
            break;
        case '\t':
            tmp[1] = 't';
            break;
        }
        ;

        if (tmp[1]) {
            if (itr > str)
                fwrite(str, itr - str, 1, fp);
            fwrite(tmp, 2, 1, fp);
            str = itr + 1;
        }
    }

    if (itr > str)
        fwrite(str, itr - str, 1, fp);

    fputc('"', fp);
}

static void
json_print_str_key_value(FILE *fp, const char *key, const char *value)
{
    SOL_NULL_CHECK(key);
    json_print_str(fp, key);
    fputs(": ", fp);
    json_print_str(fp, value);
}

static void
list_ports(FILE *fp, const struct sol_flow_port_description *const *ports)
{
    const struct sol_flow_port_description *const *itr;

    for (itr = ports; *itr != NULL; itr++) {
        const struct sol_flow_port_description *port = *itr;
        bool first = true;

        if (itr > ports)
            fputs(", ", fp);
        fputs("{\n", fp);

#define STR(x) #x
#define OUT(x)                                          \
    if (first) {                                    \
        fputs("  ", fp);                            \
        first = false;                              \
    } \
    else fputs(", ", fp);                         \
    json_print_str_key_value(fp, STR(x), port->x);  \
    fputc('\n', fp);

        OUT(name);
        OUT(description);
        OUT(data_type);
#undef OUT
#undef STR

        fprintf(fp,
            ", \"required\": %s\n"
            "}\n",
            port->required ? "true" : "false"
            );
    }
}

static void
list_members(FILE *fp, const struct sol_flow_node_options_member_description *const members)
{
    const struct sol_flow_node_options_member_description *itr;

    for (itr = members; itr->name != NULL; itr++) {
        const struct sol_flow_node_options_member_description *member = itr;
        bool first = true;

        if (itr > members)
            fputs(", ", fp);
        fputs("{\n", fp);

#define STR(x) #x
#define OUT(x)                                                  \
    if (first) {                                    \
        fputs("  ", fp);                            \
        first = false;                              \
    } \
    else fputs(", ", fp);                         \
    json_print_str_key_value(fp, STR(x), member->x);        \
    fputc('\n', fp);

        OUT(name);
        OUT(description);
        OUT(data_type);
#undef OUT
#undef STR

        if (member->data_type) {
            if (streq(member->data_type, "boolean")) {
                fprintf(fp, ", \"default\": %s\n",
                    member->defvalue.b ? "true" : "false");
            } else if (streq(member->data_type, "int")) {
                fprintf(fp, ", \"default\": "
                    "{ \"val\": %d, \"min\": %d, "
                    "\"max\": %d, \"step\": %d }\n",
                    member->defvalue.i.val,
                    member->defvalue.i.min,
                    member->defvalue.i.max,
                    member->defvalue.i.step);
            } else if (streq(member->data_type, "float")) {
                fprintf(fp, ", \"default\": "
                    "{ \"val\": %f, \"min\": %f, "
                    "\"max\": %f, \"step\": %f }\n",
                    member->defvalue.f.val,
                    member->defvalue.f.min,
                    member->defvalue.f.max,
                    member->defvalue.f.step);
            } else if (streq(member->data_type, "string")) {
                fputs(", ", fp);
                json_print_str_key_value(fp, "default", member->defvalue.s);
                fputc('\n', fp);
            }
        }

        fprintf(fp,
            ", \"required\": %s\n"
            "}\n",
            member->required ? "true" : "false");
    }
}

static void
print_options(FILE *fp, const struct sol_flow_node_options_description *options)
{
    fprintf(fp,
        "  \"version\": %hu\n"
        ", \"required\": %s\n"
        ", \"members\": [\n",
        options->sub_api,
        options->required ? "true" : "false");
    if (options->members)
        list_members(fp, options->members);
    fputs("]\n", fp);
}

static bool
cb_list(void *data, const struct sol_flow_node_type *type)
{
    struct ctx *ctx = data;
    const struct sol_flow_node_type_description *desc = type->description;
    bool first = true;

    SOL_FLOW_NODE_TYPE_API_CHECK(type, SOL_FLOW_NODE_TYPE_API_VERSION, false);
    SOL_FLOW_NODE_TYPE_DESCRIPTION_API_CHECK(desc, SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION, false);

    /* skip internal nodes */
    if (streq(desc->category, "internal"))
        return true;

    if (ctx->is_first)
        ctx->is_first = false;
    else
        fputs(", ", ctx->fp);

    fputs("{\n", ctx->fp);

#define STR(x) #x
#define OUT(x)                                          \
    if (first) first = false;                           \
    else fputs(", ", ctx->fp);                          \
    json_print_str_key_value(ctx->fp, STR(x), desc->x); \
    fputc('\n', ctx->fp);

    OUT(name);
    OUT(category);
    OUT(symbol);
    OUT(options_symbol);
    OUT(description);
    OUT(author);
    OUT(url);
    OUT(license);
    OUT(version);
#undef OUT
#undef STR

    if (desc->ports_in) {
        fputs(", \"in_ports\": [\n", ctx->fp);
        list_ports(ctx->fp, desc->ports_in);
        fputs("]\n", ctx->fp);
    }

    if (desc->ports_out) {
        fputs(", \"out_ports\": [\n", ctx->fp);
        list_ports(ctx->fp, desc->ports_out);
        fputs("]\n", ctx->fp);
    }

    if (desc->options) {
        fputs(", \"options\": {\n", ctx->fp);
        print_options(ctx->fp, desc->options);
        fputs("}\n", ctx->fp);
    }

    fputs("}\n", ctx->fp);

    return true;
}
#endif

static void
help(const char *progname)
{
    printf(
        "Usage:\n"
        "    %s [-h|--help] [--no-builtins] [--name=mod1] module1.so [--name=mod2] module2.so\n"
        "\n"
        "Options:\n"
        "    --no-builtins    if used no builtins are output, otherwise they are\n"
        "                     included in the output.\n"
        "\n"
        "    --name=STRING    if provided will be used as name in the result for the\n"
        "                     following file, otherwise the file name is used.\n"
        "\n"
        "     -h, --help      show this help.\n",
        progname);
}

int
main(int argc, char *argv[])
{
#ifndef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    SOL_WRN("does not work if compiled with --disable-flow-node-type-description");
    return EXIT_FAILURE;
#else
    int result = EXIT_FAILURE;
    int i;
    bool builtins = true;
    unsigned int count = 0;
    const char *name = NULL;
    struct ctx ctx = {
        .fp = stdout,
        .is_first = true
    };

    for (i = 1; i < argc; i++) {
        if (streq(argv[i], "--help") || streq(argv[i], "-h")) {
            help(argv[0]);
            return EXIT_SUCCESS;
        } else if (streq(argv[i], "--no-builtins"))
            builtins = false;
    }

    if (sol_init() < 0)
        goto end;

    sol_log_domain_init_level(&_log_domain);

    fputs("{\n", stdout);

    if (builtins) {
        fputs("\"builtin\": [\n", stdout);
        sol_flow_foreach_builtin_node_type(cb_list, &ctx);
        fputs("]\n", stdout);
        count++;
    }

    for (i = 1; i < argc; i++) {
        void *mod;
        void (*foreach)(bool (*)(void *, const struct sol_flow_node_type *), const void *);

        if (streqn(argv[i], "--", 2)) {
            if (streqn(argv[i], "--name=", sizeof("--name=") - 1))
                name = argv[i] + sizeof("--name=") - 1;
            continue;
        }

        mod = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL);
        if (!mod) {
            SOL_WRN("could not dlopen(\"%s\"): %s",
                argv[i], dlerror());
            continue;
        }

        foreach = dlsym(mod, "sol_flow_foreach_module_node_type");
        if (!foreach) {
            SOL_WRN("module \"%s\" does not provide sol_flow_foreach_module_node_type(): %s",
                argv[i], dlerror());
            goto mod_close;
        }

        if (count > 0)
            fputs(", ", stdout);
        count++;

        if (!name)
            name = argv[i];

        json_print_str(stdout, name);
        fputs(": [\n", stdout);
        ctx.is_first = true;
        foreach(cb_list, &ctx);
        fputs("]\n", stdout);
        name = NULL;

mod_close:
        dlclose(mod);
    }

    fputs("}\n", stdout);
    result = EXIT_SUCCESS;

end:
    sol_shutdown();

    return result;
#endif
}
