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

#include <sol-types.h>

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
 * @brief Load a certificate from a file name.
 *
 * @param file_name Name of the certificate to be loaded.
 *
 * The @a file_name can be the full path to the certificate file when file system
 * is supported. If not absolute the file will be looked at the default system
 * folders ($SYSCONF/ssl/certs, $SYSCONF/ssl/private, $SYSCONF/tls/certs and
 * $SYSCONF/tls/private), or in a directory specified by SSL_CERT_DIR.
 *
 * @return sol_cert object on success, NULL otherwise
 */
struct sol_cert *sol_cert_load_from_file(const char *file_name);

/**
 * @brief Free the resources a sol_cert object and the object itself
 *
 * @param cert The object to be freed
 */
void sol_cert_unref(struct sol_cert *cert);

#ifdef FEATURE_FILESYSTEM

/**
 * @brief Get the full path to the certificate in the filesystem
 *
 * @param cert Certificate object.
 *
 * @return sol_cert object on success, NULL otherwise
 */
const char *sol_cert_get_file_name(const struct sol_cert *cert);

#endif /*FEATURE_FILESYSTEM*/

/**
 * @brief Get the certificate contents
 *
 * @param cert Certificate object.
 *
 * @return the contents of the certificate @a cert on success, NULL otherwise
 */
struct sol_blob *sol_cert_get_contents(const struct sol_cert *cert);

/**
 * @brief Write @a contents to @a cert.
 *
 * Certificates are always saved in user context.
 *
 * @param file_name The name of the certificate file. The certificate file name
 *        must be relative. File name with full path is not supported.
 * @param contents A blob containing the contents to be written to the
 *        certificate.
 *
 * @return 0 on success or a negative error number on errors.
 */
int sol_cert_write_contents(const char *file_name, const struct sol_blob *contents);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
