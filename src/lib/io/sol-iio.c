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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <inttypes.h>
#include <unistd.h>

#include <sol-log.h>
#include <sol-macros.h>
#include <sol-mainloop.h>
#include <sol-str-slice.h>
#include <sol-util-internal.h>
#include <sol-util-file.h>
#include <sol-vector.h>

#include "sol-iio.h"

#ifdef USE_I2C
#include <sol-i2c.h>
#endif

struct reader_cb_data {
    void (*reader_cb)(void *data, struct sol_iio_device *device);
    const void *data;
};

struct sol_iio_device {
    double *mount_matrix;
    char *trigger_name;
    struct sol_vector reader_cb_list;
    struct sol_fd *fd_handler;
    struct sol_buffer buffer;
    size_t buffer_size;
    struct sol_ptr_vector channels;
    int device_id;
    int trigger_id;
    int fd;
    int name_fd;
    bool buffer_enabled : 1;
    bool manual_triggering : 1;
};

struct sol_iio_channel {
    struct sol_iio_device *device;
    double scale;
    int index;
    int offset;

    unsigned int storagebits;
    unsigned int bits;
    unsigned int shift;
    unsigned int offset_in_buffer;
    uint64_t mask;

    bool little_endian : 1;
    bool is_signed : 1;
    bool processed : 1;

    char name[]; /* Must be last. Memory trick in place. */
};

struct resolve_name_path_data {
    const char *name;
    int id;
};

struct resolve_absolute_path_data {
    const char *path;
    int id;
};

struct iio_opened_device {
    struct sol_iio_device *device;
    struct sol_iio_config *config;
    int refcount;
};

static struct sol_vector iio_opened_devices = SOL_VECTOR_INIT(struct iio_opened_device);

#define DEVICE_PATH "/dev/iio:device%d"
#define SYSFS_DEVICES_PATH "/sys/bus/iio/devices"
#define SYSFS_DEVICE_PATH "/sys/bus/iio/devices/%s"

#define CONFIGFS_IIO_HRTIMER_TRIGGERS_PATH "/sys/kernel/config/iio/triggers/hrtimer"
#define CONFIGFS_IIO_HRTIMER_TRIGGER_PATH "/sys/kernel/config/iio/triggers/hrtimer/%s"
#define HRTIMER_TRIGGER_PREFIX "hrtimer:"

#define DEVICE_NAME_PATH SYSFS_DEVICES_PATH "/iio:device%d/name"
#define DEVICE_NAME_PATH_BY_DEVICE_DIR SYSFS_DEVICES_PATH "/%s/name"

#define BUFFER_ENABLE_DEVICE_PATH SYSFS_DEVICES_PATH "/iio:device%d/buffer/enable"
#define BUFFER_LENGHT_DEVICE_PATH SYSFS_DEVICES_PATH "/iio:device%d/buffer/length"
#define CURRENT_TRIGGER_DEVICE_PATH SYSFS_DEVICES_PATH "/iio:device%d/trigger/current_trigger"

#define SYSFS_TRIGGER_NOW_PATH SYSFS_DEVICES_PATH "/%s/trigger_now"
#define SYSFS_TRIGGER_NOW_BY_ID_PATH SYSFS_DEVICES_PATH "/trigger%d/trigger_now"
#define SYSFS_TRIGGER_NAME_PATH SYSFS_DEVICES_PATH "/%s/name"
#define SYSFS_TRIGGER_SYSFS_ADD_TRIGGER SYSFS_DEVICES_PATH "/iio_sysfs_trigger/add_trigger"
#define SYSFS_TRIGGER_SYSFS_PATH SYSFS_DEVICES_PATH "/iio_sysfs_trigger"

#define CHANNEL_ENABLE_PATH SYSFS_DEVICES_PATH "/iio:device%d/%s_en"
#define CHANNEL_RAW_PATH SYSFS_DEVICES_PATH "/iio:device%d/%s_raw"
#define CHANNEL_PROCESSED_PATH SYSFS_DEVICES_PATH "/iio:device%d/%s_input"
#define CHANNEL_OFFSET_PATH SYSFS_DEVICES_PATH "/iio:device%d/%s_offset"
#define CHANNEL_SCALE_PATH SYSFS_DEVICES_PATH "/iio:device%d/%s_scale"

#define CHANNEL_SCAN_ENABLE_PATH SYSFS_DEVICES_PATH "/iio:device%d/scan_elements/%s_en"
#define CHANNEL_SCAN_INDEX_PATH SYSFS_DEVICES_PATH "/iio:device%d/scan_elements/%s_index"
#define CHANNEL_SCAN_TYPE_PATH SYSFS_DEVICES_PATH "/iio:device%d/scan_elements/%s_type"

#define SAMPLING_FREQUENCY_DEVICE_PATH SYSFS_DEVICES_PATH "/iio:device%d/sampling_frequency"
#define CHANNEL_SAMPLING_FREQUENCY_DEVICE_PATH SYSFS_DEVICES_PATH "/iio:device%d/%ssampling_frequency"

#define CHANNEL_OVERSAMPLING_RATIO_DEVICE_PATH SYSFS_DEVICES_PATH "/iio:device%d/%soversampling_ratio"

#define SAMPLING_FREQUENCY_BUFFER_PATH SYSFS_DEVICES_PATH "/iio:device%d/buffer/sampling_frequency"
#define SAMPLING_FREQUENCY_TRIGGER_PATH SYSFS_DEVICES_PATH "/trigger%d/sampling_frequency"

#define SYSFS_MOUNT_MATRIX SYSFS_DEVICES_PATH "/iio:device%d/mount_matrix"
#define SYSFS_OUT_MOUNT_MATRIX SYSFS_DEVICES_PATH "/iio:device%d/out_mount_matrix"
#define SYSFS_IN_MOUNT_MATRIX SYSFS_DEVICES_PATH "/iio:device%d/in_mount_matrix"

#define I2C_DEVICES_PATH "/sys/bus/i2c/devices/%u-%04u/"

#define MOUNT_MATRIX_LEN 9
#define REL_PATH_IDX 2
#define DEV_NUMBER_IDX 3
#define DEV_NAME_IDX 4

SOL_ATTR_PRINTF(3, 4)
static bool
craft_filename_path(char *path, size_t size, const char *base, ...)
{
    va_list args;
    int len;

    va_start(args, base);
    len = vsnprintf(path, size, base, args);
    va_end(args);

    if (len < 0)
        path[0] = '\0';

    SOL_DBG("available=%zu, used=%d, path='%s'", size, len, path);

    return len >= 0 && (size_t)len < size;
}

SOL_ATTR_PRINTF(1, 2)
static bool
check_file_existence(const char *base, ...)
{
    char path[PATH_MAX];
    va_list args;
    int len;
    struct stat st;

    va_start(args, base);
    len = vsnprintf(path, sizeof(path), base, args);
    va_end(args);

    return len >= 0 && (size_t)len < PATH_MAX && !stat(path, &st);
}

static bool
check_trigger_name(const char *trigger_dir, const char *trigger_name)
{
    char *name, path[PATH_MAX];
    bool result;
    int len;

    result = craft_filename_path(path, sizeof(path), SYSFS_TRIGGER_NAME_PATH,
        trigger_dir);
    if (!result) {
        SOL_WRN("Could not open IIO trigger (%s) name on sysfs", trigger_dir);
        return false;
    }

    len = sol_util_read_file(path, "%ms", &name);
    if (len < 0) {
        SOL_WRN("Could not read IIO trigger (%s) name on sysfs", trigger_dir);
        return false;
    }

    result = streq(trigger_name, name);
    free(name);

    return result;
}

