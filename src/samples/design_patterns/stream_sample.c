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

#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-types.h>
#include <sol-buffer.h>
#include <sol-util.h>
#include <sol-reentrant.h>
#include <soletta.h>
#include <unistd.h>
#include <stdio.h>

#define my_stream_device_write write
#define my_stream_device_read read
#define my_stream_device_add_io_monitor sol_fd_add
#define my_stream_device_remove_io_monitor sol_fd_del
#define my_stream_device_monitor_handle struct sol_fd

struct my_stream_api_handle;

//! [stream config]
struct my_stream_api_config {
    const void *user_data;
    void (*on_feed_done)(void *user_data, struct my_stream_api_handle *handle, struct sol_blob *blob, int status);
    ssize_t (*on_data)(void *user_data, struct my_stream_api_handle *handle, const struct sol_buffer *buf);
    size_t feed_size;
    size_t data_buffer_size;
};

struct my_stream_api_handle *my_stream_api_new(const struct my_stream_api_config *config, int dev);
//! [stream config]

//! [stream handle]
struct my_stream_api_handle {
    const void *user_data;
    void (*on_feed_done)(void *user_data, struct my_stream_api_handle *handle, struct sol_blob *blob, int status);
    ssize_t (*on_data)(void *user_data, struct my_stream_api_handle *handle, const struct sol_buffer *buf);
    struct sol_timeout *read_timeout;
    struct sol_buffer rx;
    struct sol_ptr_vector pending_blobs;
    my_stream_device_monitor_handle *read_monitor;
    my_stream_device_monitor_handle *write_monitor;
    size_t feed_size;
    size_t pending_bytes;
    size_t written;
    int dev;
    struct sol_reentrant reentrant;
};
//! [stream handle]

static void api_close(struct my_stream_api_handle *handle);

#define DEFAULT_BUFFER_SIZE (4096)

//! [stream write api]
int my_stream_api_feed(struct my_stream_api_handle *handle, struct sol_blob *blob);
//! [stream write api]
void my_stream_api_close(struct my_stream_api_handle *handle);

//! [stream write]

//The write operation itself
static bool
_can_write(void *data, int fd, uint32_t active_flags)
{
    struct my_stream_api_handle *handle = data;
    struct sol_blob *blob;
    ssize_t status;
    bool r = true;

    //Write the blob
    blob = sol_ptr_vector_get(&handle->pending_blobs, 0);

    //Write into the stream
    status = my_stream_device_write(handle->dev, (char *)blob->mem + handle->written, blob->size);

    if (status < 0) {
        if (status == EAGAIN || status == EINTR)
            return true;
        else {
            SOL_WRN("Could not write to the stream device!");
            handle->write_monitor = NULL;
            return false;
        }
    }

    //Update how many bytes have been written
    handle->written += status;
    handle->pending_bytes -= status;

    //Blob was completly written. Inform the user.
    if (handle->written == blob->size) {
        //Do we still have more data ?
        sol_ptr_vector_del(&handle->pending_blobs, 0);
        if (!sol_ptr_vector_get_len(&handle->pending_blobs)) {
            r = false;
            handle->write_monitor = NULL;
        }
        //Reset the written counter
        handle->written = 0;

        /*
           Inform the user.
           Since it's completly safe to call my_stream_api_close() inside on_feed_done().
           Informing the user should be the last thing to do.
         */
        if (handle->on_feed_done)
            handle->on_feed_done((void *)handle->user_data, handle, blob, status);
        sol_blob_unref(blob); //NOTE: that we unref the blob, not the user!
    }

    return r;
}

int
my_stream_api_feed(struct my_stream_api_handle *handle, struct sol_blob *blob)
{
    size_t total;
    int r;

    SOL_NULL_CHECK(handle, -EINVAL);
    SOL_NULL_CHECK(blob, -EINVAL);
    //Do not try to write with the uart is going to be closed
    SOL_EXP_CHECK(handle->reentrant.delete_me, -EINVAL);

    total = handle->pending_bytes + blob->size;

    //feed_size was set and the total must not exceed feed_size
    if (handle->feed_size && total >= handle->feed_size)
        return -ENOSPC; //Try again later

    //Store the blob and ref it, since it will written later.
    r = sol_ptr_vector_append(&handle->pending_blobs, blob);
    SOL_INT_CHECK(r, < 0, r);
    sol_blob_ref(blob);
    handle->pending_bytes = total;

    //Schedule the write operation
    if (!handle->write_monitor) {
        handle->write_monitor = my_stream_device_add_io_monitor(handle->dev,
            SOL_FD_FLAGS_OUT, _can_write, handle);
        SOL_NULL_CHECK_GOTO(handle->write_monitor, err_monitor);
    }

    return 0;

err_monitor:
    sol_ptr_vector_del_element(&handle->pending_blobs, blob);
    sol_blob_unref(blob);
    return -ENOMEM;
}

