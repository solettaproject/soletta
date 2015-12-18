/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <time.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "test.h"
#include "sol-util.h"
#include "sol-lwm2m.h"

DEFINE_TEST(test_tlv);

#define STR ("I love LWM2M")
#define STR2 ("")
#define STR3 ("THIS IS BIG TEXTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT")
#define STR4 ("OPAQUE STRING")

static void
test_tlv(void)
{
    uint16_t obj, instance;
    struct sol_lwm2m_resource resources[21];
    struct sol_lwm2m_tlv tlv;
    size_t i;
    int r;

    obj = 2;
    instance = UINT16_MAX;

    r = sol_lwm2m_resource_init(&resources[0], 0, SOL_LWM2M_RESOURCE_TYPE_BOOLEAN, true);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[1], 20, SOL_LWM2M_RESOURCE_TYPE_BOOLEAN, false);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[2], 40, SOL_LWM2M_RESOURCE_TYPE_STRING, STR);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[3], UINT16_MAX, SOL_LWM2M_RESOURCE_TYPE_STRING, STR2);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[4], UINT16_MAX, SOL_LWM2M_RESOURCE_TYPE_STRING, STR3);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[5], 66, SOL_LWM2M_RESOURCE_TYPE_INT, 2);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[6], 66, SOL_LWM2M_RESOURCE_TYPE_INT, -10);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[7], 66, SOL_LWM2M_RESOURCE_TYPE_INT, 100);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[8], 66, SOL_LWM2M_RESOURCE_TYPE_INT, 40);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[9], 66, SOL_LWM2M_RESOURCE_TYPE_INT, INT64_MAX);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[10], 66, SOL_LWM2M_RESOURCE_TYPE_OBJ_LINK, obj, instance);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[11], 66, SOL_LWM2M_RESOURCE_TYPE_TIME, time(NULL));
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[12], 22, SOL_LWM2M_RESOURCE_TYPE_FLOAT, (double)2.5);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[13], 1000, SOL_LWM2M_RESOURCE_TYPE_FLOAT, (double)3.4);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[14], 256, SOL_LWM2M_RESOURCE_TYPE_FLOAT, (double)-2.3);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[15], 255, SOL_LWM2M_RESOURCE_TYPE_FLOAT, (double)-300.23);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[16], 255, SOL_LWM2M_RESOURCE_TYPE_FLOAT, DBL_MAX);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[17], 255, SOL_LWM2M_RESOURCE_TYPE_FLOAT, DBL_MIN);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[18], 255, SOL_LWM2M_RESOURCE_TYPE_FLOAT, FLT_MIN);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[19], 255, SOL_LWM2M_RESOURCE_TYPE_FLOAT, FLT_MAX);
    ASSERT_INT_EQ(r, 0);
    r = sol_lwm2m_resource_init(&resources[20], 255, SOL_LWM2M_RESOURCE_TYPE_OPAQUE, sol_str_slice_from_str(STR4));
    ASSERT_INT_EQ(r, 0);

    for (i = 0; i < ARRAY_SIZE(resources); i++) {
        r = sol_lwm2m_resource_to_tlv(&resources[i], &tlv);
        ASSERT_INT_EQ(r, 0);
        ASSERT_INT_EQ(resources[i].id, tlv.id);
        switch (resources[i].type) {
        case SOL_LWM2M_RESOURCE_TYPE_STRING:
        case SOL_LWM2M_RESOURCE_TYPE_OPAQUE:
        {
            uint8_t *bytes;
            uint16_t len;

            r  = sol_lwm2m_tlv_get_bytes(&tlv, &bytes, &len);
            ASSERT_INT_EQ(r, 0);
            ASSERT(len == resources[i].data.bytes.len);
            ASSERT(!memcmp(bytes, resources[i].data.bytes.data, len));
            break;
        }
        case SOL_LWM2M_RESOURCE_TYPE_FLOAT:
        {
            double fp, rel;
            r = sol_lwl2m_tlv_to_float(&tlv, &fp);
            ASSERT_INT_EQ(r, 0);
            rel = fabs(fp - resources[i].data.fp) / resources[i].data.fp;
            ASSERT(rel <= 0.0032);
            break;
        }
        case SOL_LWM2M_RESOURCE_TYPE_INT:
        case SOL_LWM2M_RESOURCE_TYPE_TIME:
        {
            int64_t v;
            r = sol_lwl2m_tlv_to_int(&tlv, &v);
            ASSERT_INT_EQ(r, 0);
            ASSERT(v == resources[i].data.integer);
            break;
        }
        case SOL_LWM2M_RESOURCE_TYPE_BOOLEAN:
        {
            bool b;
            r = sol_lwl2m_tlv_to_bool(&tlv, &b);
            ASSERT_INT_EQ(r, 0);
            ASSERT(b == (bool)resources[i].data.integer);
            break;
        }
        case SOL_LWM2M_RESOURCE_TYPE_OBJ_LINK:
        {
            uint16_t o, in;
            r = sol_lwm2m_tlv_to_obj_link(&tlv, &o, &in);
            ASSERT_INT_EQ(r, 0);
            ASSERT_INT_EQ(o, obj);
            ASSERT_INT_EQ(in, instance);
            break;
        }
        default:
            break;
        }
        sol_lwm2m_tlv_clear(&tlv);
    }
}

TEST_MAIN();
