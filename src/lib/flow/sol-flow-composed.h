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


#pragma once

#include "sol-flow.h"
#include "sol-flow-metatype.h"
#include "sol-buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

int create_composed_constructor_type(const struct sol_flow_metatype_context *ctx, struct sol_flow_node_type **type);

int create_composed_splitter_type(const struct sol_flow_metatype_context *ctx, struct sol_flow_node_type **type);

int composed_metatype_constructor_generate_code_start(const struct sol_flow_metatype_context *context, struct sol_buffer *out);
int composed_metatype_constructor_generate_code_type(const struct sol_flow_metatype_context *context, struct sol_buffer *out);
int composed_metatype_constructor_generate_code_end(const struct sol_flow_metatype_context *context, struct sol_buffer *out);
int composed_metatype_constructor_get_ports_description(const struct sol_flow_metatype_context *context, struct sol_vector *in, struct sol_vector *out);

int composed_metatype_splitter_generate_code_start(const struct sol_flow_metatype_context *context, struct sol_buffer *out);
int composed_metatype_splitter_generate_code_type(const struct sol_flow_metatype_context *context, struct sol_buffer *out);
int composed_metatype_splitter_generate_code_end(const struct sol_flow_metatype_context *context,  struct sol_buffer *out);
int composed_metatype_splitter_get_ports_description(const struct sol_flow_metatype_context *context, struct sol_vector *in, struct sol_vector *out);

#ifdef __cplusplus
}
#endif
