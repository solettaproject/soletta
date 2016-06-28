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

#include <stdio.h>

#include "sol-certificate.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"

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

    cert = sol_cert_load_from_id("dummy.pem");
    ASSERT(cert != NULL);

    filename = sol_cert_get_file_name(cert);
    ASSERT(filename != NULL);
    ASSERT(streq(filename, "dummy.pem"));

    sol_cert_unref(cert);
    remove("dummy.pem");
}

DEFINE_TEST(read_write_certificate);

static void
read_write_certificate(void)
{
    struct sol_cert *cert;
    struct sol_blob *blob;

    create_dummy_certificate();

    cert = sol_cert_load_from_id("dummy.pem");
    ASSERT(cert != NULL);

    blob = sol_cert_get_contents(cert);
    ASSERT(blob != NULL);
    ASSERT(streq(dummy_cert, blob->mem));

    ASSERT(sol_cert_write_contents("dummy2.pem",
        SOL_STR_SLICE_STR(blob->mem, blob->size)) == (ssize_t)blob->size);
    sol_blob_unref(blob);
    sol_cert_unref(cert);

    cert = sol_cert_load_from_id("dummy2.pem");
    ASSERT(cert != NULL);
    blob = sol_cert_get_contents(cert);
    ASSERT(blob != NULL);
    ASSERT(streq(dummy_cert, blob->mem));

    sol_blob_unref(blob);
    sol_cert_unref(cert);
    remove("dummy.pem");
}

TEST_MAIN();
