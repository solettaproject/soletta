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

#include <stdio.h>
#include <string.h>
#include "sol-mainloop.h"
#include "sol-uart.h"
#include "sol-util.h"
#include "sol-str-slice.h"
#include "soletta.h"

/**
 * @file
 * @brief UART sample
 *
 * This sample simulates an UART producer and consumer.
 * In order to run this sample, you will need to cross connect two uart cables.
 * This means the TX from one cable must be connect to the RX of the another cable
 * and vice-versa.
 */

#define FEED_SIZE (512)
#define MAX_PACKETS (100)

static struct sol_uart *producer;
static struct sol_uart *consumer;
static struct sol_timeout *producer_timeout;
static struct sol_blob *pending_blob;

static bool producer_make_data(void *data);

static bool send_blob(struct sol_blob *blob);

//! [uart write completed]
static void
producer_data_written(void *data, struct sol_uart *uart, struct sol_blob *blob, int status)
{
    struct sol_str_slice slice;

    slice = sol_str_slice_from_blob(blob);

    if (status < 0) {
        fprintf(stderr, "Could not write the UUID %.*s - Reason: %s\n", SOL_STR_SLICE_PRINT(slice),
            sol_util_strerrora(-status));
        sol_quit();
    } else {
        printf("Producer: UUID %.*s written\n", SOL_STR_SLICE_PRINT(slice));
        if (pending_blob) { //If we have a pending blob now it's the time to try to send it!
            if (!send_blob(pending_blob)) {
                fprintf(stderr, "Could not send the pending blob!\n");
                sol_quit();
            }
        }
    }
}
//! [uart write completed]

//! [uart write]
static bool
send_blob(struct sol_blob *blob)
{
    int err;
    bool r = true;

    //Actually write the blob into UART bus
    err = sol_uart_feed(producer, blob);
    if (err < 0) {
        //Oh no, there's no more space left.
        if (err == -ENOSPC) {
            pending_blob = blob;
            printf("No space left in the tx buffer - saving blob for later. Data: %.*s\n",
                SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(pending_blob)));
        } else {
            fprintf(stderr, "Could not not perform an UART write - Reason: %s\n",
                sol_util_strerrora(-r));
            r = false;
            sol_blob_unref(blob);
        }
    } else {
        sol_blob_unref(blob);
        if (blob == pending_blob)
            pending_blob = NULL; //Flag that data production can start once again!
    }

    return r;
}

static bool
producer_make_data(void *data)
{
    void *v;
    size_t size;
    struct sol_blob *blob;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    static uint16_t packets_created = 0;
    bool keep_running = true;
    int r;

    //Stop the production until the pendind blob is sent
    if (pending_blob) {
        printf("Waiting for blob data: %.*s to be transferred.\n",
            SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(pending_blob)));
        return true;
    }

    packets_created++;

    //Generate data
    if (packets_created != MAX_PACKETS)
        r = sol_util_uuid_gen(true, true, &buf);
    else {
        r = sol_buffer_append_slice(&buf, sol_str_slice_from_str("close"));
        keep_running = false;
    }

    if (r < 0) {
        fprintf(stderr, "Could not create the UUID - Reason: %s\n",
            sol_util_strerrora(-r));
        goto err_exit;
    }

    v = sol_buffer_steal(&buf, &size);

    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL,
        v, size + 1);

    if (!blob) {
        fprintf(stderr, "Could not alloc memory for the blob\n");
        goto err_exit;
    }

    //Send it
    if (!send_blob(blob))
        goto err_exit;

    if (!keep_running)
        goto exit;
    return true;

err_exit:
    sol_quit();
exit:
    producer_timeout = NULL;
    return false;
}

//! [uart write]

//! [uart read]
static ssize_t
consumer_read_available(void *data, struct sol_uart *uart, const struct sol_buffer *buf)
{
    struct sol_str_slice slice = sol_buffer_get_slice(buf);
    char *sep;

    sep = memchr(slice.data, '\0', slice.len);

    if (!sep)
        return 0; //Bytes will not be removed fom the rx buffer

    //Close the UART!
    if (sol_str_slice_str_contains(slice, "close")) {
        sol_uart_close(uart); //This is completely safe
        consumer = NULL;
        printf("\n\n** Consumer **: Received the close command\n\n");
        sol_quit();
    } else {
        printf("\n\n** Consumer ** : Received UUID %.*s\n\n",
            SOL_STR_SLICE_PRINT(slice));
    }

    return slice.len; //slice.len bytes will be removed from the rx buffer
}
//! [uart read]

//! [uart configure]
static void
startup(void)
{
    struct sol_uart_config producer_config = {
        SOL_SET_API_VERSION(.api_version = SOL_UART_CONFIG_API_VERSION, )
        .baud_rate = SOL_UART_BAUD_RATE_9600,
        .data_bits = SOL_UART_DATA_BITS_8,
        .parity = SOL_UART_PARITY_NONE,
        .stop_bits = SOL_UART_STOP_BITS_ONE,
        .on_feed_done = producer_data_written,
        .feed_size = FEED_SIZE //Note that feed buffer is limited!
    };
    struct sol_uart_config consumer_config = {
        SOL_SET_API_VERSION(.api_version = SOL_UART_CONFIG_API_VERSION, )
        .baud_rate = SOL_UART_BAUD_RATE_9600,
        .data_bits = SOL_UART_DATA_BITS_8,
        .parity = SOL_UART_PARITY_NONE,
        .stop_bits = SOL_UART_STOP_BITS_ONE,
        .on_data = consumer_read_available,
    };

    char **argv;
    int argc;

    argc = sol_argc();
    argv = sol_argv();

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <producerUART> <consumerUART>\n", argv[0]);
        goto err_exit;
    }

    producer = sol_uart_open(argv[1], &producer_config);
    if (!producer) {
        fprintf(stderr, "Could not create the producer!\n");
        goto err_exit;
    }

    consumer = sol_uart_open(argv[2], &consumer_config);
    if (!consumer) {
        fprintf(stderr, "Could not create the consumer\n");
        goto err_exit;
    }

    //This will produce all the data to be sent
    producer_timeout = sol_timeout_add(10, producer_make_data, NULL);
    if (!producer_timeout) {
        fprintf(stderr, "Could not create the producer timeout!\n");
        goto err_exit;
    }

    return;
err_exit:
    sol_quit();
}
//! [uart configure]

static void
shutdown(void)
{
    if (producer)
        sol_uart_close(producer);
    if (consumer)
        sol_uart_close(consumer);
    if (producer_timeout)
        sol_timeout_del(producer_timeout);
    if (pending_blob)
        sol_blob_unref(pending_blob);
}

SOL_MAIN_DEFAULT(startup, shutdown);
