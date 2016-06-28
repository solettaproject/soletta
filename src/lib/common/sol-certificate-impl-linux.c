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

#include <sys/stat.h>
#include <unistd.h>

#include "sol-certificate.h"
#define SOL_LOG_DOMAIN &_sol_certificate_log_domain
#include "sol-log-internal.h"
#include "sol-util-file.h"
#include "sol-file-reader.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE(_sol_certificate_log_domain, "certificate");

struct sol_cert {
    char *filename;
    uint16_t refcnt;
    const char *basename;
};

static struct sol_ptr_vector storage = SOL_PTR_VECTOR_INIT;

static const char *const search_paths[] = {
    "ssl/certs",
    "ssl/private",
    "pki/tls/certs",
    "pki/tls/private",
    NULL,
};

static int
get_home_config_dir(struct sol_buffer *buffer)
{
    int r;

    r = sol_util_get_user_config_dir(buffer);
    SOL_INT_CHECK(r, < 0, r);

    return sol_buffer_append_printf(buffer, "/certs");
}

static inline bool
is_cert(const char *file)
{
    return access(file, R_OK) == 0;
}

static char *
find_cert(const char *filename, const char *const paths[])
{
    const char *ssl_cert_dir = getenv("SSL_CERT_DIR");
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    int idx;
    int r;

    /* Check absolute path */
    if (is_cert(filename))
        return strdup(filename);

    /* Check SSL_CERT_DIR */
    if (ssl_cert_dir) {
        r = sol_buffer_append_printf(&buffer, "%s/%s", ssl_cert_dir, filename);
        SOL_INT_CHECK(r, != 0, NULL);

        if (is_cert(buffer.data))
            return sol_buffer_steal(&buffer, NULL);

        sol_buffer_reset(&buffer);
    }

    /* Search cert in HOME config dir */
    r = get_home_config_dir(&buffer);
    SOL_INT_CHECK(r, != 0, NULL);
    r = sol_buffer_append_printf(&buffer, "/%s", filename);
    SOL_INT_CHECK(r, != 0, NULL);
    if (is_cert(buffer.data))
        return sol_buffer_steal(&buffer, NULL);

    /* Search known paths */
    for (idx = 0; search_paths[idx] != 0; idx++) {
        r = sol_buffer_append_printf(&buffer, "%s/%s/%s", SYSCONF, search_paths[idx], filename);
        SOL_INT_CHECK(r, != 0, NULL);

        if (is_cert(buffer.data))
            return sol_buffer_steal(&buffer, NULL);

        sol_buffer_reset(&buffer);
    }

    sol_buffer_fini(&buffer);

    return NULL;
}

SOL_API struct sol_cert *
sol_cert_load_from_id(const char *id)
{
    struct sol_cert *cert;
    char *path;
    int idx;
    int r;

    SOL_NULL_CHECK(id, NULL);

    if (id[0] == '/') {
        SOL_PTR_VECTOR_FOREACH_IDX (&storage, cert, idx) {
            if (streq(cert->filename, id)) {
                cert->refcnt++;
                return cert;
            }
        }
    } else {
        SOL_PTR_VECTOR_FOREACH_IDX (&storage, cert, idx) {
            if (streq(cert->basename, id)) {
                cert->refcnt++;
                return cert;
            }
        }
    }

    path = find_cert(id, search_paths);
    if (path == NULL) {
        SOL_WRN("Certificate not found: %s", id);
        return NULL;
    }

    cert = calloc(1, sizeof(*cert));
    SOL_NULL_CHECK_GOTO(cert, cert_alloc_error);

    cert->refcnt++;
    cert->filename = path;
    cert->basename = sol_util_file_get_basename(sol_str_slice_from_str(path)).data;

    r = sol_ptr_vector_append(&storage, cert);
    SOL_INT_CHECK_GOTO(r, != 0, insert_error);

    return cert;

insert_error:
    free(cert);
cert_alloc_error:
    free(path);
    return NULL;
}

SOL_API struct sol_cert *
sol_cert_ref(struct sol_cert *cert)
{
    SOL_NULL_CHECK(cert, NULL);

    errno = ENOMEM;
    SOL_INT_CHECK(cert->refcnt, == UINT16_MAX, NULL);
    errno = 0;

    cert->refcnt++;
    return cert;
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
sol_cert_get_file_name(const struct sol_cert *cert)
{
    SOL_NULL_CHECK(cert, NULL);

    return cert->basename;
}

SOL_API struct sol_blob *
sol_cert_get_contents(const struct sol_cert *cert)
{
    struct sol_file_reader *fr;

    SOL_NULL_CHECK(cert, NULL);

    fr = sol_file_reader_open(cert->filename);
    SOL_NULL_CHECK(fr, NULL);

    return sol_file_reader_to_blob(fr);
}

SOL_API ssize_t
sol_cert_write_contents(const char *file_name, struct sol_str_slice contents)
{
    SOL_BUFFER_DECLARE_STATIC(path, PATH_MAX);
    int r;

    SOL_NULL_CHECK(file_name, -EINVAL);

    if (*file_name == '\0') {
        SOL_WRN("File name shouldn't be empty");
        return -EINVAL;
    }

    r = get_home_config_dir(&path);
    SOL_INT_CHECK(r, != 0, r);

    r = sol_util_create_recursive_dirs(sol_buffer_get_slice(&path), S_IRWXU);
    SOL_INT_CHECK(r, != 0, r);

    r = sol_buffer_append_printf(&path, "/%s", file_name);
    SOL_INT_CHECK(r, != 0, r);

    return sol_util_write_file_slice(path.data, contents);
}
