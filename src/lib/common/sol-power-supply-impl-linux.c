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

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include "sol-file-reader.h"
#include "sol-log.h"
#include "sol-power-supply.h"
#include "sol-str-table.h"
#include "sol-util.h"
#include "sol-vector.h"

#define SYSFS_POWER_SUPPLY "/sys/class/power_supply"

struct list_data {
    struct sol_ptr_vector *list;
    enum sol_power_supply_type type;
    bool check_type : 1;
};

enum sol_power_supply_prop {
    SOL_POWER_SUPPLY_PROP_CAPACITY,
    SOL_POWER_SUPPLY_PROP_CAPACITY_LEVEL,
    SOL_POWER_SUPPLY_PROP_MANUFACTURER,
    SOL_POWER_SUPPLY_PROP_MODEL_NAME,
    SOL_POWER_SUPPLY_PROP_ONLINE,
    SOL_POWER_SUPPLY_PROP_PRESENT,
    SOL_POWER_SUPPLY_PROP_SERIAL_NUMBER,
    SOL_POWER_SUPPLY_PROP_STATUS,
    SOL_POWER_SUPPLY_PROP_TYPE,
    SOL_POWER_SUPPLY_PROP_VOLTAGE_NOW,
    SOL_POWER_SUPPLY_PROP_VOLTAGE_MIN,
    SOL_POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
    SOL_POWER_SUPPLY_PROP_VOLTAGE_MAX,
    SOL_POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

static const char *prop_files[] = {
    [SOL_POWER_SUPPLY_PROP_CAPACITY] = "capacity",
    [SOL_POWER_SUPPLY_PROP_CAPACITY_LEVEL] = "capacity_level",
    [SOL_POWER_SUPPLY_PROP_MANUFACTURER] = "manufacturer",
    [SOL_POWER_SUPPLY_PROP_MODEL_NAME] = "model_name",
    [SOL_POWER_SUPPLY_PROP_ONLINE] = "online",
    [SOL_POWER_SUPPLY_PROP_PRESENT] = "present",
    [SOL_POWER_SUPPLY_PROP_SERIAL_NUMBER] = "serial_number",
    [SOL_POWER_SUPPLY_PROP_STATUS] = "status",
    [SOL_POWER_SUPPLY_PROP_TYPE] = "type",
    [SOL_POWER_SUPPLY_PROP_VOLTAGE_NOW] = "voltage_now",
    [SOL_POWER_SUPPLY_PROP_VOLTAGE_MIN] = "voltage_min",
    [SOL_POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN] = "voltage_min_design",
    [SOL_POWER_SUPPLY_PROP_VOLTAGE_MAX] = "voltage_max",
    [SOL_POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN] = "voltage_max_design",
};

static int
_get_file_path(char *file_path, size_t size, const char *name,
    enum sol_power_supply_prop prop)
{
    int len;

    len = snprintf(file_path, size, SYSFS_POWER_SUPPLY "/%s/%s", name,
        prop_files[prop]);
    if (len < 0 || len >= size)
        return -EINVAL;

    return 0;
}

static int
_get_string_prop(const char *name, enum sol_power_supply_prop prop,
    char **value)
{
    char file_path[PATH_MAX];
    char *str_value;
    int r;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    r = _get_file_path(file_path, sizeof(file_path), name, prop);
    SOL_INT_CHECK(r, < 0, r);

    errno = 0;
    r = sol_util_read_file(file_path, "%ms", &str_value);
    SOL_INT_CHECK(errno, != 0, -errno);
    SOL_INT_CHECK(r, < 0, r);

    *value = str_value;

    return 0;
}

static int
_get_int_prop(const char *name, enum sol_power_supply_prop prop, int *value)
{
    char file_path[PATH_MAX];
    int int_value;
    int r;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    r = _get_file_path(file_path, sizeof(file_path), name, prop);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_read_file(file_path, "%d", &int_value);
    SOL_INT_CHECK(r, < 0, r);

    *value = int_value;

    return 0;
}

static int
_get_bool_prop(const char *name, enum sol_power_supply_prop prop, bool *value)
{
    char file_path[PATH_MAX];
    char char_value;
    int r;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    r = _get_file_path(file_path, sizeof(file_path), name, prop);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_read_file(file_path, " %c", &char_value);
    SOL_INT_CHECK(r, < 0, r);

    if (char_value == '0')
        *value = false;
    else if (char_value == '1')
        *value = true;
    else {
        SOL_WRN("Unknown boolean state %c for %s", char_value,
            prop_files[prop]);
        return -EINVAL;
    }

    return 0;
}

static int
_get_enum_prop(const char *name, enum sol_power_supply_prop prop,
    unsigned int *value, const struct sol_str_table *table,
    int default_value)
{
    char file_path[PATH_MAX];
    char str_value[16];
    int r;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    r = _get_file_path(file_path, sizeof(file_path), name, prop);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_read_file(file_path, "%15s", str_value);
    SOL_INT_CHECK(r, < 0, r);

    *value = sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(str_value),
        default_value);

