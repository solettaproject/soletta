
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

#pragma once

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
 * @}
 */

#ifdef __cplusplus
}
#endif