static bool
check_manual_triggering(struct sol_iio_device *device)
{
    DIR *dir;
    struct dirent *ent, *res;
    int success;
    long name_max;
    size_t len;
    bool result = false;

    /* The only way I've found to relact trigger name and trigger is
     * by opening all triggers on /sys/bus/iio/devices
     * and checking name by name =/ */

    dir = opendir(SYSFS_DEVICES_PATH);
    if (!dir) {
        SOL_WRN("No IIO devices available");
        return false;
    }

    /* See readdir_r(3) */
    name_max = pathconf(SYSFS_DEVICES_PATH, _PC_NAME_MAX);
    if (name_max == -1)
        name_max = 255;
    len = offsetof(struct dirent, d_name) + name_max + 1;
    ent = malloc(len);
    SOL_NULL_CHECK_GOTO(ent, end);

    success = readdir_r(dir, ent, &res);
    while (success == 0 && res) {
        if (strstartswith(ent->d_name, "trigger")) {
            if (check_trigger_name(ent->d_name, device->trigger_name)) {
                /* triggers dirs are of the form triggerX, so here we save X */
                device->trigger_id = atoi(ent->d_name + strlen("trigger"));
                device->manual_triggering = check_file_existence(
                    SYSFS_TRIGGER_NOW_BY_ID_PATH, device->trigger_id);
                result = true;
                break;
            }
        }

        success = readdir_r(dir, ent, &res);
    }

end:
    free(ent);
    closedir(dir);

    return result;
}

static bool
set_current_trigger(struct sol_iio_device *device, const char *trigger_name)
{
    char path[PATH_MAX];
    bool r;

    r = craft_filename_path(path, sizeof(path), CURRENT_TRIGGER_DEVICE_PATH,
        device->device_id);
    if (!r) {
        SOL_WRN("Could not open device current_trigger file");
        return false;
    }

    if (sol_util_write_file(path, "%s", trigger_name) < 0) {
        SOL_WRN("Could not write to device current_trigger file");
        return false;
    }

    return true;
}

static bool
create_sysfs_trigger(struct sol_iio_device *device)
{
    int len, id, i, tries = 10;
    char *trigger_name;
    bool result = false;

    id = rand();
    do {
        i = sol_util_write_file(SYSFS_TRIGGER_SYSFS_ADD_TRIGGER, "%d", id);
        if (i == -ENOENT) {
            SOL_WRN("No 'iio_sysfs_trigger' under '/sys/bus/iio/devices'."
                " Missing 'modprobe iio-trig-sysfs'?");
            return false;
        }

        /* If EINVAL, we try again. If not, give up */
        if (i < 0 && i != -EINVAL)
            goto error;

        if (i < 0)
            id = rand();
    } while (i < 0 && tries--);

    if (i < 0)
        goto error;

    /* Set device current trigger */
    len = asprintf(&trigger_name, "sysfstrig%d", id);
    if (len < 0)
        goto error;
    result = set_current_trigger(device, trigger_name);

    if (result)
        device->trigger_name = trigger_name;
    else {
        free(trigger_name);
        goto error;
    }


    return result;

error:
    SOL_WRN("Could not create sysfs trigger.");

    return false;
}

static bool
create_hrtimer_trigger(struct sol_iio_device *device, const char *trigger_name)
{
    int success, id, len;
    bool r = false;
    char *name = NULL;
    char path[PATH_MAX];

    if (check_file_existence(CONFIGFS_IIO_HRTIMER_TRIGGERS_PATH)) {
        if (strlen(trigger_name) == 0) {
            id = rand();
            len = asprintf(&name, "%d", id);
        } else
            len = asprintf(&name, "%s", trigger_name);

        if (len < 0)
            goto error;

        r = craft_filename_path(path, sizeof(path), CONFIGFS_IIO_HRTIMER_TRIGGER_PATH, name);
        if (!r) {
            goto error;
        }

        success = mkdir(path, S_IRWXU);
        if (success == 0) {
            r = set_current_trigger(device, name);
            if (r)
                device->trigger_name = name;
            else {
                goto error;
            }
        } else {
            SOL_WRN("Could not create hrtimer trigger dir: %s - %s",
                path, sol_util_strerrora(errno));
            goto error;
        }
    } else {
        SOL_WRN("IIO triggers folder '"CONFIGFS_IIO_HRTIMER_TRIGGERS_PATH "' does not exist.");
        goto error;
    }

    return r;

error:
    free(name);
    SOL_WRN("Could not create hrtimer trigger.");

    return false;
}

static bool
check_trigger(struct sol_iio_device *device, const struct sol_iio_config *config)
{
    char path[PATH_MAX];
    char *name = NULL;
    int len;
    bool r;

    r = craft_filename_path(path, sizeof(path), CURRENT_TRIGGER_DEVICE_PATH, device->device_id);
    if (!r) {
        SOL_WRN("Could not open IIO device%d trigger on sysfs", device->device_id);
        return false;
    }

    len = sol_util_read_file(path, "%ms", &name);
    if (len < 0) {
        if (config->trigger_name && strstartswith(config->trigger_name, HRTIMER_TRIGGER_PREFIX)) {
            SOL_INF("No current trigger for iio:device%d. Creating a hrtimer one.",
                device->device_id);
            if (!create_hrtimer_trigger(device, config->trigger_name + strlen(HRTIMER_TRIGGER_PREFIX)))
                return false;
        } else {
            SOL_INF("No current trigger for iio:device%d. Creating a sysfs one.",
                device->device_id);
            if (!create_sysfs_trigger(device))
                return false;
        }
    } else
        device->trigger_name = name;

    return check_manual_triggering(device);
}

static void
set_buffer_size(struct sol_iio_device *device, int buffer_size)
{
    char path[PATH_MAX];
    int r;

    if (!craft_filename_path(path, sizeof(path), BUFFER_LENGHT_DEVICE_PATH, device->device_id)) {
        SOL_WRN("Could not set IIO device buffer size");
        return;
    }

    if ((r = sol_util_write_file(path, "%d", buffer_size)) < 0) {
        SOL_WRN("Could not set IIO device buffer size to %d at '%s': %s",
            buffer_size, path, sol_util_strerrora(-r));
    }

    return;
}

static bool
set_buffer_enabled(struct sol_iio_device *device, bool enabled)
{
    char path[PATH_MAX];
    bool r;

    r = craft_filename_path(path, sizeof(path), BUFFER_ENABLE_DEVICE_PATH, device->device_id);
    if (!r)
        return false;

    if (sol_util_write_file(path, "%d", enabled) < 0)
        return false;

    return true;
}

static bool
get_buffer_value(struct sol_iio_device *device, int *value)
{
    char path[PATH_MAX];
    bool r;

    r = craft_filename_path(path, sizeof(path), BUFFER_ENABLE_DEVICE_PATH, device->device_id);
    if (!r)
        return false;

    if (sol_util_read_file(path, "%d", value) < 0)
        return false;

    return true;
}

