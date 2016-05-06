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

#include <sys/stat.h>

#include "sol-certificate.h"
#define SOL_LOG_DOMAIN &_sol_certificate_log_domain
#include "sol-log-internal.h"
#include "sol-util-internal.h"
#include "sol-util-file.h"
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
    if (stat(filename, &st) == 0 && S_ISREG(st.st_mode) && st.st_mode & S_IRUSR)
        return strdup(filename);

    /* Check SSL_CERT_DIR */
    if (ssl_cert_dir) {
        r = sol_buffer_append_printf(&buffer, "%s/%s", ssl_cert_dir, filename);
        SOL_INT_CHECK(r, != 0, NULL);

        if (stat(buffer.data, &st) == 0 && S_ISREG(st.st_mode) && st.st_mode & S_IRUSR)
            return sol_buffer_steal(&buffer, NULL);

        sol_buffer_reset(&buffer);
    }

    /* Search known paths */
    for (idx = 0; search_paths[idx] != 0; idx++) {
        r = sol_buffer_append_printf(&buffer, "%s/%s/%s", SYSCONF, search_paths[idx], filename);
        SOL_INT_CHECK(r, != 0, NULL);

        if (stat(buffer.data, &st) == 0 && S_ISREG(st.st_mode) && st.st_mode & S_IRUSR)
            return sol_buffer_steal(&buffer, NULL);

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

    path = find_cert(filename, search_paths);
    if (path == NULL) {
        SOL_WRN("Certificate not found: %s", filename);
        return NULL;
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&storage, cert, idx) {
        if (streq(cert->filename, path)) {
            cert->refcnt++;
            free(path);
            return cert;
        }
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
sol_cert_unref(struct sol_cert *cert)
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

SOL_API int
sol_cert_read_data(const struct sol_cert *cert, struct sol_buffer *buffer)
{
    int r;

    SOL_NULL_CHECK(cert, -EINVAL);
    SOL_NULL_CHECK(buffer, -EINVAL);

    r = sol_util_load_file_buffer(cert->filename, buffer);

    return r < 0 ? r : 0;
}

SOL_API int
sol_cert_write_data(const struct sol_cert *cert, struct sol_buffer *buffer)
{
    ssize_t r;

    SOL_NULL_CHECK(cert, -EINVAL);
    SOL_NULL_CHECK(buffer, -EINVAL);

    r = sol_util_write_file_buffer(cert->filename, buffer);

    return r < 0 ? (int)r : 0;
}

SOL_API struct sol_cert *
sol_cert_new(const char *path)
{
    struct sol_cert *cert;
    int idx;
    int r;

    SOL_NULL_CHECK(path, NULL);

    SOL_PTR_VECTOR_FOREACH_IDX (&storage, cert, idx) {
        if (streq(cert->filename, path)) {
            cert->refcnt++;
            return cert;
        }
    }

    cert = calloc(1, sizeof(*cert));
    SOL_NULL_CHECK(cert, NULL);

    cert->refcnt++;
    cert->filename = strdup(path);

    r = sol_ptr_vector_append(&storage, cert);
    SOL_INT_CHECK_GOTO(r, != 0, insert_error);

    return cert;

insert_error:
    free(cert->filename);
    free(cert);
    return NULL;
}

SOL_API ssize_t
sol_cert_size(const struct sol_cert *cert)
{
    struct stat st;

    SOL_NULL_CHECK(cert, -EINVAL);

    errno = 0;
    if (stat(cert->filename, &st) < 0)
        return -errno;

    if (st.st_size < 0 || (unsigned long long)st.st_size > SIZE_MAX)
        return -EOVERFLOW;

    return (ssize_t)st.st_size;

}
