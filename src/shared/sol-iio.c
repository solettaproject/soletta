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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <inttypes.h>
#include <unistd.h>

#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-str-slice.h>
#include <sol-util.h>
#include <sol-util-file.h>
#include <sol-vector.h>

#include "sol-iio.h"

struct sol_iio_device {
    char *trigger_name;
    void (*reader_cb)(void *data, struct sol_iio_device *device);
    const void *reader_cb_data;
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

    char name[]; /* Must be last. Memory trick in place. */
};

#define DEVICE_PATH "/dev/iio:device%d"
#define SYSFS_DEVICES_PATH "/sys/bus/iio/devices"

#define DEVICE_NAME_PATH SYSFS_DEVICES_PATH "/iio:device%d/name"

#define BUFFER_ENABLE_DEVICE_PATH SYSFS_DEVICES_PATH "/iio:device%d/buffer/enable"
#define BUFFER_LENGHT_DEVICE_PATH SYSFS_DEVICES_PATH "/iio:device%d/buffer/length"
#define CURRENT_TRIGGER_DEVICE_PATH SYSFS_DEVICES_PATH "/iio:device%d/trigger/current_trigger"

#define SYSFS_TRIGGER_NOW_PATH SYSFS_DEVICES_PATH "/%s/trigger_now"
#define SYSFS_TRIGGER_NOW_BY_ID_PATH SYSFS_DEVICES_PATH "/trigger%d/trigger_now"
#define SYSFS_TRIGGER_NAME_PATH SYSFS_DEVICES_PATH "/%s/name"
#define SYSFS_TRIGGER_SYSFS_ADD_TRIGGER SYSFS_DEVICES_PATH "/iio_sysfs_trigger/add_trigger"
#define SYSFS_TRIGGER_SYSFS_PATH SYSFS_DEVICES_PATH "/iio_sysfs_trigger"

#define CHANNEL_RAW_PATH SYSFS_DEVICES_PATH "/iio:device%d/%s_raw"
#define CHANNEL_OFFSET_PATH SYSFS_DEVICES_PATH "/iio:device%d/%s_offset"
#define CHANNEL_SCALE_PATH SYSFS_DEVICES_PATH "/iio:device%d/%s_scale"

#define CHANNEL_SCAN_ENABLE_PATH SYSFS_DEVICES_PATH "/iio:device%d/scan_elements/%s_en"
#define CHANNEL_SCAN_INDEX_PATH SYSFS_DEVICES_PATH "/iio:device%d/scan_elements/%s_index"
#define CHANNEL_SCAN_TYPE_PATH SYSFS_DEVICES_PATH "/iio:device%d/scan_elements/%s_type"

#define SAMPLING_FREQUENCY_DEVICE_PATH SYSFS_DEVICES_PATH "/iio:device%d/sampling_frequency"
#define SAMPLING_FREQUENCY_BUFFER_PATH SYSFS_DEVICES_PATH "/iio:device%d/buffer/sampling_frequency"
#define SAMPLING_FREQUENCY_TRIGGER_PATH SYSFS_DEVICES_PATH "/trigger%d/sampling_frequency"

static bool
craft_filename_path(char *path, size_t size, const char *base, ...)
{
    va_list args;
    int len;

    va_start(args, base);
    len = vsnprintf(path, size, base, args);
    va_end(args);

    return len >= 0 && (size_t)len < size;
}

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
                device->manual_triggering = check_file_existence(
                    SYSFS_TRIGGER_NAME_PATH, ent->d_name);
                /* triggers dirs are of the form triggerX, so here we save X */
                device->trigger_id = atoi(ent->d_name + strlen("trigger"));
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
    int len, id, i;
    char *trigger_name;
    bool result = false;

    /* TODO check available trigger indice instead of using a random one */
    /* Create new trigger */
    id = rand();
    i = sol_util_write_file(SYSFS_TRIGGER_SYSFS_ADD_TRIGGER, "%d", id);
    if (i < 0) {
        if (i == -ENOENT) {
            SOL_WRN("No 'iio_sysfs_tigger' under '/sys/bus/iio/devices'."
                " Missing 'modprobe iio-trig-sysfs'?");
            return false;
        }

        goto error;
    }

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
check_trigger(struct sol_iio_device *device)
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
        SOL_INF("No current trigger for iio:device%d. Creating a sysfs one.",
            device->device_id);
        if (!create_sysfs_trigger(device))
            return false;
    } else
        device->trigger_name = name;

    return check_manual_triggering(device);
}

