/*
 * This file is part of the Soletta Project
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

#define TX_SIZE (512)

static struct sol_uart *producer;
static struct sol_uart *consumer;
static struct sol_timeout *producer_timeout;
static struct sol_blob *pending_blob;

static bool producer_make_data(void *data);

static bool
send_blob(struct sol_blob *blob)
{
    int err;
    bool r = true;

    err = sol_uart_write(producer, blob);
    if (err < 0) {
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
    } else
        sol_blob_unref(blob);
    return r;
}

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
        if (pending_blob && !send_blob(pending_blob)) {
            fprintf(stderr, "Could not send the pending blob!\n");
            sol_quit();
        }
        pending_blob = NULL;
    }
}

static bool
producer_make_data(void *data)
{
    void *v;
    size_t size;
    struct sol_blob *blob;
    struct sol_buffer uuid = SOL_BUFFER_INIT_EMPTY;
    int r;

    if (pending_blob) {
        printf("Waiting for blob data: %.*s to be transfered.\n",
            SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(pending_blob)));
        return true;
    }

    r = sol_util_uuid_gen(true, true, &uuid);

    if (r < 0) {
        fprintf(stderr, "Could not create the UUID - Reason: %s\n",
            sol_util_strerrora(-r));
        goto err_exit;
    }

    v = sol_buffer_steal(&uuid, &size);

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL,
        v, size + 1);

    if (!blob) {
        fprintf(stderr, "Could not alloc memory for the blob\n");
        goto err_exit;
    }

    if (!send_blob(blob))
        goto err_exit;

    return true;

err_exit:
    sol_quit();
    producer_timeout = NULL;
    return false;
}

static ssize_t
consumer_read_available(void *data, struct sol_uart *uart, const struct sol_buffer *buf)
{
    char *sep;

    sep = memchr(buf->data, '\0', buf->used);

    if (!sep)
        return 0;

    printf("\n\n** Consumer ** : Received UUID %.*s\n\n",
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(buf)));
    return strlen(buf->data) + 1;
}

static void
startup(void)
{
    struct sol_uart_config producer_config = {
        SOL_SET_API_VERSION(.api_version = SOL_UART_CONFIG_API_VERSION, )
        .baud_rate = SOL_UART_BAUD_RATE_9600,
        .data_bits = SOL_UART_DATA_BITS_8,
        .parity = SOL_UART_PARITY_NONE,
        .stop_bits = SOL_UART_STOP_BITS_ONE,
        .tx_cb = producer_data_written,
        .tx_size = TX_SIZE
    };
    struct sol_uart_config consumer_config = {
        SOL_SET_API_VERSION(.api_version = SOL_UART_CONFIG_API_VERSION, )
        .baud_rate = SOL_UART_BAUD_RATE_9600,
        .data_bits = SOL_UART_DATA_BITS_8,
        .parity = SOL_UART_PARITY_NONE,
        .stop_bits = SOL_UART_STOP_BITS_ONE,
        .rx_cb = consumer_read_available,
    };

    char **argv;
    int argc;

    argc = sol_argc();
    argv = sol_argv();

    if (argc < 3) {
        fprintf(stderr, "Usage: ./uart-sample <producerUART> <consumerUART>\n");
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

    producer_timeout = sol_timeout_add(10, producer_make_data, NULL);
    if (!producer_timeout) {
        fprintf(stderr, "Could not create the producer timeout!\n");
        goto err_exit;
    }

    return;
err_exit:
    sol_quit();
}

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