//! [stream write]

//! [stream read]
//Delivery the data to the user
static bool
_inform_user(void *data)
{
    struct my_stream_api_handle *handle = data;
    ssize_t r;

    //Flag that the handle is in use
    SOL_REENTRANT_CALL(handle->reentrant) {
        //Inform the user
        r = handle->on_data((void *)handle->user_data, handle, &handle->rx);
    }

    //Remove the data.
    if (r < 0)
        SOL_ERR("Something wrong happened %zd", r);
    else
        sol_buffer_remove_data(&handle->rx, 0, r);

    //Close the handle
    if (handle->reentrant.delete_me) {
        SOL_REENTRANT_FREE(handle->reentrant) {
            api_close(handle);
        }
        handle->read_timeout = NULL;
        return false;
    }

    //Still need to callback the user with remaining data, keep the timer running
    if (handle->rx.used)
        return true;
    handle->read_timeout = NULL;
    return false;
}

//Actually read from the device
static bool
_can_read(void *data, int fd, uint32_t active_flags)
{
    struct my_stream_api_handle *handle = data;
    size_t remaining = handle->rx.capacity - handle->rx.used;
    ssize_t status;

    //The rx buffer is full. Try to expand it in order to store more data.
    if (!remaining && !(handle->rx.flags & SOL_BUFFER_FLAGS_FIXED_CAPACITY)) {
        int err;

        err = sol_buffer_expand(&handle->rx, DEFAULT_BUFFER_SIZE);
        SOL_INT_CHECK(err, < 0, true);
        remaining = DEFAULT_BUFFER_SIZE;
    }

    if (remaining > 0) {
        //Append data to the buffer
        status = my_stream_device_read(handle->dev, sol_buffer_at_end(&handle->rx), remaining);
        if (status < 0) {
            if (status == EAGAIN || status == EINTR)
                return true;
            else {
                SOL_WRN("Could not read to the stream device!");
                handle->read_monitor = NULL;
                return false;
            }
        }
        handle->rx.used += status;
    }

    if (!handle->read_timeout)
        handle->read_timeout = sol_timeout_add(0, _inform_user, handle);

    return true;
}

//Stream creation, where on_data is configured
struct my_stream_api_handle *
my_stream_api_new(const struct my_stream_api_config *config, int dev)
{
    struct my_stream_api_handle *handle;
    size_t data_buffer_size = 0;
    void *buf = NULL;
    //By default the rx buffer will not be limited
    enum sol_buffer_flags flags = SOL_BUFFER_FLAGS_NO_NUL_BYTE | SOL_BUFFER_FLAGS_DEFAULT;

    handle = calloc(1, sizeof(struct my_stream_api_handle));
    SOL_NULL_CHECK(handle, NULL);

    handle->on_feed_done = config->on_feed_done;
    handle->feed_size = config->feed_size;
    handle->user_data = config->user_data;

    //The user does not want to read from the stream, ignore rx configuration.
    if (config->on_data) {
        handle->on_data = config->on_data;
        data_buffer_size = config->data_buffer_size;

        //The rx buffer has a fixed size and we should respect that.
        if (data_buffer_size) {
            buf = malloc(data_buffer_size);
            SOL_NULL_CHECK_GOTO(buf, err_buf);
            flags |= SOL_BUFFER_FLAGS_FIXED_CAPACITY;
        } //else - The user is informing that the rx buffer should be unlimited

        //Setup input monitor
        handle->read_monitor = my_stream_device_add_io_monitor(handle->dev, SOL_FD_FLAGS_IN, _can_read, handle);
        SOL_NULL_CHECK_GOTO(handle->read_monitor, err_monitor);
    }

    sol_buffer_init_flags(&handle->rx, buf, data_buffer_size, flags);
    sol_ptr_vector_init(&handle->pending_blobs);
    handle->dev = dev;

    return handle;

err_monitor:
    free(buf);
err_buf:
    free(handle);
    return NULL;
}

