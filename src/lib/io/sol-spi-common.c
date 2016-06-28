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
#include "sol-spi.h"
#include "sol-util.h"

SOL_API enum sol_spi_mode
sol_spi_mode_from_str(const char *spi_mode)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("mode0", SOL_SPI_MODE_0),
        SOL_STR_TABLE_ITEM("mode1", SOL_SPI_MODE_1),
        SOL_STR_TABLE_ITEM("mode2", SOL_SPI_MODE_2),
        SOL_STR_TABLE_ITEM("mode3", SOL_SPI_MODE_3),
        { }
    };

    SOL_NULL_CHECK(spi_mode, SOL_SPI_MODE_0);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(spi_mode), SOL_SPI_MODE_0);
}

SOL_API const char *
sol_spi_mode_to_str(enum sol_spi_mode spi_mode)
{
    static const char *spi_mode_names[] = {
        [SOL_SPI_MODE_0] = "mode0",
        [SOL_SPI_MODE_1] = "mode1",
        [SOL_SPI_MODE_2] = "mode2",
        [SOL_SPI_MODE_3] = "mode3"
    };

    if (spi_mode < sol_util_array_size(spi_mode_names))
        return spi_mode_names[spi_mode];

    return NULL;
}
