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

#pragma once

#include "sol-buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Soletta's certificate handling.
 */

/**
 * @defgroup Certificate Certificate
 *
 * @{
 */

/**
 * @struct sol_cert
 *
 * @brief Certificate handler
 *
 * This object is the abstraction of certificate.
 */
struct sol_cert;

/**
 * @brief Create a certificate located in @a path.
 *
 * @param path Absolute path to the certificate file.
 *
 * @return sol_cert object on success, NULL otherwise
 */
struct sol_cert *sol_cert_new(const char *path);

/**
 * @brief Load a certificate from a file
 *
 * @param filename Path to the certificate file
 *
 * The path to the certificate file does not need to be absolute. The file
 * can also be in the default system folders ($SYSCONF/ssl/certs,
 * $SYSCONF/ssl/private, $SYSCONF/tls/certs and $SYSCONF/tls/private), or
 * in a directory specified by SSL_CERT_DIR
 *
 * @return sol_cert object on success, NULL otherwise
 */
struct sol_cert *sol_cert_load_from_file(const char *filename);

/**
 * @brief Free the resources a sol_cert object and the object itself
 *
 * @param cert The object to be freed
 */
void sol_cert_unref(struct sol_cert *cert);

/**
 * @brief Get the full path to the certificate in the filesystem
 *
 * @param cert Certificate object.
 *
 * @return sol_cert object on success, NULL otherwise
 */
const char *sol_cert_get_filename(const struct sol_cert *cert);

/**
 * @brief Read data from @a cert.
 *
 * @param cert Certificate object.
 * @param buffer The buffer to write the read data.
 *
 * @return 0 on success or a negative error number on errors.
 */
int sol_cert_read_data(const struct sol_cert *cert, struct sol_buffer *buffer);

/**
 * @brief Write data to @a cert.
 *
 * @param cert Certificate object.
 * @param buffer The buffer to be written to certificate.
 *
 * @return 0 on success or a negative error code on errors.
 */
int sol_cert_write_data(const struct sol_cert *cert, struct sol_buffer *buffer);

/**
 * @brief Get the size in bytes occupied by the certificate.
 *
 * @param cert The certificate object.
 *
 * @return The size in bytes occupied by this certificate or a negative error
 *         code on errors.
 */
ssize_t sol_cert_size(const struct sol_cert *cert);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
