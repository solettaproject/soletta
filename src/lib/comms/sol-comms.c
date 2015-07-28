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

#ifdef HTTP_CLIENT
extern int sol_http_client_init(void);
extern void sol_http_client_shutdown(void);
#endif
#ifdef NETWORK
extern int sol_network_init(void);
extern void sol_network_shutdown(void);
#endif

int sol_comms_init(void);
void sol_comms_shutdown(void);

int
sol_comms_init(void)
{
    int r;

#ifdef NETWORK
    r = sol_network_init();
    if (r < 0)
        goto network_error;
#endif

#ifdef HTTP_CLIENT
    r = sol_http_client_init();
    if (r < 0)
        goto http_error;
#endif

    return 0;

#ifdef HTTP_CLIENT
http_error:
#endif
#ifdef NETWORK
    sol_network_shutdown();
network_error:
#endif
    return -1;
}

void
sol_comms_shutdown(void)
{
#ifdef HTTP_CLIENT
    sol_http_client_shutdown();
#endif
#ifdef NETWORK
    sol_network_shutdown();
#endif
}
