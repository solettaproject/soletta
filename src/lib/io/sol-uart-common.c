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

#include <stdlib.h>
#include <string.h>

#include "sol-log-internal.h"
#include "sol-str-table.h"
#include "sol-uart.h"
#include "sol-util.h"

SOL_API enum sol_uart_baud_rate
sol_uart_baud_rate_from_str(const char *baud_rate)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("baud-9600", SOL_UART_BAUD_RATE_9600),
        SOL_STR_TABLE_ITEM("baud-19200", SOL_UART_BAUD_RATE_19200),
        SOL_STR_TABLE_ITEM("baud-38400", SOL_UART_BAUD_RATE_38400),
        SOL_STR_TABLE_ITEM("baud-57600", SOL_UART_BAUD_RATE_57600),
        SOL_STR_TABLE_ITEM("baud-115200", SOL_UART_BAUD_RATE_115200),
        { }
    };

    SOL_NULL_CHECK(baud_rate, SOL_UART_BAUD_RATE_115200);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(baud_rate), SOL_UART_BAUD_RATE_115200);
}

SOL_API const char *
sol_uart_baud_rate_to_str(enum sol_uart_baud_rate baud_rate)
{
    static const char *baud_rate_names[] = {
        [SOL_UART_BAUD_RATE_9600] = "baud-9600",
        [SOL_UART_BAUD_RATE_19200] = "baud-19200",
        [SOL_UART_BAUD_RATE_38400] = "baud-38400",
        [SOL_UART_BAUD_RATE_57600] = "baud-57600",
        [SOL_UART_BAUD_RATE_115200] = "baud-115200"
    };

    if (baud_rate < sol_util_array_size(baud_rate_names))
        return baud_rate_names[baud_rate];

    return NULL;
}

SOL_API enum sol_uart_parity
sol_uart_parity_from_str(const char *parity)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("none", SOL_UART_PARITY_NONE),
        SOL_STR_TABLE_ITEM("even", SOL_UART_PARITY_EVEN),
        SOL_STR_TABLE_ITEM("odd", SOL_UART_PARITY_ODD),
        { }
    };

    SOL_NULL_CHECK(parity, SOL_UART_PARITY_NONE);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(parity), SOL_UART_PARITY_NONE);
}

SOL_API const char *
sol_uart_parity_to_str(enum sol_uart_parity parity)
{
    static const char *parity_names[] = {
        [SOL_UART_PARITY_NONE] = "none",
        [SOL_UART_PARITY_EVEN] = "even",
        [SOL_UART_PARITY_ODD] = "odd"
    };

    if (parity < sol_util_array_size(parity_names))
        return parity_names[parity];

    return NULL;
}

SOL_API enum sol_uart_stop_bits
sol_uart_stop_bits_from_str(const char *stop_bits)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("stopbits-1", SOL_UART_STOP_BITS_ONE),
        SOL_STR_TABLE_ITEM("stopbits-2", SOL_UART_STOP_BITS_TWO),
        { }
    };

    SOL_NULL_CHECK(stop_bits, SOL_UART_STOP_BITS_ONE);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(stop_bits), SOL_UART_STOP_BITS_ONE);
}

SOL_API const char *
sol_uart_stop_bits_to_str(enum sol_uart_stop_bits stop_bits)
{
    static const char *stop_bits_names[] = {
        [SOL_UART_STOP_BITS_ONE] = "stopbits-1",
        [SOL_UART_STOP_BITS_TWO] = "stopbits-2"
    };

    if (stop_bits < sol_util_array_size(stop_bits_names))
        return stop_bits_names[stop_bits];

    return NULL;
}


SOL_API enum sol_uart_data_bits
sol_uart_data_bits_from_str(const char *data_bits)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("databits-8", SOL_UART_DATA_BITS_8),
        SOL_STR_TABLE_ITEM("databits-7", SOL_UART_DATA_BITS_7),
        SOL_STR_TABLE_ITEM("databits-6", SOL_UART_DATA_BITS_6),
        SOL_STR_TABLE_ITEM("databits-5", SOL_UART_DATA_BITS_5),
        { }
    };

    SOL_NULL_CHECK(data_bits, SOL_UART_DATA_BITS_8);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(data_bits), SOL_UART_DATA_BITS_8);
}

SOL_API const char *
sol_uart_data_bits_to_str(enum sol_uart_data_bits data_bits)
{
    static const char *data_bits_names[] = {
        [SOL_UART_DATA_BITS_8] = "databits-8",
        [SOL_UART_DATA_BITS_7] = "databits-7",
        [SOL_UART_DATA_BITS_6] = "databits-6",
        [SOL_UART_DATA_BITS_5] = "databits-5"
    };

    if (data_bits < sol_util_array_size(data_bits_names))
        return data_bits_names[data_bits];

    return NULL;
}
