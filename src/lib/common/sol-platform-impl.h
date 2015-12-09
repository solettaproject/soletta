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

#define SOL_LOG_DOMAIN &_sol_platform_log_domain
#include "sol-log-internal.h"
#include "sol-platform.h"
#include "sol-vector.h"
#include "sol-str-slice.h"

extern struct sol_log_domain _sol_platform_log_domain;

int sol_platform_impl_init(void);
void sol_platform_impl_shutdown(void);

int sol_platform_impl_get_state(void);

int sol_platform_impl_add_service_monitor(const char *service) SOL_ATTR_NONNULL(1);
int sol_platform_impl_del_service_monitor(const char *service) SOL_ATTR_NONNULL(1);

int sol_platform_impl_start_service(const char *service) SOL_ATTR_NONNULL(1);
int sol_platform_impl_stop_service(const char *service) SOL_ATTR_NONNULL(1);
int sol_platform_impl_restart_service(const char *service) SOL_ATTR_NONNULL(1);

int sol_platform_impl_set_target(const char *target) SOL_ATTR_NONNULL(1);

int sol_platform_impl_get_machine_id(char id[SOL_STATIC_ARRAY_SIZE(33)]);
int sol_platform_impl_get_serial_number(char **number);
int sol_platform_impl_get_os_version(char **version);

/* callbacks into generic platform abstraction */
void sol_platform_inform_state_monitors(enum sol_platform_state state);
void sol_platform_inform_service_monitors(const char *service,
    enum sol_platform_service_state state);

int sol_platform_impl_get_mount_points(struct sol_ptr_vector *vector);
int sol_platform_impl_umount(const char *mpoint, void (*cb)(void *data, const char *mpoint, int error), const void *data);

int sol_platform_impl_set_hostname(const char *name);
const char *sol_platform_impl_get_hostname(void);

void sol_platform_inform_hostname_monitors(void);

int sol_platform_unregister_hostname_monitor(void);
int sol_platform_register_hostname_monitor(void);

void sol_platform_inform_system_clock_changed(void);

int sol_platform_impl_set_system_clock(int64_t timestamp);
int64_t sol_platform_impl_get_system_clock(void);

int sol_platform_unregister_system_clock_monitor(void);
int sol_platform_register_system_clock_monitor(void);

void sol_platform_inform_timezone_changed(void);

int sol_platform_impl_set_timezone(const char *timezone);
const char *sol_platform_impl_get_timezone(void);
int sol_platform_register_timezone_monitor(void);
int sol_platform_unregister_timezone_monitor(void);

int sol_platform_impl_set_locale(char **locales);
const char *sol_platform_impl_get_locale(enum sol_platform_locale_category type);

void sol_platform_inform_locale_changed(void);
int sol_platform_register_locale_monitor(void);
int sol_platform_unregister_locale_monitor(void);
int sol_platform_impl_apply_locale(enum sol_platform_locale_category type, const char *locale);
int sol_platform_impl_load_locales(char **locale_cache);

int sol_platform_locale_to_c_category(enum sol_platform_locale_category type);
const char *sol_platform_locale_to_c_str_category(enum sol_platform_locale_category type);
