/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-coap-transport.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "coap-transport");

#ifndef SOL_NO_API_VERSION
#define SOL_COAP_TRANSPORT_API_CHECK(transport, expected, ...) \
    do { \
        if (((const struct sol_coap_transport *)transport)->api_version != (expected)) { \
            SOL_WRN("Invalid " # transport " %p API version(%hu), " \
                "expected " # expected "(%hu)", \
                (transport), \
                ((const struct sol_coap_transport *)transport)->api_version, \
                (expected)); \
            return __VA_ARGS__; \
        } \
    } while (0)
#else
#define SOL_COAP_TRANSPORT_API_CHECK(transport, expected, ...)
#endif

SOL_API int
sol_coap_transport_sendmsg(struct sol_coap_transport *transport, const void *buf, size_t len,
    const struct sol_network_link_addr *addr)
{
    SOL_NULL_CHECK(transport, -EINVAL);
    SOL_COAP_TRANSPORT_API_CHECK(transport, SOL_COAP_TRANSPORT_API_VERSION, -EINVAL);
    SOL_NULL_CHECK(transport->sendmsg, -EINVAL);

    return transport->sendmsg(transport, buf, len, addr);
}

SOL_API int
sol_coap_transport_recvmsg(struct sol_coap_transport *transport, void *buf, size_t len,
    struct sol_network_link_addr *addr)
{
    SOL_NULL_CHECK(transport, -EINVAL);
    SOL_COAP_TRANSPORT_API_CHECK(transport, SOL_COAP_TRANSPORT_API_VERSION, -EINVAL);
    SOL_NULL_CHECK(transport->recvmsg, -EINVAL);

    return transport->recvmsg(transport, buf, len, addr);
}

SOL_API int
sol_coap_transport_set_on_write(struct sol_coap_transport *transport,
    bool (*on_can_write)(void *data, struct sol_coap_transport *transport),
    const void *user_data)
{
    SOL_NULL_CHECK(transport, -EINVAL);
    SOL_COAP_TRANSPORT_API_CHECK(transport, SOL_COAP_TRANSPORT_API_VERSION, -EINVAL);
    SOL_NULL_CHECK(transport->set_on_write, -EINVAL);

    return transport->set_on_write(transport, on_can_write, user_data);
}

SOL_API int
sol_coap_transport_set_on_read(struct sol_coap_transport *transport,
    bool (*on_can_read)(void *data, struct sol_coap_transport *transport),
    const void *user_data)
{
    SOL_NULL_CHECK(transport, -EINVAL);
    SOL_COAP_TRANSPORT_API_CHECK(transport, SOL_COAP_TRANSPORT_API_VERSION, -EINVAL);
    SOL_NULL_CHECK(transport->set_on_read, -EINVAL);

    return transport->set_on_read(transport, on_can_read, user_data);
}