static bool
set_channel_enabled(struct sol_iio_device *device, const char *channel_name, bool enabled)
{
    if (check_file_existence(CHANNEL_ENABLE_PATH, device->device_id,
        channel_name)) {
        char path[PATH_MAX];

        if (!craft_filename_path(path, sizeof(path), CHANNEL_ENABLE_PATH,
            device->device_id, channel_name))
            return false;

        if (sol_util_write_file(path, "%d", enabled) < 0)
            return false;
    }

    return true;
}

static size_t
calc_buffer_size(const struct sol_iio_device *device)
{
    struct sol_iio_channel *channel;
    int i;
    size_t size = 0;

    SOL_PTR_VECTOR_FOREACH_IDX (&device->channels, channel, i) {
        size += channel->storagebits;
    }

    return size / 8 + (size % 8 != 0);
}

static bool
device_reader_cb(void *data, int fd, uint32_t active_flags)
{
    struct sol_iio_device *device = data;
    struct reader_cb_data *reader_cb_data_elem = NULL;
    bool result = true;
    ssize_t ret;
    int i;

    if (active_flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL)) {
        SOL_WRN("Unexpected reading");
        result = false;
    }

    ret = sol_util_fill_buffer(fd, &device->buffer, device->buffer_size - device->buffer.used);
    if (ret <= 0) {
        result = false;
    } else if (device->buffer.used == device->buffer_size) {
        SOL_VECTOR_FOREACH_IDX (&device->reader_cb_list, reader_cb_data_elem, i) {
            reader_cb_data_elem->reader_cb((void *)reader_cb_data_elem->data, device);
        }
        sol_buffer_reset(&device->buffer);
    }

    if (!result) {
        device->fd_handler = NULL;
        close(device->fd);
        device->fd = -1;
    }

    return result;
}

static bool
setup_device_reader(struct sol_iio_device *device)
{
    int fd;
    char path[PATH_MAX];
    bool r;

    r = craft_filename_path(path, sizeof(path), DEVICE_PATH, device->device_id);
    if (!r) {
        SOL_WRN("Could not open IIO device%d: Could not find it's file name", device->device_id);
        return false;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        SOL_WRN("Could not open IIO device%d: Could not access it at %s : %s",
            device->device_id, path, sol_util_strerrora(errno));
        return false;
    }

    device->fd_handler = sol_fd_add(fd,
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_PRI | SOL_FD_FLAGS_ERR, device_reader_cb,
        device);
    if (!device->fd_handler) {
        SOL_WRN("Could not setup reader for device%d", device->device_id);
        close(fd);
        return false;
    }

    device->fd = fd;

    return true;
}

/* Some channels are named on the form  <type>[_x|_y|_z]
 * This function shall return the name without <x|y|z> components.
 * And remove both/ir/uv suffix frome some light internsity sensors.
 * The form <type>[Y][_modifier] is also common (Y is a number), and this function
 * remove numbers too, in an attempt to get the 'pure' name.
 * TODO there are other esoteric combinations - for them, if we care about,
 * we will probably need API calls to get the 'scale' and 'offset' file names */
static char *
channel_get_pure_name(const char *name)
{
    char channel_name[NAME_MAX];
    size_t channel_name_len;
    char *channel_pure_name;
    bool modified = false;

    SOL_NULL_CHECK(name, NULL);

    strncpy(channel_name, name, sizeof(channel_name) - 1);
    channel_name[sizeof(channel_name) - 1] = '\0';

    channel_name_len = strlen(channel_name);
    if (strendswith(channel_name, "_green")) {
        channel_name[channel_name_len - 6] = '\0';
        modified = true;
    }

    if (strendswith(channel_name, "_both") ||
        strendswith(channel_name, "_blue")) {
        channel_name[channel_name_len - 5] = '\0';
        modified = true;
    }

    if (strendswith(channel_name, "_red")) {
        channel_name[channel_name_len - 4] = '\0';
        modified = true;
    }

    if (strendswith(channel_name, "_ir") ||
        strendswith(channel_name, "_uv")) {
        channel_name[channel_name_len - 3] = '\0';
        modified = true;
    }

    channel_name_len = strlen(channel_name);
    if (channel_name_len > 2) {
        char *channel_name_suffix;

        channel_name_suffix = channel_name + channel_name_len - 2;
        if (streq(channel_name_suffix,  "_x") ||
            streq(channel_name_suffix,  "_y") || streq(channel_name_suffix,  "_z")) {

            channel_pure_name = strndup(channel_name, channel_name_len - 2);
            SOL_NULL_CHECK(channel_pure_name, NULL);
            return channel_pure_name;
        } else {
            /* Recreate channel name without Y_ components (Y is a number).
             * Idea is 's/[0-9]+//' */
            size_t i;
            char *original_channel_pure_name;

            channel_pure_name = calloc(1, channel_name_len + 1);
            SOL_NULL_CHECK(channel_pure_name, NULL);
            original_channel_pure_name = channel_pure_name;
            for (i = 0; i < channel_name_len; i++) {
                if (isalpha((uint8_t)channel_name[i]) || channel_name[i] == '-'
                    || channel_name[i] == '_')
                    *channel_pure_name++ = channel_name[i];
                else if (isdigit((uint8_t)channel_name[i])) {
                    modified = true;
                    continue;
                }
            }
            if (modified)
                return original_channel_pure_name;

            free(original_channel_pure_name);
            return NULL;
        }
    }

    if (modified) {
        channel_pure_name = strndup(channel_name, channel_name_len);
        SOL_NULL_CHECK(channel_pure_name, NULL);
        return channel_pure_name;
    }

    return NULL;
}

static bool
iio_set_oversampling_ratio(struct sol_iio_device *device, const struct sol_iio_config *config)
{
    char path[PATH_MAX];
    struct sol_str_table *iter;

    SOL_NULL_CHECK(device, false);

    for (iter = config->oversampling_ratio_table; iter->key; iter++) {
        if (!iter->val)
            continue;

        if (!craft_filename_path(path, sizeof(path), CHANNEL_OVERSAMPLING_RATIO_DEVICE_PATH,
            device->device_id, iter->key)) {
            SOL_WRN("Could not set oversampling ratio");
            return false;
        }

        if (sol_util_write_file(path, "%d", iter->val) <= 0) {
            SOL_WRN("Could not set oversampling ratio to %d at '%s'",
                iter->val, path);
            return false;
        }
    }

    return true;
}

static bool
iio_set_sampling_frequency(struct sol_iio_device *device, const struct sol_iio_config *config)
{
    char path[PATH_MAX];
    int frequency = config->sampling_frequency;

    SOL_NULL_CHECK(device, false);

    if (craft_filename_path(path, sizeof(path), SAMPLING_FREQUENCY_DEVICE_PATH,
        device->device_id)) {

        if (sol_util_write_file(path, "%d", frequency) > 0)
            return true;
    }

    if (craft_filename_path(path, sizeof(path), CHANNEL_SAMPLING_FREQUENCY_DEVICE_PATH,
        device->device_id, config->sampling_frequency_name)) {

        if (sol_util_write_file(path, "%d", frequency) > 0)
            return true;
    }

    if (craft_filename_path(path, sizeof(path), SAMPLING_FREQUENCY_BUFFER_PATH,
        device->device_id)) {

        if (sol_util_write_file(path, "%d", frequency) > 0)
            return true;
    }


    if (craft_filename_path(path, sizeof(path), SAMPLING_FREQUENCY_TRIGGER_PATH,
        device->trigger_id)) {

        if (sol_util_write_file(path, "%d", frequency) > 0)
            return true;
    }

    return false;
}

