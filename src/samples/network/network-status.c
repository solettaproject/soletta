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

#include "soletta.h"
#include "sol-network.h"
#include "sol-vector.h"

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
        uint16_t i;

        SOL_BUFFER_DECLARE_STATIC(addr_str, SOL_NETWORK_INET_ADDR_STR_LEN);

        printf("\tUP ");
        SOL_VECTOR_FOREACH_IDX (&link->addrs, addr, i) {
            addr_str.used = 0;
            sol_network_link_addr_to_str(addr, &addr_str);
            printf("%.*s ", SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr_str)));
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
    int c, r, opt_idx,  argc = sol_argc();
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

    r = sol_network_subscribe_events(_on_network_event, NULL);
    SOL_INT_CHECK_GOTO(r, < 0, err_init);

    return;

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
}

SOL_MAIN_DEFAULT(startup_network, shutdown_network);