static void
set_buffer_size(struct sol_iio_device *device, int buffer_size)
{
    char path[PATH_MAX];

    if (craft_filename_path(path, sizeof(path), BUFFER_LENGHT_DEVICE_PATH, device->device_id)) {
        SOL_WRN("Could not set IIO device buffer size");
        return;
    }

    if (sol_util_write_file(path, "%d", buffer_size) < 0) {
        SOL_WRN("Could not set IIO device buffer size");
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
device_reader_cb(void *data, int fd, unsigned int active_flags)
{
    struct sol_iio_device *device = data;
    bool result = true;
    ssize_t ret;

    if (active_flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL)) {
        SOL_WRN("Unexpected reading");
        result = false;
    }

    sol_buffer_reset(&device->buffer);
    ret = sol_util_fill_buffer(fd, &device->buffer, device->buffer_size);
    if (ret <= 0) {
        result = false;
    } else if (device->buffer.used == device->buffer_size) {
        if (device->reader_cb) {
            device->reader_cb((void *)device->reader_cb_data, device);
        }
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
 * The form <type>[Y][_modifier] is also common (Y is a number), and this function
 * remove numbers too, in an attempt to get the 'pure' name.
 * TODO there are other esoteric combinations - for them, if we care about,
 * we will probably need API calls to get the 'scale' and 'offset' file names */
static char *
channel_get_pure_name(struct sol_iio_channel *channel)
{
    size_t channel_name_len;
    char *channel_pure_name;
    bool modified = false;

    channel_name_len = strlen(channel->name);
    if (channel_name_len > 2) {
        char *channel_name_suffix;

        channel_name_suffix = channel->name + channel_name_len - 2;
        if (streq(channel_name_suffix,  "_x") ||
            streq(channel_name_suffix,  "_y") || streq(channel_name_suffix,  "_z")) {

            channel_pure_name = strndup(channel->name, channel_name_len - 2);
            return channel_pure_name;
        } else {
            /* Recreate channel name without Y_ components (Y is a number).
             * Idea is 's/[0-9]+//' */
            size_t i;
            char *original_channel_pure_name;

            channel_pure_name = calloc(1, channel_name_len + 1);
            original_channel_pure_name = channel_pure_name;
            for (i = 0; i < channel_name_len; i++) {
                if (isalpha(channel->name[i]) || channel->name[i] == '-' || channel->name[i] == '_')
                    *channel_pure_name++ = channel->name[i];
                else if (isdigit(channel->name[i])) {
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

    return NULL;
}

static bool
iio_set_sampling_frequency(struct sol_iio_device *device, int frequency)
{
    char path[PATH_MAX];

    SOL_NULL_CHECK(device, false);

    if (craft_filename_path(path, sizeof(path), SAMPLING_FREQUENCY_DEVICE_PATH,
        device->device_id)) {

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

        if (sol_util_write_file(path, "%lf", scale) > 0) {
            channel->scale = scale;
            return true;
        }
    }

    /* If failed, try channel pure name */
    pure_name = channel_get_pure_name(channel);
    if (pure_name && craft_filename_path(path, sizeof(path), CHANNEL_SCALE_PATH,
        channel->device->device_id, channel->name)) {

        result = (sol_util_write_file(path, "%lf", scale) > 0);
        if (result)
            channel->scale = scale;
    }

    if (!result)
        SOL_WRN("Could not set scale to %lf on channel [%s] of device%d",
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
    pure_name = channel_get_pure_name(channel);
    if (pure_name && craft_filename_path(path, sizeof(path), CHANNEL_OFFSET_PATH,
        channel->device->device_id, channel->name)) {

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

struct sol_iio_device *
sol_iio_open(int device_id, const struct sol_iio_config *config)
{
    bool r;
    char path[PATH_MAX];
    struct sol_iio_device *device = NULL;

    SOL_NULL_CHECK(config, NULL);
    if (unlikely(config->api_version != SOL_IIO_CONFIG_API_VERSION)) {
        SOL_WRN("IIO config version '%u' is unexpected, expected '%u'",
            config->api_version, SOL_IIO_CONFIG_API_VERSION);
        return NULL;
    }

    device = calloc(1, sizeof(struct sol_iio_device));
    SOL_NULL_CHECK(device, NULL);
    sol_ptr_vector_init(&device->channels);
    device->buffer = SOL_BUFFER_INIT_EMPTY;
    device->fd = -1;

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

    if (config->buffer_size > -1) {
        if (!config->sol_iio_reader_cb) {
            SOL_WRN("Buffer is enabled for device%d but no 'sol_iio_reader_cb'"
                " was defined.'", device->device_id);
            goto error;
        }
        device->reader_cb = config->sol_iio_reader_cb;
        device->reader_cb_data = config->data;

        if (config->trigger_name && config->trigger_name[0] != '\0') {
            if (!set_current_trigger(device, config->trigger_name)) {
                SOL_WRN("Could not set device%d current trigger",
                    device->device_id);
                goto error;
            }
        }

        if (!check_trigger(device)) {
            SOL_WRN("No trigger available for device%d", device->device_id);
            goto error;
        }

        if (config->buffer_size != 0)
            set_buffer_size(device, config->buffer_size);

        if (!device->manual_triggering) {
            SOL_WRN("No 'trigger_now' file on device%d current trigger. "
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
        if (!set_buffer_enabled(device, false)) {
            SOL_WRN("Could not disable buffer for device%d", device->device_id);
            goto error;
        }
    }

    if (config->sampling_frequency > -1) {
        if (!iio_set_sampling_frequency(device, config->sampling_frequency))
            SOL_WRN("Could not set device%d sampling frequency", device->device_id);
    }

    SOL_DBG("iio device created. device%d - buffer_enabled: %d - manual_trigger: %d"
        " - trigger_name: %s - trigger_id: %d", device->device_id,
        device->buffer_enabled, device->manual_triggering, device->trigger_name,
        device->trigger_id);

    return device;

error:
    sol_iio_close(device);

    return NULL;
}

static bool
enable_channel_scan(struct sol_iio_channel *channel)
{
    struct sol_iio_device *device = channel->device;
    char path[PATH_MAX];
    int current_value;

    if (!craft_filename_path(path, sizeof(path), CHANNEL_SCAN_ENABLE_PATH,
        device->device_id, channel->name)) {

        return false;
    }

    /* First, check if not already enabled */
    sol_util_read_file(path, "%d", &current_value);
    if (current_value != 1)
        return sol_util_write_file(path, "%d", 1) > 0;

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

static void
channel_get_scale(struct sol_iio_channel *channel)
{
    char path[PATH_MAX];
    char *channel_pure_name = NULL;
    struct sol_iio_device *device = channel->device;
    bool r;

#define GET_SCALE(_name) \
    do { \
        r = craft_filename_path(path, sizeof(path), CHANNEL_SCALE_PATH, \
            device->device_id, _name); \
        if (r && sol_util_read_file(path, "%lf", &channel->scale) > 0) { \
            goto end; \
        } \
    } while (0)

    GET_SCALE(channel->name);
    /* No scale. If channel has x, y, z or Y component, look for scale file without it */
    channel_pure_name = channel_get_pure_name(channel);
    GET_SCALE(channel_pure_name);

    SOL_INF("Could not get scale for channel [%s] in device%d. Assuming 1.0",
        channel->name, device->device_id);
    channel->scale = 1.0;

#undef GET_SCALE

end:
    free(channel_pure_name);
}

static void
channel_get_offset(struct sol_iio_channel *channel)
{
    char path[PATH_MAX];
    char *channel_pure_name = NULL;
    struct sol_iio_device *device = channel->device;
    bool r;

#define GET_OFFSET(_name) \
    do { \
        r = craft_filename_path(path, sizeof(path), CHANNEL_OFFSET_PATH, \
            device->device_id, _name); \
        if (r && sol_util_read_file(path, "%d", &channel->offset) > 0) { \
            goto end; \
        } \
    } while (0)

    GET_OFFSET(channel->name);
    /* No scale. If channel has x, y or z component, look for scale file without it */
    channel_pure_name = channel_get_pure_name(channel);
    GET_OFFSET(channel_pure_name);

    SOL_INF("Could not get offset for channel [%s] in device%d. Assuming 0",
        channel->name, device->device_id);
    channel->offset = 0;

#undef GET_OFFSET

end:
    free(channel_pure_name);
}

static void
iio_del_channel(struct sol_iio_channel *channel)
{
    free(channel);
}

void
sol_iio_close(struct sol_iio_device *device)
{
    int i;
    struct sol_iio_channel *channel;

    SOL_NULL_CHECK(device);

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&device->channels, channel, i) {
        iio_del_channel(channel);
    }
    sol_ptr_vector_clear(&device->channels);

    if (device->fd_handler) sol_fd_del(device->fd_handler);
    if (device->fd > -1) close(device->fd);
    if (device->name_fd > -1) close(device->name_fd);

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

struct sol_iio_channel *
sol_iio_add_channel(struct sol_iio_device *device, const char *name, const struct sol_iio_channel_config *config)
{
    struct sol_iio_channel *channel;

    SOL_NULL_CHECK(device, NULL);
    SOL_NULL_CHECK(name, NULL);
    SOL_NULL_CHECK(config, NULL);

    if (unlikely(config->api_version != SOL_IIO_CHANNEL_CONFIG_API_VERSION)) {
        SOL_WRN("IIO channel config version '%u' is unexpected, expected '%u'",
            config->api_version, SOL_IIO_CHANNEL_CONFIG_API_VERSION);
        return NULL;
    }

    if (!check_file_existence(CHANNEL_RAW_PATH, device->device_id, name)) {
        SOL_WRN("Could not find channel [%s] for device%d", name,
            device->device_id);
        return NULL;
    }

    channel = calloc(1, sizeof(struct sol_iio_channel) + strlen(name) + 1);
    SOL_NULL_CHECK(channel, NULL);
    memcpy(channel->name, name, strlen(name));

    channel->device = device;

    if (config->scale > -1)
        iio_set_channel_scale(channel, config->scale);
    else
        channel_get_scale(channel);

    if (config->use_custom_offset)
        iio_set_channel_offset(channel, config->offset);
    else
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

        channel->mask = (1 << channel->bits) - 1;
        channel->offset_in_buffer = -1;
    }

    sol_ptr_vector_append(&channel->device->channels, channel);

    if (channel->storagebits > 64) {
        // XXX is such a thing even possible?
        SOL_WRN("Could not add channel [%s] - more than 64 bits of storage, found %d",
            channel->name, channel->storagebits);
        return false;
    }

    SOL_DBG("channel [%s] added. scale: %lf - offset: %d - storagebits: %d"
        " - bits: %d - mask: %" PRIu64, channel->name, channel->scale,
        channel->offset, channel->storagebits, channel->bits, channel->mask);

    return channel;

error:
    iio_del_channel(channel);

    return NULL;
}

static bool
iio_read_buffer_channel_value(struct sol_iio_channel *channel, double *value)
{
    uint64_t data = 0;
    int64_t s_data;
    unsigned j, offset_bytes;
    int i, storage_bytes;
    bool negative = false;
    struct sol_iio_device *device = channel->device;
    uint8_t *buffer = device->buffer.data;

    SOL_NULL_CHECK(buffer, false);

    if (channel->offset_in_buffer + channel->storagebits > device->buffer_size * 8) {
        SOL_WRN("Invalid read on buffer.");
        return false;
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
        *value = (s_data + channel->offset) * channel->scale;
    } else
        *value = (data + channel->offset) * channel->scale;

    return true;
}

bool
sol_iio_read_channel_value(struct sol_iio_channel *channel, double *value)
{
    int len, raw_value;
    char path[PATH_MAX];
    struct sol_iio_device *device = channel->device;
    bool r;

    SOL_NULL_CHECK(channel, false);
    SOL_NULL_CHECK(value, false);

    if (device->buffer_enabled) {
        return iio_read_buffer_channel_value(channel, value);
    }

    r = craft_filename_path(path, sizeof(path), CHANNEL_RAW_PATH,
        device->device_id, channel->name);
    if (!r) {
        SOL_WRN("Could not read channel [%s] in device%d", channel->name,
            device->device_id);
        return false;
    }

    len = sol_util_read_file(path, "%d", &raw_value);
    if (len < 0) {
        SOL_WRN("Could not read channel [%s] in device%d", channel->name,
            device->device_id);
        return false;
    }

    *value = (raw_value + channel->offset) * channel->scale;
    return true;
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

bool
sol_iio_device_trigger_now(struct sol_iio_device *device)
{
    char path[PATH_MAX];
    bool r;

    SOL_NULL_CHECK(device, false);

    if (!device->manual_triggering) {
        SOL_WRN("No manual triggering available for device%d", device->device_id);
        return false;
    }

    r = craft_filename_path(path, sizeof(path), SYSFS_TRIGGER_NOW_BY_ID_PATH,
        device->trigger_id);
    if (!r) {
        SOL_WRN("No valid trigger_now file available for trigger [%s]",
            device->trigger_name);
        return false;
    }

    if (sol_util_write_file(path, "%d", 1) < 0) {
        SOL_WRN("Could not write to trigger_now file for trigger [%s]",
            device->trigger_name);
        return false;
    }

    return true;
}

bool
sol_iio_device_start_buffer(struct sol_iio_device *device)
{
    struct sol_iio_channel *channel;
    int i;

    SOL_NULL_CHECK(device, false);

    /* Enable device after added all channels */
    if (device->buffer_enabled && !set_buffer_enabled(device, true)) {
        SOL_WRN("Could not enable buffer for device. No readings will be performed");
        return false;
    }

    device->buffer_size = calc_buffer_size(device);
    i = sol_buffer_ensure(&device->buffer, device->buffer_size);
    if (i < 0) {
        SOL_WRN("Could not alloc buffer for device. No readings will be performed");
        return false;
    }

    /* Now that all channels have been added, calc their offset in buffer */
    SOL_PTR_VECTOR_FOREACH_IDX (&device->channels, channel, i) {
        channel->offset_in_buffer = calc_channel_offset_in_buffer(channel);
    }

    return true;
}