static bool
iio_set_channel_scale(struct sol_iio_channel *channel, double scale)
{
    char path[PATH_MAX], *pure_name = NULL;
    bool result = false;

    SOL_NULL_CHECK(channel, false);

    if (craft_filename_path(path, sizeof(path), CHANNEL_SCALE_PATH,
        channel->device->device_id, channel->name)) {

        if (sol_util_write_file(path, "%.9lf", scale) > 0) {
            channel->scale = scale;
            return true;
        }
    }

    /* If failed, try channel pure name */
    pure_name = channel_get_pure_name(channel->name);
    if (pure_name && craft_filename_path(path, sizeof(path), CHANNEL_SCALE_PATH,
        channel->device->device_id, pure_name)) {

        result = (sol_util_write_file(path, "%.9lf", scale) > 0);
        if (result)
            channel->scale = scale;
    }

    if (!result)
        SOL_WRN("Could not set scale to %.9lf on channel [%s] of device%d",
            scale, channel->name, channel->device->device_id);

    free(pure_name);

    return result;
}

static bool
iio_set_channel_offset(struct sol_iio_channel *channel, int offset)
{
    char path[PATH_MAX], *pure_name = NULL;
    bool result = false;

    SOL_NULL_CHECK(channel, false);

    if (craft_filename_path(path, sizeof(path), CHANNEL_OFFSET_PATH,
        channel->device->device_id, channel->name)) {

        if (sol_util_write_file(path, "%d", offset) > 0) {
            channel->offset = offset;
            return true;
        }
    }

    /* If failed, try channel pure name */
    pure_name = channel_get_pure_name(channel->name);
    if (pure_name && craft_filename_path(path, sizeof(path), CHANNEL_OFFSET_PATH,
        channel->device->device_id, pure_name)) {

        result = sol_util_write_file(path, "%d", offset) > 0;
        if (result)
            channel->offset = offset;
    }

    if (!result)
        SOL_WRN("Could not set offset to %d on channel [%s] of device%d",
            offset, channel->name, channel->device->device_id);

    free(pure_name);

    return result;
}

static bool
get_mount_matrix(struct sol_iio_device *device, double *mount_matrix)
{
    char buf[100];
    char *tmp1, *tmp2;
    char path[PATH_MAX];
    bool result = false;
    int len, i;
    double d;
    const char *base = NULL;

    /* Find the mount matrix sysfs node */
    if (check_file_existence(SYSFS_MOUNT_MATRIX, device->device_id))
        base = SYSFS_MOUNT_MATRIX;
    else if (check_file_existence(SYSFS_OUT_MOUNT_MATRIX, device->device_id))
        base = SYSFS_OUT_MOUNT_MATRIX;
    else if (check_file_existence(SYSFS_IN_MOUNT_MATRIX, device->device_id))
        base = SYSFS_IN_MOUNT_MATRIX;
    else {
        SOL_DBG("Could not find mount_matrix for device%d", device->device_id);
        return false;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    result = craft_filename_path(path, sizeof(path), base, device->device_id);
#pragma GCC diagnostic pop
    if (!result) {
        SOL_WRN("Could not open device mount_matrix file");
        return false;
    }

    len = sol_util_read_file(path, "%99[^\n]", buf);
    if (len < 0) {
        SOL_WRN("Coult not read mount matrix %s on sysfs.", path);
        return false;
    }

    SOL_DBG("in_mount_matrix=%s", buf);

    tmp1 = buf;
    for (i = 0; i < MOUNT_MATRIX_LEN; i++) {
        d = strtod(tmp1, &tmp2);
        if (tmp1 == tmp2)
            return false;
        mount_matrix[i] = d;
        tmp1 = tmp2 + 1;
        SOL_DBG("matrix[%d]=%f", i, mount_matrix[i]);
    }

    return result;
}

SOL_API struct sol_iio_device *
sol_iio_open(int device_id, const struct sol_iio_config *config)
{
    int i;
    int prefix_len = strlen(HRTIMER_TRIGGER_PREFIX);
    bool r;
    bool buffer_existence;
    char path[PATH_MAX];
    struct sol_iio_device *device = NULL;
    struct iio_opened_device *entry = NULL;
    struct reader_cb_data *reader_cb_data_elem = NULL;

    SOL_NULL_CHECK(config, NULL);
#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(config->api_version != SOL_IIO_CONFIG_API_VERSION)) {
        SOL_WRN("IIO config version '%" PRIu16 "' is unexpected, expected '%"
            PRIu16 "'", config->api_version, SOL_IIO_CONFIG_API_VERSION);
        return NULL;
    }
#endif
    SOL_VECTOR_FOREACH_IDX (&iio_opened_devices, entry, i) {
        if (device_id == entry->device->device_id) {
            if ((config->buffer_size == entry->config->buffer_size)
                && (config->sampling_frequency == entry->config->sampling_frequency)) {
                device = entry->device;
                reader_cb_data_elem = sol_vector_append(&device->reader_cb_list);
                SOL_NULL_CHECK(reader_cb_data_elem, NULL);
                reader_cb_data_elem->data = config->data;
                reader_cb_data_elem->reader_cb = config->sol_iio_reader_cb;
                entry->refcount++;
                return device;
            } else {
                SOL_ERR("device%d is already open, but could not reuse it with different config",
                    device_id);
                return NULL;
            }
        }
    }

    device = calloc(1, sizeof(struct sol_iio_device));
    SOL_NULL_CHECK(device, NULL);
    sol_ptr_vector_init(&device->channels);
    device->buffer = SOL_BUFFER_INIT_EMPTY;
    device->fd = -1;
    sol_vector_init(&device->reader_cb_list, sizeof(struct reader_cb_data));

    device->device_id = device_id;

    /* Let's keep device name file open until close, so we can prevent a
     * rogue destruction of sysfs structure by unloading kernel module related
     * to it */
    r = craft_filename_path(path, sizeof(path), DEVICE_NAME_PATH,
        device->device_id);
    if (!r) {
        SOL_WRN("Could not open IIO device%d name file", device->device_id);
        goto error;
    }
    device->name_fd = open(path, O_RDONLY | O_CLOEXEC);
    if (device->name_fd == -1) {
        SOL_WRN("Could not open IIO device%d name file [%s]", device->device_id,
            path);
        goto error;
    }

    buffer_existence = false;
    if (check_file_existence(BUFFER_ENABLE_DEVICE_PATH, device->device_id)) {
        buffer_existence = true;
    }

    if (config->buffer_size > -1) {
        if (!buffer_existence) {
            SOL_WRN("Buffer is enabled but device%d does not support Buffer.",
                device->device_id);
            goto error;
        }

        if (!config->sol_iio_reader_cb) {
            SOL_WRN("Buffer is enabled for device%d but no 'sol_iio_reader_cb'"
                " was defined.'", device->device_id);
            goto error;
        }

        reader_cb_data_elem = sol_vector_append(&device->reader_cb_list);
        SOL_NULL_CHECK_GOTO(reader_cb_data_elem, error);
        reader_cb_data_elem->reader_cb = config->sol_iio_reader_cb;
        reader_cb_data_elem->data = config->data;

        if (config->trigger_name && config->trigger_name[0] != '\0') {
            /* if name start with HRTIMER_TRIGGER_PREFIX, it is a hrtimer trigger */
            if (strstartswith(config->trigger_name, HRTIMER_TRIGGER_PREFIX)) {
                if (config->trigger_name[prefix_len] != '\0') {
                    if (!set_current_trigger(device, config->trigger_name + prefix_len))
                        SOL_WRN("Could not set device%d current trigger", device->device_id);
                }
            } else {
                if (!set_current_trigger(device, config->trigger_name))
                    SOL_WRN("Could not set device%d current trigger", device->device_id);
            }
        }

        if (!check_trigger(device, config)) {
            SOL_WRN("No trigger available for device%d", device->device_id);
            goto error;
        }

        if (config->buffer_size != 0)
            set_buffer_size(device, config->buffer_size);

        if (!device->manual_triggering) {
            SOL_INF("No 'trigger_now' file on device%d current trigger. "
                "It won't be possible to manually trigger a reading on device",
                device->device_id);
        }

        if (!setup_device_reader(device)) {
            SOL_WRN("Could not setup device%d reading", device->device_id);
            goto error;
        }
        device->buffer_enabled = true;
    } else {
        /* buffer_size == -1 means that user doesn't want to use the buffer */
        device->buffer_enabled = false;
        if (buffer_existence) {
            if (!set_buffer_enabled(device, false)) {
                SOL_WRN("Could not disable buffer for device%d", device->device_id);
            }
        }
    }

    if (config->sampling_frequency > -1) {
        if (!iio_set_sampling_frequency(device, config))
            SOL_WRN("Could not set device%d sampling frequency", device->device_id);
    }

    if (config->oversampling_ratio_table) {
        if (!iio_set_oversampling_ratio(device, config))
            SOL_WRN("Could not set device%d oversampling ratio", device->device_id);
    }

    device->mount_matrix = calloc(MOUNT_MATRIX_LEN, sizeof(double));
    SOL_NULL_CHECK_GOTO(device->mount_matrix, error);

    r = get_mount_matrix(device, device->mount_matrix);
    if (!r) {
        free(device->mount_matrix);
        device->mount_matrix = NULL;
    }

    SOL_DBG("iio device created. device%d - buffer_enabled: %d - manual_trigger: %d"
        " - trigger_name: %s - trigger_id: %d - mount_matrix: %p", device->device_id,
        device->buffer_enabled, device->manual_triggering, device->trigger_name,
        device->trigger_id, device->mount_matrix);

    entry = sol_vector_append(&iio_opened_devices);
    SOL_NULL_CHECK_GOTO(entry, error);
    entry->device = device;

    entry->config = calloc(1, sizeof(struct sol_iio_config));
    SOL_NULL_CHECK_GOTO(entry->config, error_config);
    memcpy(entry->config, config, sizeof(struct sol_iio_config));

    entry->refcount = 1;
    return device;

error_config:
    sol_vector_del_last(&iio_opened_devices);
error:
    sol_iio_close(device);

    return NULL;
}

