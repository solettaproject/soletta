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

#include <sys/stat.h>

#include "sol-certificate.h"
#define SOL_LOG_DOMAIN &_sol_certificate_log_domain
#include "sol-log-internal.h"
#include "sol-util.h"
#include "sol-util.h"
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE(_sol_certificate_log_domain, "certificate");

struct sol_cert {
    int refcnt;

    char *filename;
};

static struct sol_ptr_vector storage = SOL_PTR_VECTOR_INIT;

static const char *const search_paths[] = {
    "ssl/certs",
    "ssl/private",
    "pki/tls/certs",
    "pki/tls/private",
    NULL,
};

static char *
find_cert(const char *filename, const char *const paths[])
{
    const char *ssl_cert_dir = getenv("SSL_CERT_DIR");
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    struct stat st;
    int idx;
    int r;

    /* Check absolute path */
    if (stat(filename, &st) == 0)
        return strdup(filename);

    /* Check SSL_CERT_DIR */
    r = sol_buffer_append_printf(&buffer, "%s/%s", ssl_cert_dir, filename);
    SOL_INT_CHECK(r, != 0, NULL);

    if (stat(buffer.data, &st) == 0)
        return sol_buffer_take_data(&buffer);

    sol_buffer_reset(&buffer);

    /* Search known paths */
    for (idx = 0; search_paths[idx] != 0; idx++) {
        r = sol_buffer_append_printf(&buffer, "%s/%s/%s", SYSCONF, search_paths[idx], filename);
        SOL_INT_CHECK(r, != 0, NULL);

        if (stat(buffer.data, &st) == 0)
            return sol_buffer_take_data(&buffer);

        sol_buffer_reset(&buffer);
    }

    sol_buffer_fini(&buffer);

    return NULL;
}

SOL_API struct sol_cert *
sol_cert_load_from_file(const char *filename)
{
    struct sol_cert *cert;
    char *path;
    int idx;
    int r;

    SOL_NULL_CHECK(filename, NULL);

    SOL_PTR_VECTOR_FOREACH_IDX (&storage, cert, idx) {
        if (streq(cert->filename, filename)) {
            cert->refcnt++;
            return cert;
        }
    }

    path = find_cert(filename, search_paths);
    if (path == NULL) {
        SOL_WRN("Certificate not found: %s", filename);
        return NULL;
    }

    cert = calloc(1, sizeof(*cert));
    SOL_NULL_CHECK_GOTO(cert, cert_alloc_error);

    cert->refcnt++;
    cert->filename = path;

    r = sol_ptr_vector_append(&storage, cert);
    SOL_INT_CHECK_GOTO(r, != 0, insert_error);

    return cert;

insert_error:
    free(cert);
cert_alloc_error:
    free(path);
    return NULL;
}

SOL_API void
sol_cert_free(struct sol_cert *cert)
{
    if (cert == NULL)
        return;

    cert->refcnt--;

    if (cert->refcnt > 0)
        return;

    sol_ptr_vector_remove(&storage, cert);

    free(cert->filename);
    free(cert);
}

SOL_API const char *
sol_cert_get_filename(const struct sol_cert *cert)
{
    SOL_NULL_CHECK(cert, NULL);

    return cert->filename;
}
