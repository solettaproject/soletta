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

/**
 * @file
 * @brief Network link status
 *
 * Network sample that monitors specified links. It checks if the link is
 * up/down and its addresses.
 * To see the usage help, -h or --help.
 */

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "sol-mainloop.h"
#include "sol-network.h"
#include "sol-vector.h"
#include "sol-log.h"

static regex_t regex;

static bool
_compile_regex(const char *text)
{
    char error_message[PATH_MAX];
    int status = regcomp(&regex, text, REG_EXTENDED | REG_NEWLINE);

    if (!status)
        return true;

    regerror(status, &regex, error_message, sizeof(error_message));
    fprintf(stderr, "[ERROR] Regex error compiling '%s': %s\n", text, error_message);

    return false;
}

static bool
_match_link(const struct sol_network_link *link)
{
    char *name = sol_network_link_get_name(link);
    const char *p = name;
    regmatch_t m;

    if (!name)
        return false;
    if (!regexec(&regex, p, 1, &m, 0)) {
        free(name);
        return true;
    }
    free(name);

    return false;
}

static void
_on_network_event(void *data, const struct sol_network_link *link, enum sol_network_event event)
{
    char *name;

    if (!_match_link(link))
        return;

    name = sol_network_link_get_name(link);
    if (!name) {
        fprintf(stderr, "[ERROR] Could not get the link's name\n");
        return;
    }

    switch (event) {
    case SOL_NETWORK_LINK_CHANGED:
        printf("Link %s was changed\n", name);
        break;
    case SOL_NETWORK_LINK_ADDED:
        printf("Link %s was added\n", name);
        break;
    case SOL_NETWORK_LINK_REMOVED:
        printf("Link %s was removed\n", name);
        free(name);
        return;
        break;
    default:
        break;
    }

    if (link->flags & SOL_NETWORK_LINK_UP) {
        struct sol_network_link_addr *addr;
        char addr_str[SOL_INET_ADDR_STRLEN];
        uint16_t i;

        printf("\tUP ");
        SOL_VECTOR_FOREACH_IDX (&link->addrs, addr, i) {
            sol_network_addr_to_str(addr, addr_str, sizeof(addr_str));
            printf("%s ", addr_str);
        }
        printf("\n");
    } else {
        printf("\tDOWN\n");
    }

    free(name);
}

static void
startup_network(void)
{
    char **argv = sol_argv();
    int c, opt_idx,  argc = sol_argc();
    char *regexp = NULL;
    static const struct option opts[] = {
        { "interface", required_argument, NULL, 'i' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(argc, argv, "i:h", opts, &opt_idx)) != -1) {
        switch (c) {
        case 'i':
            regexp = optarg;
            break;
        case 'h':
        default:
            fprintf(stderr,
                "Usage:\n\t%s [-i <interface to monitor>]\n"
                "\tIf any interface is given all of them will be monitored\n", argv[0]);
            sol_quit_with_code(EXIT_SUCCESS);
            return;
        }
    }

    regexp = (regexp) ? : (char *)".*";
    if (!_compile_regex(regexp))
        goto err;

    if (sol_network_init() != 0) {
        fprintf(stderr, "[ERROR] Could not initialize the network\n");
        goto err_init;
    }

    if (!sol_network_subscribe_events(_on_network_event, NULL))
        goto err_net;

    return;

err_net:
    sol_network_shutdown();
err_init:
    regfree(&regex);
err:
    sol_quit_with_code(EXIT_FAILURE);
}


static void
shutdown_network(void)
{
    regfree(&regex);
    sol_network_unsubscribe_events(_on_network_event, NULL);
    sol_network_shutdown();
}

SOL_MAIN_DEFAULT(startup_network, shutdown_network);
