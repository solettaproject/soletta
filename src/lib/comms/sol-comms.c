/*
 * This file is part of the Soletta Project
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

#ifdef HTTP_CLIENT
extern int sol_http_client_init(void);
extern void sol_http_client_shutdown(void);
#endif
extern int sol_network_init(void);
extern void sol_network_shutdown(void);
#ifdef OIC
extern void sol_oic_server_shutdown(void);
#endif

int sol_comms_init(void);
void sol_comms_shutdown(void);

int
sol_comms_init(void)
{
    int ret;

    ret = sol_network_init();
    if (ret != 0)
        return -1;

#ifdef HTTP_CLIENT
    ret = sol_http_client_init();
    if (ret != 0)
        goto http_error;

#endif

    return 0;

#ifdef HTTP_CLIENT
http_error:
    sol_network_shutdown();
#endif
    return -1;
}

void
sol_comms_shutdown(void)
{
#ifdef OIC
    sol_oic_server_shutdown();
#endif
#ifdef HTTP_CLIENT
    sol_http_client_shutdown();
#endif
    sol_network_shutdown();
}
