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

#include <sol-network-util.h>
#include <sys/socket.h>

enum sol_network_family
sol_network_af_to_sol(int af)
{
    switch (af) {
    case AF_INET:
        return SOL_NETWORK_FAMILY_INET;
    case AF_INET6:
        return SOL_NETWORK_FAMILY_INET6;
    default:
        return SOL_NETWORK_FAMILY_UNSPEC;
    }
}

int
sol_network_sol_to_af(enum sol_network_family snf)
{
    switch (snf) {
    case SOL_NETWORK_FAMILY_INET:
        return AF_INET;
    case SOL_NETWORK_FAMILY_INET6:
        return AF_INET6;
    default:
        return AF_UNSPEC;
    }
}