static void
api_close(struct my_stream_api_handle *handle)
{
    uint16_t i;
    struct sol_blob *blob;

    //Inform that some blobs where not sent
    SOL_PTR_VECTOR_FOREACH_IDX (&handle->pending_blobs, blob, i) {
        if (handle->on_feed_done)
            handle->on_feed_done((void *)handle->user_data, handle, blob, -ECANCELED);
        sol_blob_unref(blob);
    }

    //Last chance to consume the rx buffer
    if (handle->rx.used)
        handle->on_data((void *)handle->user_data, handle, &handle->rx);

    sol_ptr_vector_clear(&handle->pending_blobs);
    sol_buffer_fini(&handle->rx);
    free(handle);
}

void
my_stream_api_close(struct my_stream_api_handle *handle)
{
    SOL_NULL_CHECK(handle);
    SOL_EXP_CHECK(handle->reentrant.delete_me);

    if (handle->read_timeout) {
        sol_timeout_del(handle->read_timeout);
        handle->read_timeout = NULL;
    }

    if (handle->read_monitor) {
        my_stream_device_remove_io_monitor(handle->read_monitor);
        handle->read_monitor = NULL;
    }

    if (handle->write_monitor) {
        my_stream_device_remove_io_monitor(handle->write_monitor);
        handle->write_monitor = NULL;
    }

    //The user is trying to delete the handle from the on_data, do not delete it now.
    SOL_REENTRANT_FREE(handle->reentrant) {
        api_close(handle);
    }
}

//! [stream read]

static void
on_feed_done(void *data, struct my_stream_api_handle *handle, struct sol_blob *blob, int status)
{
    struct sol_str_slice slice = sol_str_slice_from_blob(blob);

    if (status  < 0)
        fprintf(stderr, "Could not send the blob data: %.*s. Reason: %s",
            SOL_STR_SLICE_PRINT(slice), sol_util_strerrora(-status));
    else
        printf("\n'%.*s' Sent to stdout\n", SOL_STR_SLICE_PRINT(slice));
}

static struct my_stream_api_handle *out_handle;

static ssize_t
on_data(void *user_data, struct my_stream_api_handle *handle, const struct sol_buffer *buf)
{
    struct sol_str_slice slice = sol_buffer_get_slice(buf);
    struct sol_blob *blob;
    char *sep;

    sep = memchr(slice.data, '\n', slice.len);

    if (!sep)
        return 0;

    slice.len = sep - slice.data;

    printf("Received data '%.*s'. Sending to stdout.\n", SOL_STR_SLICE_PRINT(slice));

    if (sol_str_slice_str_eq(slice, "Bye")) {
        my_stream_api_close(handle);
        printf("Closing the input stream\n");
        sol_quit();
    }

    blob = sol_blob_new(&SOL_BLOB_TYPE_NO_FREE_DATA, NULL, slice.data, slice.len);
    if (!blob) {
        fprintf(stderr, "Could not create a blob to send to stdout");
        return -ENOMEM;
    }
    my_stream_api_feed(out_handle, blob);
    sol_blob_unref(blob);


    return slice.len + 1;
}

static void
startup(void)
{
    struct my_stream_api_handle *in_handle;
    static const struct my_stream_api_config in_config = {
        .on_data = on_data,
    };
    static const struct my_stream_api_config out_config = {
        .on_feed_done = on_feed_done,
    };

    in_handle = my_stream_api_new(&in_config, STDIN_FILENO);

    if (!in_handle) {
        fprintf(stderr, "Could not create the input stream\n");
        goto err_in;
    }

    out_handle = my_stream_api_new(&out_config, STDOUT_FILENO);
    if (!out_handle) {
        fprintf(stderr, "Could not create the output stream\n");
        goto err_out;
    }

    printf("Type some text and press enter.\n");

    return;
err_out:
    my_stream_api_close(in_handle);
err_in:
    sol_quit_with_code(EXIT_FAILURE);
}

static void
shutdown(void)
{
}


SOL_MAIN_DEFAULT(startup, shutdown);