    return 0;
}

static bool
_list_supplies_cb(void *data, const char *dir_path, struct dirent *ent)
{
    struct list_data *ldata = data;
    enum sol_power_supply_type type;
    char *name;
    int r;

    if ((!strcmp(ent->d_name, ".")) || (!strcmp(ent->d_name, ".."))) {
        return false;
    }

    if (ldata->check_type) {
        r = sol_power_supply_get_type(ent->d_name, &type);
        SOL_INT_CHECK(r, < 0, false);

        if (type != ldata->type)
            return false;
    }

    name = strdup(ent->d_name);
    SOL_NULL_CHECK(name, false);

    r = sol_ptr_vector_append(ldata->list, name);
    if (r < 0) {
        SOL_WRN("Failed to append supply to list %s", name);
        free(name);
        return false;
    }

    return false;
}

static int
_get_list_by_type(struct sol_ptr_vector *list, bool check_type,
    enum sol_power_supply_type type)
{
    struct list_data ldata;

    SOL_NULL_CHECK(list, -EINVAL);

    sol_ptr_vector_init(list);

    ldata.list = list;
    ldata.type = type;
    ldata.check_type = check_type;

    sol_util_iterate_dir(SYSFS_POWER_SUPPLY, _list_supplies_cb, &ldata);

    return 0;
}

SOL_API int
sol_power_supply_get_list(struct sol_ptr_vector *list)
{
    return _get_list_by_type(list, false, SOL_POWER_SUPPLY_TYPE_UNKNOWN);
}

SOL_API int
sol_power_supply_get_list_by_type(struct sol_ptr_vector *list,
    enum sol_power_supply_type type)
{
    return _get_list_by_type(list, true, type);
}

SOL_API int
sol_power_supply_get_type(const char *name, enum sol_power_supply_type *type)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("Unknown", SOL_POWER_SUPPLY_TYPE_UNKNOWN),
        SOL_STR_TABLE_ITEM("Battery", SOL_POWER_SUPPLY_TYPE_BATTERY),
        SOL_STR_TABLE_ITEM("UPS", SOL_POWER_SUPPLY_TYPE_UPS),
        SOL_STR_TABLE_ITEM("Mains", SOL_POWER_SUPPLY_TYPE_MAINS),
        SOL_STR_TABLE_ITEM("USB", SOL_POWER_SUPPLY_TYPE_USB),
        SOL_STR_TABLE_ITEM("USB_DCP", SOL_POWER_SUPPLY_TYPE_USB_DCP),
        SOL_STR_TABLE_ITEM("USB_CDP", SOL_POWER_SUPPLY_TYPE_USB_CDP),
        SOL_STR_TABLE_ITEM("USB_ACA", SOL_POWER_SUPPLY_TYPE_USB_ACA),
        { }
    };

    return _get_enum_prop(name, SOL_POWER_SUPPLY_PROP_TYPE, type,
        table, SOL_POWER_SUPPLY_TYPE_UNKNOWN);
}

SOL_API int
sol_power_supply_exist(const char *name, bool *exist)
{
    char file_path[PATH_MAX];
    struct stat st;
    int len;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(exist, -EINVAL);

    len = snprintf(file_path, sizeof(file_path), SYSFS_POWER_SUPPLY "/%s/",
        name);
    if (len < 0 || len >= sizeof(file_path)) {
        SOL_WRN("Couldn't test path for %s.", name);
        return -EINVAL;
    }

    *exist = !stat(file_path, &st);

    return 0;
}

