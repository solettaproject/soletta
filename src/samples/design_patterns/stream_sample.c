#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-types.h>
#include <sol-buffer.h>
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
    void (*tx_cb)(void *user_data, struct my_stream_api_handle *handle, struct sol_blob *blob, int status);
    ssize_t (*rx_cb)(void *user_data, struct my_stream_api_handle *handle, const struct sol_buffer *buf);
    size_t tx_size;
    size_t rx_size;
};

struct my_stream_api_handle *my_stream_api_new(const struct my_stream_api_config *config, int dev);
//! [stream config]

//! [stream handle]
struct my_stream_api_handle {
    const void *user_data;
    void (*tx_cb)(void *user_data, struct my_stream_api_handle *handle, struct sol_blob *blob, int status);
    ssize_t (*rx_cb)(void *user_data, struct my_stream_api_handle *handle, const struct sol_buffer *buf);
    struct sol_timeout *read_timeout;
    struct sol_buffer rx;
    struct sol_ptr_vector pending_blobs;
    my_stream_device_monitor_handle *read_monitor;
    my_stream_device_monitor_handle *write_monitor;
    size_t tx_size;
    size_t pending_bytes;
    size_t written;
    int dev;
    bool in_use;
    bool delete_me;
};
//! [stream handle]

//! [stream write api]
int my_stream_api_write(struct my_stream_api_handle *handle, struct sol_blob *blob);
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

        /*
           Inform the user.
           Since it's completly safe to call my_stream_api_close() inside tx_cb().
           Informing the user should be the last thing to do.
         */
        if (handle->tx_cb)
            handle->tx_cb((void *)handle->user_data, handle, blob, status);
        sol_blob_unref(blob); //NOTE: that we unref the blob, not the user!
    }

    return r;
}

int
my_stream_api_write(struct my_stream_api_handle *handle, struct sol_blob *blob)
{
    size_t total = handle->pending_bytes + blob->size;
    int r;

    //Tx_size was set and the total must not exceed tx_size
    if (handle->tx_size && total >= handle->tx_size)
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

    //Flag that we're using the handle, then if the user tries to delete us from the rx_cb we do not crash.
    handle->in_use = true;

    //Inform the user
    r = handle->rx_cb((void *)handle->user_data, handle, &handle->rx);

    handle->in_use = false;
    //The user asked for deletion. Do it now.
    if (handle->delete_me) {
        handle->read_timeout = NULL;
        my_stream_api_close(handle);
        return false;
    }

    //Remove the data.
    if (r < 0)
        SOL_ERR("Something wrong happened %zd", r);
    else
        sol_buffer_remove_data(&handle->rx, 0, r);

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
    ssize_t status;

    //Append data to the buffer
    status = my_stream_device_read(handle->dev, sol_buffer_at_end(&handle->rx), handle->rx.capacity - handle->rx.used);
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
    //Keep reading until there's no more data
    if (!handle->read_timeout)
        handle->read_timeout = sol_timeout_add(0, _inform_user, handle);

    return true;
}

//Stream creation, where rx_cb is configured
struct my_stream_api_handle *
my_stream_api_new(const struct my_stream_api_config *config, int dev)
{
    struct my_stream_api_handle *handle;
    size_t rx_size;
    void *buf = NULL;
    enum sol_buffer_flags flags = SOL_BUFFER_FLAGS_NO_NUL_BYTE;

    handle = calloc(1, sizeof(struct my_stream_api_handle));
    SOL_NULL_CHECK(handle, NULL);

    handle->tx_cb = config->tx_cb;
    handle->tx_size = config->tx_size;
    handle->user_data = config->user_data;

    //The user does not want to read from the stream, ignore rx configuration.
    if (config->rx_cb) {
        handle->rx_cb = config->rx_cb;
        rx_size = config->rx_size;

        if (rx_size) {
            buf = malloc(rx_size);
            SOL_NULL_CHECK_GOTO(buf, err_buf);
        } else
            flags |= SOL_BUFFER_FLAGS_DEFAULT;

        //Setup input monitor
        handle->read_monitor = my_stream_device_add_io_monitor(handle->dev, SOL_FD_FLAGS_IN, _can_read, handle);
        SOL_NULL_CHECK_GOTO(handle->read_monitor, err_monitor);
        flags |= SOL_BUFFER_FLAGS_FIXED_CAPACITY;
    } else
        flags |= SOL_BUFFER_FLAGS_DEFAULT;

    sol_buffer_init_flags(&handle->rx, buf, rx_size, flags);
    sol_ptr_vector_init(&handle->pending_blobs);
    handle->dev = dev;

    return handle;

err_monitor:
    free(buf);
err_buf:
    free(handle);
    return NULL;
}

void
my_stream_api_close(struct my_stream_api_handle *handle)
{
    uint16_t i;
    struct sol_blob *blob;

    //The user is trying to delete the handle from the rx_cb, do not delete it now.
    if (handle->in_use) {
        handle->delete_me = true;
        return;
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&handle->pending_blobs, blob, i)
        sol_blob_unref(blob);
    sol_ptr_vector_clear(&handle->pending_blobs);
    sol_buffer_fini(&handle->rx);
    if (handle->read_timeout)
        sol_timeout_del(handle->read_timeout);
    if (handle->read_monitor)
        my_stream_device_remove_io_monitor(handle->read_monitor);
    if (handle->write_monitor)
        my_stream_device_remove_io_monitor(handle->write_monitor);
    free(handle);
}

//! [stream read]

static void
startup(void)
{
}

static void
shutdown(void)
{
}


SOL_MAIN_DEFAULT(startup, shutdown);