static bool
enable_channel_scan(struct sol_iio_channel *channel)
{
    struct sol_iio_device *device = channel->device;
    char path[PATH_MAX];
    int current_value, len, ret, buffer_enable;

    if (!craft_filename_path(path, sizeof(path), CHANNEL_SCAN_ENABLE_PATH,
        device->device_id, channel->name)) {

        return false;
    }

    /* First, check if not already enabled */
    len = sol_util_read_file(path, "%d", &current_value);
    if (len < 0) {
        SOL_WRN("Could not read from %s", path);
        return false;
    }

    if (current_value != 1) {
        if (!get_buffer_value(device, &buffer_enable)) {
            SOL_WRN("Coult not get buffer status for device%d", device->device_id);
            return false;
        }

        if (buffer_enable) {
            if (!set_buffer_enabled(device, false)) {
                SOL_WRN("Could not disable buffer for device%d", device->device_id);
                return false;
            }
        }

        ret = sol_util_write_file(path, "%d", 1);
        if (ret < 0) {
            SOL_WRN("Could not enable scan %s for device%d", path, device->device_id);
        }

        if (buffer_enable) {
            if (!set_buffer_enabled(device, true)) {
                SOL_WRN("Could not enable buffer for device%d", device->device_id);
                return false;
            }
        }
        return ret > 0;
    }
    return true;
}

static bool
read_channel_scan_info(struct sol_iio_channel *channel)
{
    struct sol_iio_device *device = channel->device;
    char path[PATH_MAX];
    char *type, *original_type;
    int len;
    bool crafted;

    crafted = craft_filename_path(path, sizeof(path), CHANNEL_SCAN_TYPE_PATH,
        device->device_id, channel->name);
    if (crafted) {
        len = sol_util_read_file(path, "%ms", &type);
        if (len < 0) return false;
    } else
        return false;

    original_type = type;
    /* Form of type is [be|le]:[s|u]bits/storagebits[>>shift] */
    if (strstartswith(type, "be:")) {
        channel->little_endian = false;
        type += 3;
    } else if (strstartswith(type, "le:")) {
        channel->little_endian = true;
        type += 3;
    } else
        channel->little_endian = true;

    if (type[0] == 's') {
        channel->is_signed = true;
        type++;
    } else if (type[0] == 'u') {
        channel->is_signed = false;
        type++;
    } else
        channel->is_signed = true; /* TODO check if signed is the default */

    len = sscanf(type, "%d/%d>>%d", &channel->bits, &channel->storagebits, &channel->shift);

    free(original_type);

    if (len == 3) {
        return true;
    } else if (len == 2) {
        channel->shift = 0;
        return true;
    }

    return false;
}

static int
get_scale(const struct sol_iio_device *device, const char *prefix_name, double *scale)
{
    char path[PATH_MAX];
    char *pure_name = NULL;
    int r = 0;

#define GET_SCALE(_name) \
    do { \
        if (craft_filename_path(path, sizeof(path), CHANNEL_SCALE_PATH, \
            device->device_id, _name)) { \
            r = sol_util_read_file(path, "%lf", scale); \
            if (r > 0) \
                goto end; \
        } \
    } while (0)

    GET_SCALE(prefix_name);
    pure_name = channel_get_pure_name(prefix_name);
    GET_SCALE(pure_name);

    SOL_INF("Could not get scale for channel [%s] in device%d. Assuming 1.0",
        prefix_name, device->device_id);
    *scale = 1.0;

#undef GET_SCALE

end:
    if (pure_name)
        free(pure_name);

    if (r > 0)
        return 0;

    return r;
}

static int
channel_get_scale(struct sol_iio_channel *channel)
{
    SOL_NULL_CHECK(channel->device, -EINVAL);
    SOL_NULL_CHECK(channel->name, -EINVAL);

    return get_scale(channel->device, channel->name, &channel->scale);
}

static int
get_offset(const struct sol_iio_device *device, const char *prefix_name, double *offset)
{
    char path[PATH_MAX];
    char *pure_name = NULL;
    int r = 0;

#define GET_OFFSET(_name) \
    do { \
        if (craft_filename_path(path, sizeof(path), CHANNEL_OFFSET_PATH, \
            device->device_id, _name)) { \
            r = sol_util_read_file(path, "%lf", offset); \
            if (r > 0) \
                goto end; \
        } \
    } while (0)

    GET_OFFSET(prefix_name);
    pure_name = channel_get_pure_name(prefix_name);
    GET_OFFSET(pure_name);

    SOL_INF("Could not get offset for channel [%s] in device%d. Assuming 0",
        prefix_name, device->device_id);
    *offset = 0.0;

#undef GET_OFFSET

end:
    if (pure_name)
        free(pure_name);

    if (r > 0)
        return 0;

    return r;
}