SOL_API int
sol_power_supply_is_online(const char *name, bool *online)
{
    return _get_bool_prop(name, SOL_POWER_SUPPLY_PROP_ONLINE, online);
}

SOL_API int
sol_power_supply_is_present(const char *name, bool *present)
{
    return _get_bool_prop(name, SOL_POWER_SUPPLY_PROP_PRESENT, present);
}

SOL_API int
sol_power_supply_get_status(const char *name,
    enum sol_power_supply_status *status)
{
    /* Strings used by sysfs:
     *   drivers/power/power_supply_sysfs.c
     */
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("Unknown", SOL_POWER_SUPPLY_STATUS_UNKNOWN),
        SOL_STR_TABLE_ITEM("Charging", SOL_POWER_SUPPLY_STATUS_CHARGING),
        SOL_STR_TABLE_ITEM("Discharging", SOL_POWER_SUPPLY_STATUS_DISCHARGING),
        SOL_STR_TABLE_ITEM("Not charging",
            SOL_POWER_SUPPLY_STATUS_NOT_CHARGING),
        SOL_STR_TABLE_ITEM("Full", SOL_POWER_SUPPLY_STATUS_FULL),
        { }
    };

    return _get_enum_prop(name, SOL_POWER_SUPPLY_PROP_STATUS,
        status, table, SOL_POWER_SUPPLY_STATUS_UNKNOWN);
}

SOL_API int
sol_power_supply_get_capacity(const char *name, int *capacity)
{
    return _get_int_prop(name, SOL_POWER_SUPPLY_PROP_CAPACITY, capacity);
}

SOL_API int
sol_power_supply_get_capacity_level(const char *name,
    enum sol_power_supply_capacity_level *capacity_level)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("Unknown", SOL_POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN),
        SOL_STR_TABLE_ITEM("Critical",
            SOL_POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL),
        SOL_STR_TABLE_ITEM("Low", SOL_POWER_SUPPLY_CAPACITY_LEVEL_LOW),
        SOL_STR_TABLE_ITEM("Normal", SOL_POWER_SUPPLY_CAPACITY_LEVEL_NORMAL),
        SOL_STR_TABLE_ITEM("High", SOL_POWER_SUPPLY_CAPACITY_LEVEL_HIGH),
        SOL_STR_TABLE_ITEM("Full", SOL_POWER_SUPPLY_CAPACITY_LEVEL_FULL),
        { }
    };

    return _get_enum_prop(name, SOL_POWER_SUPPLY_PROP_CAPACITY_LEVEL,
        capacity_level, table, SOL_POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN);
}

SOL_API int
sol_power_supply_get_model_name(const char *name, char **model_name)
{
    return _get_string_prop(name, SOL_POWER_SUPPLY_PROP_MODEL_NAME, model_name);
}

SOL_API int
sol_power_supply_get_manufacturer(const char *name, char **manufacturer)
{
    return _get_string_prop(name,
        SOL_POWER_SUPPLY_PROP_MANUFACTURER, manufacturer);
}

SOL_API int
sol_power_supply_get_serial_number(const char *name, char **serial_number)
{
    return _get_string_prop(name,
        SOL_POWER_SUPPLY_PROP_SERIAL_NUMBER, serial_number);
}

SOL_API int
sol_power_supply_get_voltage(const char *name, int *voltage)
{
    return _get_int_prop(name, SOL_POWER_SUPPLY_PROP_VOLTAGE_NOW, voltage);
}

SOL_API int
sol_power_supply_get_voltage_min(const char *name, int *voltage)
{
    return _get_int_prop(name, SOL_POWER_SUPPLY_PROP_VOLTAGE_MIN, voltage);
}

SOL_API int
sol_power_supply_get_voltage_max(const char *name, int *voltage)
{
    return _get_int_prop(name, SOL_POWER_SUPPLY_PROP_VOLTAGE_MAX, voltage);
}

SOL_API int
sol_power_supply_get_voltage_min_design(const char *name, int *voltage)
{
    return _get_int_prop(name,
        SOL_POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN, voltage);
}

SOL_API int
sol_power_supply_get_voltage_max_design(const char *name, int *voltage)
{
    return _get_int_prop(name,
        SOL_POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN, voltage);
}
