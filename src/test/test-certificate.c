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

#include <stdio.h>

#include "sol-certificate.h"
#include "sol-mainloop.h"
#include "sol-util.h"

#include "test.h"

const char dummy_cert[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDXTCCAkWgAwIBAgIJAPVrKaY8Ra57MA0GCSqGSIb3DQEBCwUAMEUxCzAJBgNV\n"
    "BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
    "aWRnaXRzIFB0eSBMdGQwHhcNMTUxMTA0MTkyODMzWhcNMTUxMjA0MTkyODMzWjBF\n"
    "MQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50\n"
    "ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIB\n"
    "CgKCAQEAuBuzH33BlrlxxSJ5J8kQ2Nwun/G3ygIVBZj2cKEt2zNg7yHren9x4frO\n"
    "PYPUumy3ipR3lQVKcd76dV71p5CP476X0aQZoF01t96RVYNBYRtoHn32i2dVkM0i\n"
    "EdUqAXlM+1LjdRs85vk4fB1cr9BrY6lsUXFa12cwVLT4edDmgGtsyC3Ho51X6Rtr\n"
    "+JAcQJ3jobIl8bM0gT8vzJKJEDaEWQkYpsOegMqjXqhVvLw4Ee5A4GbEM6nkgEDm\n"
    "8SzZ49raUjYnSF0xp1Cg8S5pBcm+lIhkHNbVHmHPgvmwYfHN59PoGRreZLPtUeD6\n"
    "1p7dkz9N/ovZweKshrUjLsyz1USRhwIDAQABo1AwTjAdBgNVHQ4EFgQUQT5BxbUo\n"
    "fjjVBiw57eJAXx/dQXgwHwYDVR0jBBgwFoAUQT5BxbUofjjVBiw57eJAXx/dQXgw\n"
    "DAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAMlHQjYuRnuxf9YLUTRPW\n"
    "Kq4x9xkliQukfWf9nQ4U9mrNfYoJph6e7ZrCTJ3OZ6OIU9Kr3ygskuPkSzc06aCv\n"
    "3/W7rmJsVjYBghz54RQJNTKWpe3il6MLlqI0rWwzBt+PJYBOfMSzEbGvkxKF7w5a\n"
    "Pd7EPKZvoGfrGZ+Prmeeawm4gHAVnJfIvNw+my8F8Tre+B7HGnCq0H8dsgRxuRIb\n"
    "/yTUmuhW4JCYSV6ZwmUjGVpGDNPsQMy6YTt7DUpUR/l5vSgbVrzUecxT1UPH6D9o\n"
    "AH7+gfGW+ITynDVNSl6RPvtEK5mLmDvfINcjPo85EgWw3DpWUDJlGDWguVkbTYtN\n"
    "2A==\n"
    "-----END CERTIFICATE-----\n";

static void
create_dummy_certificate(void)
{
    FILE *fp = fopen("dummy.pem", "w");
    int r;

    ASSERT(fp != NULL);

    r = fprintf(fp, "%s", dummy_cert);

    ASSERT(r >= 0);

    fclose(fp);
}

DEFINE_TEST(load_certificate);

static void
load_certificate(void)
{
    struct sol_cert *cert;
    const char *filename;

    create_dummy_certificate();

    cert = sol_cert_load_from_file("dummy.pem");
    ASSERT(cert != NULL);

    filename = sol_cert_get_filename(cert);
    ASSERT(filename != NULL);
    ASSERT(streq(filename, "dummy.pem"));

    sol_cert_unref(cert);
    remove("dummy.pem");
}

TEST_MAIN();
