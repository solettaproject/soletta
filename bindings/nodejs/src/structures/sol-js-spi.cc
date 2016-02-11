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

#include <nan.h>
#include "../common.h"
#include "sol-js-spi.h"

using namespace v8;

bool c_sol_spi_config(v8::Local<v8::Object> jsSPIConfig,
    sol_spi_config *config) {
    SOL_SET_API_VERSION(config->api_version = SOL_SPI_CONFIG_API_VERSION;)

    VALIDATE_AND_ASSIGN((*config), chip_select, unsigned int, IsUint32,
        "(Chip select)", false, jsSPIConfig, Uint32Value);

    VALIDATE_AND_ASSIGN((*config), mode, sol_spi_mode, IsInt32,
        "(SPI transfer mode)", false, jsSPIConfig, Int32Value);

    VALIDATE_AND_ASSIGN((*config), frequency, uint32_t, IsUint32,
        "(Frequency in Hz)", false, jsSPIConfig, Uint32Value);

    VALIDATE_AND_ASSIGN((*config), bits_per_word, uint8_t, IsUint32,
        "(Bits per word)", false, jsSPIConfig, Uint32Value);

    return true;
}