static int
channel_get_offset(struct sol_iio_channel *channel)
{
    SOL_NULL_CHECK(channel->device, -EINVAL);
    SOL_NULL_CHECK(channel->name, -EINVAL);

    return get_offset(channel->device, channel->name, (double *)&channel->offset);
}

static void
iio_del_channel(struct sol_iio_device *device, struct sol_iio_channel *channel)
{
    /* Deactivate device firmware/hardware after use */
    set_channel_enabled(device, channel->name, false);

    free(channel);
}

SOL_API void
sol_iio_close(struct sol_iio_device *device)
{
    int i;
    struct sol_iio_channel *channel;
    struct iio_opened_device *entry = NULL;
    struct iio_opened_device *remove_entry = NULL;

    SOL_NULL_CHECK(device);

    SOL_VECTOR_FOREACH_IDX (&iio_opened_devices, entry, i) {
        if (device->device_id == entry->device->device_id) {
            entry->refcount--;
            if (entry->refcount == 0) {
                SOL_DBG("Close device%d", device->device_id);
                remove_entry = entry;
                free(entry->config);
            } else {
                SOL_DBG("Won't close device%d refcount=%d > 0", device->device_id, entry->refcount);
                return;
            }
            break;
        }
    }

    if (remove_entry)
        sol_vector_del_element(&iio_opened_devices, remove_entry);

    if (iio_opened_devices.len == 0)
        sol_vector_clear(&iio_opened_devices);

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&device->channels, channel, i) {
        iio_del_channel(device, channel);
    }
    sol_ptr_vector_clear(&device->channels);

    if ((device->buffer_enabled) && (!set_buffer_enabled(device, false)))
        SOL_WRN("Could not disable buffer for device%d", device->device_id);

    if (device->fd_handler) sol_fd_del(device->fd_handler);
    if (device->fd > -1) close(device->fd);
    if (device->name_fd > -1) close(device->name_fd);
    free(device->mount_matrix);

    sol_vector_clear(&device->reader_cb_list);

    sol_buffer_fini(&device->buffer);
    free(device->trigger_name);
    free(device);
}

static bool
read_channel_index_in_buffer(struct sol_iio_channel *channel)
{
    struct sol_iio_device *device = channel->device;
    char path[PATH_MAX];
    bool r;

    r = craft_filename_path(path, sizeof(path), CHANNEL_SCAN_INDEX_PATH,
        device->device_id, channel->name);
    if (!r || sol_util_read_file(path, "%d", &channel->index) < 0)
        return false;

    return true;
}

