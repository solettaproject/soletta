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

#include <stdlib.h>

#include <sol-vector.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Power Supply API provides a way to get information about battery,
 * UPS, AC or DC power supply.
 *
 * Some information only is provided by some kinds of power supplies and
 * depends on platform and drivers.
 */

/**
 * @defgroup PowerSupply Power Supply
 *
 * @{
 */

/**
 * Type of power supply.
 * It may be unknown, battery, mains (like ac), and usb variants.
 */
enum sol_power_supply_type {
    SOL_POWER_SUPPLY_TYPE_UNKNOWN,
    SOL_POWER_SUPPLY_TYPE_BATTERY,
    SOL_POWER_SUPPLY_TYPE_UPS,
    SOL_POWER_SUPPLY_TYPE_MAINS,
    SOL_POWER_SUPPLY_TYPE_USB,
    SOL_POWER_SUPPLY_TYPE_USB_DCP,
    SOL_POWER_SUPPLY_TYPE_USB_CDP,
    SOL_POWER_SUPPLY_TYPE_USB_ACA
};

/**
 * Power supply charging status.
 * It doesn't apply to some types of power supplies.
 * Batteries usually provide status.
 */
enum sol_power_supply_status {
    SOL_POWER_SUPPLY_STATUS_UNKNOWN,
    SOL_POWER_SUPPLY_STATUS_CHARGING,
    SOL_POWER_SUPPLY_STATUS_DISCHARGING,
    SOL_POWER_SUPPLY_STATUS_NOT_CHARGING,
    SOL_POWER_SUPPLY_STATUS_FULL
};

/**
 * Power supply capacity level.
 *
 * It represents capacity as provided by sol_power_supply_get_capacity()
 * in well defined ranges.
 *
 * It doesn't apply to some types of power supplies.
 * Batteries usually provide capacity level.
 */
enum sol_power_supply_capacity_level {
    SOL_POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN,
    SOL_POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL,
    SOL_POWER_SUPPLY_CAPACITY_LEVEL_LOW,
    SOL_POWER_SUPPLY_CAPACITY_LEVEL_NORMAL,
    SOL_POWER_SUPPLY_CAPACITY_LEVEL_HIGH,
    SOL_POWER_SUPPLY_CAPACITY_LEVEL_FULL
};

/**
 * Retrieves a list of names of all power supplies found. Types may vary.
 *
 * @param list A vector that will be initialized and filled with all
 * power supply names found. It's required to free names pointed by
 * it and clear the vector after usage.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_list(struct sol_ptr_vector *list);

/**
 * Retrieves a list of names of all power supplies found that match
 * a specified type.
 *
 * @param list A vector that will be initialized and filled with all
 * power supply names found. It's required to free names pointed by
 * it and clear the vector after usage.
 *
 * @param type Type of power supply to be fetched.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_list_by_type(struct sol_ptr_vector *list, enum sol_power_supply_type type);

/**
 * Get type of power supply (battery, USB, ...)
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param type Type of power supply will be set on this pointer on success.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_type(const char *name, enum sol_power_supply_type *type);

/**
 * Check if a given power supply can be found.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param exist On success, if power supply is found,
 * exist will be set to @c True. Otherwise it'll be set to false.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_exist(const char *name, bool *exist);

/**
 * Check if a power supply is online.
 *
 * Usually provided by AC / USB power supplies.
 *
 * For example, if a charger is connected to the wall it would be
 * considered online.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param online On success, if power supply is online this pointer will
 * be set to @c True or to @c False otherwise.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_is_online(const char *name, bool *online);

/**
 * Check if a power supply is present.
 *
 * Usually provided by batteries.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param present On success, if power supply is present this pointer will
 * be set to @c True or to @c False if not attached to the board at the moment.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_is_present(const char *name, bool *present);

/**
 * Get power supply status (charging, discharging, not charging, full).
 *
 * Usually provided by batteries.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param status Status of power supply will be set on this pointer on success.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_status(const char *name, enum sol_power_supply_status *status);

/**
 * Get power supply capacity percentage. Value should vary from 0 to 100 (full).
 *
 * Usually provided by batteries.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param capacity Capacity percentage will be set on this pointer on success.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_capacity(const char *name, int *capacity);

/**
 * Get capacity level, as provided by driver (critical, low, normal,
 * high, full).
 *
 * Usually provided by batteries.
 *
 * If interested on evaluate level using other ranges, or interested on
 * make them consistent for the same percentage between different drivers,
 * sol_power_supply_get_capacity() should be used.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param capacity_level Capacity level of power supply will be set on
 * this pointer on success.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_capacity_level(const char *name, enum sol_power_supply_capacity_level *capacity_level);

/**
 * Get model name of a given power supply.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param model_name Model name of power supply will be set on
 * this pointer on success. It must be freed after usage.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_model_name(const char *name, char **model_name);

/**
 * Get manufacturer of a given power supply.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param manufacturer Manufacturer of power supply will be set on
 * this pointer on success. It must be freed after usage.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_manufacturer(const char *name, char **manufacturer);

/**
 * Get serial number of a given power supply.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param serial_number Serial number of power supply will be set on
 * this pointer on success. It must be freed after usage.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_serial_number(const char *name, char **serial_number);

/**
 * Get current voltage.
 *
 * There are two possible voltage ranges: defined by design and measured by
 * hardware.
 *
 * Minimal and maximal voltages defined by design means expected voltage
 * at normal conditions for supply when it's empty and full. Such
 * values are provided by sol_power_supply_get_voltage_min_design()
 * and sol_power_supply_get_voltage_max_design().
 *
 * If hardware is able to measure and retain voltage information, these
 * thresholds may be provided by sol_power_supply_get_voltage_min()
 * and sol_power_supply_get_voltage_max().
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param voltage Current voltage will be set on this pointer on success.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_voltage(const char *name, int *voltage);

/**
 * Get minimum voltage measured by hardware.
 *
 * @see sol_power_supply_get_voltage() for more details.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param voltage Minimum voltage measured by hardwared will be set on
 * this pointer on success.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_voltage_min(const char *name, int *voltage);

/**
 * Get maximum voltage measured by hardware.
 *
 * @see sol_power_supply_get_voltage() for more details.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param voltage Maximum voltage measured by hardwared will be set on
 * this pointer on success.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_voltage_max(const char *name, int *voltage);

/**
 * Get value of voltage when supply is empty as defined by design.
 *
 * @see sol_power_supply_get_voltage() for more details.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param voltage Minimum voltage as defined by design will be set on
 * this pointer on success.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_voltage_min_design(const char *name, int *voltage);

/**
 * Get value of voltage when supply is full as defined by design.
 *
 * @see sol_power_supply_get_voltage() for more details.
 *
 * @param name Name of power supply. A list of all board supplies names may be
 * fetched with sol_power_supply_get_list().
 *
 * @param voltage Maximum voltage as defined by design will be set on
 * this pointer on success.
 *
 * @return On success, it returns @c 0. On error, a negative value is returned
 * and properties won't be set (pointers won't be valid);
 */
int sol_power_supply_get_voltage_max_design(const char *name, int *voltage);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
