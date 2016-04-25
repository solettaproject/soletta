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
 * @brief Get the certificate contents
 *
 * @param cert Certificate object.
 *
 * @return the contents of the certificate @a cert on success, NULL otherwise
 */
struct sol_blob *sol_cert_get_contents(const struct sol_cert *cert);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
