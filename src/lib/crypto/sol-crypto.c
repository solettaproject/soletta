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

#include "sol-crypto.h"

int sol_crypto_init(void);
void sol_crypto_shutdown(void);

#ifndef CRYPTO_MESSAGE_DIGEST
int
sol_message_digest_init(void)
{
    return 0;
}

void
sol_message_digest_shutdown(void)
{
}
#endif

int
sol_crypto_init(void)
{
    return sol_message_digest_init();
}

void
sol_crypto_shutdown(void)
{
    sol_message_digest_shutdown();
}
