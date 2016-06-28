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

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include "sol-file-reader.h"
#include "sol-log.h"
#include "sol-power-supply.h"
#include "sol-str-table.h"
#include "sol-util-file.h"
#include "sol-util-internal.h"
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
    if (len < 0 || (size_t)len >= size)
        return -EINVAL;

    return 0;
}

static int
_get_string_prop(const char *name, enum sol_power_supply_prop prop,
    struct sol_buffer *buf)
{
    char file_path[PATH_MAX];
    int r;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(buf, -EINVAL);

    r = _get_file_path(file_path, sizeof(file_path), name, prop);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_load_file_buffer(file_path, buf);
    SOL_INT_CHECK(r, < 0, r);

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
    if (r < 0)
        return r;

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
    if (r < 0)
        return r;

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
    if (r < 0)
        return r;

    *value = sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(str_value),
        default_value);

    return 0;
}

static enum sol_util_iterate_dir_reason
_list_supplies_cb(void *data, const char *dir_path, struct dirent *ent)
{
    struct list_data *ldata = data;
    enum sol_power_supply_type type;
    char *name;
    int r;

    if (ldata->check_type) {
        r = sol_power_supply_get_type(ent->d_name, &type);
        SOL_INT_CHECK(r, < 0, SOL_UTIL_ITERATE_DIR_CONTINUE);

        if (type != ldata->type)
            return SOL_UTIL_ITERATE_DIR_CONTINUE;
    }

    name = strdup(ent->d_name);
    SOL_NULL_CHECK(name, -ENOMEM);

    r = sol_ptr_vector_append(ldata->list, name);
    if (r < 0) {
        SOL_WRN("Failed to append supply to list %s", name);
        free(name);
        return r;
    }

    return SOL_UTIL_ITERATE_DIR_CONTINUE;
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
sol_power_supply_free_list(struct sol_ptr_vector *list)
{
    char *name;
    uint16_t i;

    SOL_NULL_CHECK(list, -EINVAL);

    SOL_PTR_VECTOR_FOREACH_IDX (list, name, i) {
        free(name);
    }
    sol_ptr_vector_clear(list);

    return 0;
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
sol_power_supply_exists(const char *name, bool *exist)
{
    char file_path[PATH_MAX];
    struct stat st;
    int len;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(exist, -EINVAL);

    len = snprintf(file_path, sizeof(file_path), SYSFS_POWER_SUPPLY "/%s/",
        name);
    if (len < 0 || (size_t)len >= sizeof(file_path)) {
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
    int r;

    r = _get_int_prop(name, SOL_POWER_SUPPLY_PROP_CAPACITY, capacity);
    if (r < 0)
        return r;

    if (*capacity < 0 || *capacity > 100)
        SOL_WRN("Capacity out of expected range %d", *capacity);

    return 0;
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
sol_power_supply_get_model_name(const char *name,
    struct sol_buffer *model_name_buf)
{
    return _get_string_prop(name, SOL_POWER_SUPPLY_PROP_MODEL_NAME,
        model_name_buf);
}

SOL_API int
sol_power_supply_get_manufacturer(const char *name,
    struct sol_buffer *manufacturer_buf)
{
    return _get_string_prop(name,
        SOL_POWER_SUPPLY_PROP_MANUFACTURER, manufacturer_buf);
}

SOL_API int
sol_power_supply_get_serial_number(const char *name,
    struct sol_buffer *serial_number_buf)
{
    return _get_string_prop(name,
        SOL_POWER_SUPPLY_PROP_SERIAL_NUMBER, serial_number_buf);
}

SOL_API int
sol_power_supply_get_voltage(const char *name, int *voltage)
{
    return _get_int_prop(name, SOL_POWER_SUPPLY_PROP_VOLTAGE_NOW, voltage);
}

SOL_API int
sol_power_supply_get_min_voltage(const char *name, int *voltage)
{
    return _get_int_prop(name, SOL_POWER_SUPPLY_PROP_VOLTAGE_MIN, voltage);
}

SOL_API int
sol_power_supply_get_max_voltage(const char *name, int *voltage)
{
    return _get_int_prop(name, SOL_POWER_SUPPLY_PROP_VOLTAGE_MAX, voltage);
}

SOL_API int
sol_power_supply_get_min_voltage_design(const char *name, int *voltage)
{
    return _get_int_prop(name,
        SOL_POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN, voltage);
}

SOL_API int
sol_power_supply_get_max_voltage_design(const char *name, int *voltage)
{
    return _get_int_prop(name,
        SOL_POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN, voltage);
}