SOL_API struct sol_iio_channel *
sol_iio_add_channel(struct sol_iio_device *device, const char *name, const struct sol_iio_channel_config *config)
{
    struct sol_iio_channel *channel;
    bool processed;
    int r;

    SOL_NULL_CHECK(device, NULL);
    SOL_NULL_CHECK(name, NULL);
    SOL_NULL_CHECK(config, NULL);

#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(config->api_version != SOL_IIO_CHANNEL_CONFIG_API_VERSION)) {
        SOL_WRN("IIO channel config version '%" PRIu16 "' is unexpected, expected '%" PRIu16 "'",
            config->api_version, SOL_IIO_CHANNEL_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    /* First try '_raw' suffix, then '_input' */
    if (check_file_existence(CHANNEL_RAW_PATH, device->device_id, name))
        processed = false;
    else if (check_file_existence(CHANNEL_PROCESSED_PATH, device->device_id, name))
        processed = true;
    else {
        SOL_WRN("Could not find channel [%s] for device%d", name,
            device->device_id);
        return NULL;
    }

    channel = calloc(1, sizeof(struct sol_iio_channel) + strlen(name) + 1);
    SOL_NULL_CHECK(channel, NULL);
    memcpy(channel->name, name, strlen(name));

    channel->device = device;
    channel->processed = processed;

    r = 0;
    if (config->scale > -1)
        r = !!iio_set_channel_scale(channel, config->scale);

    if (!r)
        channel_get_scale(channel);

    r = 0;
    if (config->use_custom_offset)
        r = !!iio_set_channel_offset(channel, config->offset);

    if (!r)
        channel_get_offset(channel);

    if (device->buffer_enabled) {
        if (!enable_channel_scan(channel)) {
            SOL_WRN("Could not enable scanning of channel [%s] in device%d",
                channel->name, device->device_id);
            goto error;
        }

        if (!read_channel_scan_info(channel)) {
            SOL_WRN("Could not read scanning info of channel [%s] in device%d",
                channel->name, device->device_id);
            goto error;
        }

        if (!read_channel_index_in_buffer(channel)) {
            SOL_WRN("Could not read index in buffer of channel [%s] in device%d",
                channel->name, device->device_id);
            goto error;
        }

        channel->mask = ((uint64_t)1 << channel->bits) - 1;
    }

    r = sol_ptr_vector_append(&channel->device->channels, channel);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    /* Some device firmware/hardware feature default state is off, thus need to activate it before read */
    if (!set_channel_enabled(device, name, true))
        SOL_WRN("Could not activate device channel [%s] in device%d",
            name, device->device_id);

    SOL_DBG("channel [%s] added. scale: %.9lf - offset: %d - storagebits: %d"
        " - bits: %d - mask: %" PRIu64, channel->name, channel->scale,
        channel->offset, channel->storagebits, channel->bits, channel->mask);

    return channel;

error:
    iio_del_channel(device, channel);

    return NULL;
}

static int
iio_read_buffer_channel_value(struct sol_iio_channel *channel, double *value)
{
    uint64_t data = 0;
    int64_t s_data;
    unsigned j, offset_bytes;
    int i, storage_bytes;
    bool negative = false;
    struct sol_iio_device *device = channel->device;
    uint8_t *buffer = device->buffer.data;

    SOL_NULL_CHECK(buffer, -EINVAL);

    if (channel->storagebits > 64) {
        SOL_WRN("Could not read channel [%s] value - more than 64 bits of"
            " storage - found %d. Use sol_iio_read_channel_raw_buffer() instead",
            channel->name, channel->storagebits);
        return -EBADMSG;
    }

    if (channel->offset_in_buffer + channel->storagebits > device->buffer_size * 8) {
        SOL_WRN("Invalid read on buffer.");
        return -EBADMSG;
    }

    offset_bytes = channel->offset_in_buffer / 8;
    storage_bytes = channel->storagebits / 8;
    if (channel->little_endian) {
        for (i = 0, j = 0; i < storage_bytes; i++, j += 8) {
            data |= (uint64_t)buffer[i + offset_bytes] << j;
        }
    } else {
        for (i = storage_bytes - 1, j = 0; i >= 0; i--, j += 8) {
            data |= (uint64_t)buffer[i + offset_bytes] << j;
        }
    }

    data >>= channel->shift;

    /* Remove the top useless bits */
    data &= channel->mask;

    /* If signed and msb is 1, we have a negative number */
    if (channel->is_signed) {
        negative = data & (1 << (channel->bits - 1));
    }

    if (negative) {
        s_data = data | ~channel->mask;
        *value = s_data;
    } else
        *value = data;

    if (!channel->processed)
        *value = (*value + channel->offset) * channel->scale;

    return 0;
}

SOL_API int
sol_iio_read_channel_value(struct sol_iio_channel *channel, double *value)
{
    int len;
    double raw_value;
    char path[PATH_MAX];
    struct sol_iio_device *device;
    bool r;

    SOL_NULL_CHECK(channel, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    device = channel->device;

    if (device->buffer_enabled) {
        return iio_read_buffer_channel_value(channel, value);
    }

    r = craft_filename_path(path, sizeof(path),
        channel->processed ? CHANNEL_PROCESSED_PATH : CHANNEL_RAW_PATH,
        device->device_id, channel->name);
    if (!r) {
        SOL_WRN("Could not read channel [%s] in device%d", channel->name,
            device->device_id);
        return -ENOMEM;
    }

    len = sol_util_read_file(path, "%lf", &raw_value);
    if (len < 0) {
        SOL_WRN("Could not read channel [%s] in device%d", channel->name,
            device->device_id);
        return -EIO;
    }

    if (channel->processed)
        *value = raw_value;
    else
        *value = (raw_value + channel->offset) * channel->scale;
    return 0;
}

static int
calc_channel_offset_in_buffer(const struct sol_iio_channel *channel)
{
    struct sol_iio_channel *channel_itr;
    int i, offset = 0;

    SOL_PTR_VECTOR_FOREACH_IDX (&channel->device->channels, channel_itr, i) {
        if (channel_itr->index < channel->index)
            offset += channel->storagebits;
    }

    return offset;
}

SOL_API int
sol_iio_device_trigger(struct sol_iio_device *device)
{
    char path[PATH_MAX];
    bool r;
    int i;

    SOL_NULL_CHECK(device, -EINVAL);

    if (!device->manual_triggering) {
        SOL_WRN("No manual triggering available for device%d", device->device_id);
        return -EBADF;
    }

    r = craft_filename_path(path, sizeof(path), SYSFS_TRIGGER_NOW_BY_ID_PATH,
        device->trigger_id);
    if (!r) {
        SOL_WRN("No valid trigger_now file available for trigger [%s]",
            device->trigger_name);
        return -EBADF;
    }

    if ((i = sol_util_write_file(path, "%d", 1)) < 0) {
        SOL_WRN("Could not write to trigger_now file for trigger [%s]: %s",
            device->trigger_name, sol_util_strerrora(i));
        return -EBADF;
    }

    return 0;
}

SOL_API int
sol_iio_device_start_buffer(struct sol_iio_device *device)
{
    struct sol_iio_channel *channel;
    int i;

    SOL_NULL_CHECK(device, -EINVAL);

    /* Enable device after added all channels */
    if (device->buffer_enabled && !set_buffer_enabled(device, true)) {
        SOL_WRN("Could not enable buffer for device. No readings will be performed");
        return -EBADMSG;
    }

    device->buffer_size = calc_buffer_size(device);
    i = sol_buffer_ensure(&device->buffer, device->buffer_size);
    if (i < 0) {
        SOL_WRN("Could not alloc buffer for device. No readings will be performed");
        return -ENOMEM;
    }

    /* Now that all channels have been added, calc their offset in buffer */
    SOL_PTR_VECTOR_FOREACH_IDX (&device->channels, channel, i) {
        channel->offset_in_buffer = calc_channel_offset_in_buffer(channel);
    }

    return 0;
}

static enum sol_util_iterate_dir_reason
resolve_name_path_cb(void *data, const char *dir_path, struct dirent *ent)
{
    struct resolve_name_path_data *result = data;
    char path[PATH_MAX], *name;
    int len;

    if (strstartswith(ent->d_name, "iio:device")) {
        if (craft_filename_path(path, sizeof(path),
            DEVICE_NAME_PATH_BY_DEVICE_DIR, ent->d_name)) {

            len = sol_util_read_file(path, "%ms", &name);
            if (len > 0) {
                if (streq(name, result->name)) {
                    result->id = atoi(ent->d_name + strlen("iio:device"));
                    free(name);
                    return SOL_UTIL_ITERATE_DIR_STOP;
                }
                free(name);
            }
        }
    }

    return SOL_UTIL_ITERATE_DIR_CONTINUE;
}

static int
resolve_name_path(const char *name)
{
    struct resolve_name_path_data data = { .id = -1, .name = name };

    sol_util_iterate_dir(SYSFS_DEVICES_PATH, resolve_name_path_cb, &data);

    return data.id;
}

static enum sol_util_iterate_dir_reason
resolve_absolute_path_cb(void *data, const char *dir_path, struct dirent *ent)
{
    struct resolve_absolute_path_data *result = data;
    char path[PATH_MAX], real_path[PATH_MAX];

    if (craft_filename_path(path, sizeof(path),
        SYSFS_DEVICE_PATH, ent->d_name)) {

        if (realpath(path, real_path)) {
            SOL_DBG("resolve_absolute_path_cb - Real path: %s", real_path);
            if (strstartswith(real_path, result->path)) {
                result->id = atoi(ent->d_name + strlen("iio:device"));
                return SOL_UTIL_ITERATE_DIR_STOP;
            }
        }
    }

    return SOL_UTIL_ITERATE_DIR_CONTINUE;
}

static char *
resolve_path(const char *path, char *resolved_path)
{
    int r;
    char *p;
    glob_t result;

    r = glob(path, GLOB_MARK | GLOB_TILDE, NULL, &result);
    if (r != 0) {
        switch (r) {
        case GLOB_NOMATCH:
            errno = ENOENT;
            return NULL;
        case GLOB_NOSPACE:
            errno = ENOMEM;
            return NULL;
        case GLOB_ABORTED:
        default:
            errno = EINVAL;
            return NULL;
        }
    }

    p = realpath(result.gl_pathv[0], resolved_path);
    globfree(&result);
    return p ? resolved_path : NULL;
}

static int
resolve_absolute_path(const char *address)
{
    char real_path[PATH_MAX];
    struct resolve_absolute_path_data result = { .id = -1 };
    struct timespec start = sol_util_timespec_get_current();

    /* Wait up to one second for the file to exist. Useful if we created
     * via i2c */
    SOL_DBG("Trying to open address: %s", address);
    while (result.id == -1) {
        struct timespec elapsed, now;

        if (resolve_path(address, real_path)) {
            result.path = real_path;
            SOL_DBG("resolve_absolute_path - Real path: %s", real_path);
            sol_util_iterate_dir(SYSFS_DEVICES_PATH, resolve_absolute_path_cb, &result);
        }

        now = sol_util_timespec_get_current();
        sol_util_timespec_sub(&now, &start, &elapsed);
        if (elapsed.tv_sec > 0)
            break;
    }

    return result.id;
}

static int
resolve_i2c_path(const char *address)
{
    unsigned int bus, device;
    char path[PATH_MAX], real_path[PATH_MAX];
    struct resolve_absolute_path_data result = { .id = -1 };

    if (sscanf(address, "%u-%u", &bus, &device) != 2) {
        SOL_WRN("Unexpected i2c address format. Got [%s], expected X-YYYY,"
            " where X is bus number and YYYY is device address", address);
        return -1;
    }

    if (craft_filename_path(path, sizeof(path), I2C_DEVICES_PATH, bus, device)) {
        /* Idea: check if there's a symbolic link on iio/devices to the same
         * destination as the i2c dir */
        if (realpath(path, real_path)) {
            result.path = real_path;
            sol_util_iterate_dir(SYSFS_DEVICES_PATH, resolve_absolute_path_cb, &result);
        }
    }

    return result.id;
}

static int
check_device_id(int id)
{
    char path[PATH_MAX];
    struct stat st;

    if (craft_filename_path(path, sizeof(path), DEVICE_NAME_PATH, id)) {
        if (!stat(path, &st))
            return id;
    }

    return -1;
}

static int
resolve_device_address(const char *address)
{
    char *end_ptr;
    int i;

    SOL_NULL_CHECK(address, -1);

    if (strstartswith(address, "/"))
        return resolve_absolute_path(address);

    if (strstartswith(address, "i2c/"))
        return resolve_i2c_path(address + strlen("i2c/"));

    errno = 0;
    i = strtol(address, &end_ptr, 0);
    if (!errno && *end_ptr == '\0')
        return check_device_id(i);

    return resolve_name_path(address);
}

static char *
create_device_address(struct sol_str_slice *command)
{
    char *rel_path = NULL, *dev_number_s = NULL, *dev_name = NULL, *result = NULL;
    struct sol_vector instructions = SOL_VECTOR_INIT(struct sol_str_slice);
    struct sol_buffer path = SOL_BUFFER_INIT_EMPTY;

    if (strstartswith(command->data, "create,i2c,")) {
#ifndef USE_I2C
        SOL_WRN("No support for i2c");
        goto end;
#else
        unsigned int dev_number;
        char *end_ptr;
        int r;

        instructions = sol_str_slice_split(*command, ",", 5);

        if (instructions.len < 5) {
            SOL_WRN("Invalid create device path. Expected 'create,i2c,<rel_path>,"
                "<devnumber>,<devname>'");
            goto end;
        }

        rel_path = sol_str_slice_to_str(
            *(const struct sol_str_slice *)sol_vector_get(&instructions, REL_PATH_IDX));
        SOL_NULL_CHECK_GOTO(rel_path, end);

        dev_number_s = sol_str_slice_to_str(
            *(const struct sol_str_slice *)sol_vector_get(&instructions, DEV_NUMBER_IDX));
        SOL_NULL_CHECK_GOTO(dev_number_s, end);

        errno = 0;
        dev_number = strtoul(dev_number_s, &end_ptr, 0);
        if (errno || *end_ptr != '\0')
            goto end;

        dev_name = sol_str_slice_to_str(
            *(const struct sol_str_slice *)sol_vector_get(&instructions, DEV_NAME_IDX));
        SOL_NULL_CHECK_GOTO(dev_name, end);

        r = sol_i2c_create_device(rel_path, dev_name, dev_number, &path);

        if (r >= 0 || r == -EEXIST)
            result = sol_buffer_steal(&path, NULL);
#endif
    }

end:
    free(rel_path);
    free(dev_number_s);
    free(dev_name);
    sol_vector_clear(&instructions);
    sol_buffer_fini(&path);

    return result;
}

SOL_API int
sol_iio_address_device(const char *commands)
{
    struct sol_vector commands_vector;
    struct sol_str_slice *command;
    char *command_s;
    int r = -1, command_index = 0;

    SOL_NULL_CHECK(commands, -EINVAL);

    commands_vector = sol_str_slice_split(sol_str_slice_from_str(commands), " ", 0);

    do {
        command = sol_vector_get(&commands_vector, command_index++);
        if (!command) {
            SOL_WRN("Could not create or resolve device address using any of commands");

            r = -EINVAL;
            goto end;
        }

        SOL_DBG("IIO device creation/resolving dispatching command: %.*s",
            SOL_STR_SLICE_PRINT(*command));

        if (strstartswith(command->data, "create,"))
            command_s = create_device_address(command);
        else
            command_s = sol_str_slice_to_str(*command);

        if (command_s) {
            r = resolve_device_address(command_s);
            free(command_s);
        }
    } while (r < 0);

end:
    sol_vector_clear(&commands_vector);

    return r;
}

SOL_API struct sol_str_slice
sol_iio_read_channel_raw_buffer(struct sol_iio_channel *channel)
{
    struct sol_str_slice slice = SOL_STR_SLICE_EMPTY;
    unsigned int offset_bytes, storage_bytes;

    SOL_NULL_CHECK(channel, slice);
    SOL_NULL_CHECK(channel->device->buffer.data, slice);

    if (!channel->device->buffer_enabled) {
        SOL_WRN("sol_iio_read_channel_raw_buffer() only works when buffer"
            " is enabled.");
        return slice;
    }

    offset_bytes = channel->offset_in_buffer / 8;
    storage_bytes = channel->storagebits / 8;

    slice.len = storage_bytes;
    slice.data = (char *)channel->device->buffer.data + offset_bytes;

    return slice;
}

SOL_API int
sol_iio_mount_calibration(struct sol_iio_device *device, sol_direction_vector *value)
{
    int i;
    double tmp[3];

    if (!device->mount_matrix)
        return -1;

    // mount correction
    for (i = 0; i < 3; i++) {
        tmp[i] = value->x * device->mount_matrix[i * 3]
            + value->y * device->mount_matrix[i * 3 + 1]
            + value->z * device->mount_matrix[i * 3 + 2];
    }

    value->x = tmp[0];
    value->y = tmp[1];
    value->z = tmp[2];
    SOL_DBG("%f-%f-%f", value->x, value->y, value->z);
    return 0;
}

SOL_API const char *
sol_iio_channel_get_name(const struct sol_iio_channel *channel)
{
    SOL_NULL_CHECK(channel, NULL);

    return channel->name;
}

SOL_API int
sol_iio_device_get_scale(const struct sol_iio_device *device, const char *prefix_name, double *scale)
{
    SOL_NULL_CHECK(device, -EINVAL);
    SOL_NULL_CHECK(prefix_name, -EINVAL);
    SOL_NULL_CHECK(scale, -EINVAL);

    return get_scale(device, prefix_name, scale);
}

SOL_API int
sol_iio_device_get_offset(const struct sol_iio_device *device, const char *prefix_name, double *offset)
{
    SOL_NULL_CHECK(device, -EINVAL);
    SOL_NULL_CHECK(prefix_name, -EINVAL);
    SOL_NULL_CHECK(offset, -EINVAL);

    return get_offset(device, prefix_name, offset);
}

SOL_API int
sol_iio_device_get_sampling_frequency(const struct sol_iio_device *device, const char *prefix_name, int *sampling_frequency)
{
    char path[PATH_MAX];
    int r;

    SOL_NULL_CHECK(device, -EINVAL);
    SOL_NULL_CHECK(prefix_name, -EINVAL);
    SOL_NULL_CHECK(sampling_frequency, -EINVAL);


    if (craft_filename_path(path, sizeof(path), CHANNEL_SAMPLING_FREQUENCY_DEVICE_PATH,
        device->device_id, prefix_name)) {
        r = sol_util_read_file(path, "%d", sampling_frequency);
        SOL_INT_CHECK(r, < 0, r);
    }

    if (craft_filename_path(path, sizeof(path), SAMPLING_FREQUENCY_DEVICE_PATH,
        device->device_id)) {
        r = sol_util_read_file(path, "%d", sampling_frequency);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}
