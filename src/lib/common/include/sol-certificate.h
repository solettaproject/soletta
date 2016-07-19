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

#pragma once

#include <sol-common-buildopts.h>
#include <sol-types.h>
#include <sol-str-slice.h>

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
 * @typedef sol_cert
 *
 * @brief Certificate handler
 *
 * This object is the abstraction of certificate.
 */
struct sol_cert;
typedef struct sol_cert sol_cert;

/**
 * @brief Load a certificate from an id
 *
 * @param id The id of the certificate to be loaded.
 *
 * In systems where file system is supported, the @a id is the name of the
 * certificate file. The @a id can be a relative or an absolute path to the
 * certificat file. If relative, function will look for it at user home config
 * directory (${HOME}/.config/{$APPNAME}/certs/), in default system
 * directories ($SYSCONF/ssl/certs, $SYSCONF/ssl/private, $SYSCONF/tls/certs
 * and $SYSCONF/tls/private), and in a directory specified by SSL_CERT_DIR
 *
 * @return sol_cert object on success, NULL otherwise
 */
struct sol_cert *sol_cert_load_from_id(const char *id);

/**
 * @brief Increments the reference counter of the given sol_cert.
 *
 * @param cert The certificate to increase the references
 *
 * @return Pointer to the referenced certificate.
 */
struct sol_cert *sol_cert_ref(struct sol_cert *cert);

/**
 * @brief Free the resources a sol_cert object and the object itself
 *
 * @param cert The object to be freed
 */
void sol_cert_unref(struct sol_cert *cert);

#ifdef SOL_FEATURE_FILESYSTEM

/**
 * @brief Get the full path to the certificate in the filesystem
 *
 * @param cert Certificate object.
 *
 * @return sol_cert object on success, NULL otherwise
 */
const char *sol_cert_get_file_name(const struct sol_cert *cert);

#endif /*SOL_FEATURE_FILESYSTEM*/

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
 * @return The number of written characters, if an error is encountered a
 * negative value with the error code.
 */
ssize_t sol_cert_write_contents(const char *file_name, struct sol_str_slice contents);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
