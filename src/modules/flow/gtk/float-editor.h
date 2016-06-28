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

#include "common.h"
#include "sol-flow.h"

struct float_editor_note_type {
    struct sol_flow_node_type base;
    void (*setup_widget)(struct gtk_common_data *mdata);
    void (*send_output_packet)(struct gtk_common_data *mdata);
};

DEFINE_DEFAULT_HEADER(float_editor);

void direction_vector_setup(struct gtk_common_data *mdata);
void location_setup(struct gtk_common_data *mdata);
void send_location_output(struct gtk_common_data *mdata);
void send_direction_vector_output(struct gtk_common_data *mdata);
void float_setup(struct gtk_common_data *mdata);
void send_float_output(struct gtk_common_data *mdata);
