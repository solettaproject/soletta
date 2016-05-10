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

#define UUID_LEN (37)
#define MAX_PENDING_BYTES (512)

static struct sol_uart *producer;
static struct sol_uart *consumer;
static struct sol_timeout *producer_timeout;

static bool producer_make_data(void *data);

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
        int r;
        size_t pending;

        printf("Producer: UUID %.*s written\n", SOL_STR_SLICE_PRINT(slice));

        r = sol_uart_get_pending_write_bytes(uart, &pending);
        if (r < 0) {
            fprintf(stderr, "Could not get the pending bytes amount - Reason: %s\n",
                sol_util_strerrora(-status));
            sol_quit();
        } else if (pending > MAX_PENDING_BYTES && producer_timeout) {
            printf("** Stoping data production - Too many pending bytes: %zu **\n", pending);
            sol_timeout_del(producer_timeout);
            producer_timeout = NULL;
        } else if (pending == 0 && !producer_timeout) {
            printf("** Producer will start to send more data **\n");
            producer_timeout = sol_timeout_add(10, producer_make_data, NULL);
            if (!producer_timeout) {
                fprintf(stderr, "Could not recreate the producer timeout!\n");
                sol_quit();
            }
        }
    }
}

static bool
producer_make_data(void *data)
{
    char *uuid;
    struct sol_blob *blob;
    int r;

    uuid = calloc(UUID_LEN, sizeof(char));
    if (!uuid) {
        fprintf(stderr, "Could not alloc memory to store the UUID\n");
        goto err_exit;
    }

    r = sol_util_uuid_gen(true, true, uuid);

    if (r < 0) {
        fprintf(stderr, "Could not create the UUID - Reason: %s\n",
            sol_util_strerrora(-r));
        goto err_exit;
    }

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL,
        uuid, UUID_LEN);

    if (!blob) {
        fprintf(stderr, "Could not alloc memory for the blob\n");
        goto err_exit;
    }

    r = sol_uart_write(producer, blob);
    sol_blob_unref(blob);

    if (r < 0) {
        fprintf(stderr, "Could not not perform an UART write - Reason: %s\n",
            sol_util_strerrora(-r));
        goto err_exit;
    }

    return true;

err_exit:
    sol_quit();
    producer_timeout = NULL;
    return false;
}

static ssize_t
consumer_read_available(void *data, struct sol_uart *uart, struct sol_buffer *buf)
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
}

SOL_MAIN_DEFAULT(startup, shutdown);
