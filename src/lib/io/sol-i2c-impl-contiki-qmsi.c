/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <errno.h>
#include <stdlib.h>

#include <qm_i2c.h>
#include <qm_interrupt.h>
#include <qm_scss.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "i2c");

#include "sol-event-handler-contiki.h"
#include "sol-i2c.h"
#include "sol-mainloop.h"

enum xfer_type {
    NONE,
    READ,
    WRITE,
    READ_REG,
    WRITE_REG,
    READ_REG_MULTIPLE
};

struct sol_i2c {
    qm_i2c_t bus;
    uint8_t slave_addr;
    struct {
        enum xfer_type type;
        const void *user_data;
        const uint8_t *data;
        size_t length;
        uint8_t reg;
        ssize_t status;
        size_t multiple_count;
        size_t multiple_done;
        union {
            void (*rw)(void *user_data, struct sol_i2c *i2c, uint8_t *data,
                ssize_t status);
            void (*rw_reg)(void *user_data, struct sol_i2c *i2c, uint8_t reg,
                uint8_t *data, ssize_t status);
        };
    } xfer;
};

static process_event_t i2c_irq_event;
extern struct process soletta_app_process;

static struct sol_i2c *buses[QM_I2C_NUM];

static qm_rc_t begin_transfer(qm_i2c_t i2c, uint16_t slave, uint32_t id,
    const uint8_t *tx, uint32_t tx_len, const uint8_t *rx, uint32_t rx_len,
    bool stop);

void sol_i2c_close_raw(struct sol_i2c *i2c);

static void
i2c_cb_dispatch(void *user_data, process_event_t ev, process_data_t ev_data)
{
    struct sol_i2c *i2c = (struct sol_i2c *)ev_data;
    enum xfer_type type = i2c->xfer.type;

    i2c->xfer.type = NONE;
    if ((type == READ) || (type == WRITE))
        i2c->xfer.rw((void *)i2c->xfer.user_data, i2c,
            (uint8_t *)i2c->xfer.data, i2c->xfer.status);
    else
        i2c->xfer.rw_reg((void *)i2c->xfer.user_data, i2c, i2c->xfer.reg,
            (uint8_t *)i2c->xfer.data, i2c->xfer.status);
}

static void
tx_callback(uint32_t id, uint32_t len)
{
    struct sol_i2c *i2c = buses[id];
    qm_rc_t ret;

    if ((i2c->xfer.type != WRITE) && (i2c->xfer.type != WRITE_REG))
        return;

    if (i2c->xfer.multiple_count != i2c->xfer.multiple_done) {
        ret = begin_transfer(i2c->bus, i2c->slave_addr, id, i2c->xfer.data,
            i2c->xfer.length, NULL, 0, true);
        if (ret != QM_RC_OK) {
            i2c->xfer.status = -ret;
            process_post(&soletta_app_process, i2c_irq_event, i2c);
            return;
        }
        i2c->xfer.multiple_done++;
        return;
    }

    i2c->xfer.status = len;

    process_post(&soletta_app_process, i2c_irq_event, i2c);
}

static void
rx_callback(uint32_t id, uint32_t len)
{
    struct sol_i2c *i2c = buses[id];
    uint32_t offset;
    qm_rc_t ret;
    bool stop;

    if (i2c->xfer.type != READ_REG_MULTIPLE) {
        i2c->xfer.status = len;
        process_post(&soletta_app_process, i2c_irq_event, i2c);
        return;
    }

    if (i2c->xfer.multiple_done == i2c->xfer.multiple_count) {
        i2c->xfer.status = i2c->xfer.length * i2c->xfer.multiple_count;
        process_post(&soletta_app_process, i2c_irq_event, i2c);
        return;
    }

    offset = i2c->xfer.multiple_done * i2c->xfer.length;
    i2c->xfer.multiple_done++;
    stop = i2c->xfer.multiple_done == i2c->xfer.multiple_count;

    ret = begin_transfer(i2c->bus, i2c->slave_addr, id, &i2c->xfer.reg, 1,
        i2c->xfer.data + offset, i2c->xfer.length, stop);

    if (ret == QM_RC_OK)
        return;

    i2c->xfer.status = -ret;
    process_post(&soletta_app_process, i2c_irq_event, i2c);
}

static void
err_callback(uint32_t id, qm_i2c_status_t status)
{
    struct sol_i2c *i2c = buses[id];

    i2c->xfer.status = -status;

    process_post(&soletta_app_process, i2c_irq_event, i2c);
}

static qm_rc_t
begin_transfer(qm_i2c_t i2c, uint16_t slave, uint32_t id, const uint8_t *tx,
    uint32_t tx_len, const uint8_t *rx, uint32_t rx_len, bool stop)
{
    qm_i2c_transfer_t xfer;

    xfer.tx = (uint8_t *)tx;
    xfer.tx_len = tx_len;
    xfer.rx = (uint8_t *)rx;
    xfer.rx_len = rx_len;
    xfer.id = id;
    xfer.stop = stop;
    xfer.tx_callback = tx_callback;
    xfer.rx_callback = rx_callback;
    xfer.err_callback = err_callback;

    return qm_i2c_master_irq_transfer(i2c, &xfer, slave);
}

SOL_API struct sol_i2c *
sol_i2c_open_raw(uint8_t bus, enum sol_i2c_speed speed)
{
    struct sol_i2c *i2c;
    qm_i2c_config_t cfg;
    qm_i2c_speed_t bus_speed;
    qm_rc_t ret;

    if (bus >= QM_I2C_NUM) {
        SOL_WRN("I2C bus #%" PRIu8 " doesn't exist.", bus);
        return NULL;
    }

    if ((i2c = buses[bus]) != NULL)
        return i2c;

    switch (speed) {
    case SOL_I2C_SPEED_10KBIT:
    case SOL_I2C_SPEED_100KBIT:
        bus_speed = QM_I2C_SPEED_STD;
        break;
    case SOL_I2C_SPEED_400KBIT:
        bus_speed = QM_I2C_SPEED_FAST;
        break;
    case SOL_I2C_SPEED_1MBIT:
    case SOL_I2C_SPEED_3MBIT_400KBIT:
        bus_speed = QM_I2C_SPEED_FAST_PLUS;
        break;
    default:
        SOL_WRN("Unsupported speed value: %d", speed);
        return NULL;
    };

    switch (bus) {
    case QM_I2C_0:
        qm_irq_request(QM_IRQ_I2C_0, qm_i2c_0_isr);
        clk_periph_enable(CLK_PERIPH_CLK | CLK_PERIPH_I2C_M0_REGISTER);
        break;
#if QUARK_SE
    case QM_I2C_1:
        qm_irq_request(QM_IRQ_I2C_1, qm_i2c_1_isr);
        clk_periph_enable(CLK_PERIPH_CLK | CLK_PERIPH_I2C_M1_REGISTER);
        break;
#endif
    case QM_I2C_NUM:
        /* We checked if we were passed the limit before, so we should never
         * hit this point. Using all the enum values and no default, however,
         * allows us to rely on the compiler to know if there are values
         * we are not considering (depending on warning levels) */
        break;
    }

    i2c = calloc(1, sizeof(*i2c));
    SOL_NULL_CHECK(i2c, NULL);

    i2c->bus = bus;

    ret = qm_i2c_get_config(i2c->bus, &cfg);
    SOL_EXP_CHECK_GOTO(ret != QM_RC_OK, error);

    cfg.speed = bus_speed;
    cfg.address_mode = QM_I2C_7_BIT;
    cfg.mode = QM_I2C_MASTER;
    cfg.slave_addr = 0;

    ret = qm_i2c_set_config(i2c->bus, &cfg);
    SOL_EXP_CHECK_GOTO(ret != QM_RC_OK, error);

    if (!i2c_irq_event) {
        bool r;

        i2c_irq_event = process_alloc_event();
        r = sol_mainloop_contiki_event_handler_add(&i2c_irq_event, NULL,
            i2c_cb_dispatch, NULL);
        SOL_EXP_CHECK_GOTO(!r, error);
    }

    buses[i2c->bus] = i2c;

    return i2c;

error:
    free(i2c);
    return NULL;
}

void
sol_i2c_close_raw(struct sol_i2c *i2c)
{
    buses[i2c->bus] = NULL;
    free(i2c);
}

SOL_API int
sol_i2c_set_slave_address(struct sol_i2c *i2c, uint8_t slave_address)
{
    SOL_NULL_CHECK(i2c, -EINVAL);

    if (qm_i2c_get_status(i2c->bus) != QM_I2C_IDLE)
        return -EBUSY;

    i2c->slave_addr = slave_address;
    return 0;
}

SOL_API uint8_t
sol_i2c_get_slave_address(struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c, 0);

    return i2c->slave_addr;
}

SOL_API uint8_t
sol_i2c_get_bus(const struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c, 0);

    return i2c->bus;
}

SOL_API struct sol_i2c_pending *
sol_i2c_write_quick(struct sol_i2c *i2c, bool rw,
    void (*write_quick_cb)(void *cb_data, struct sol_i2c *i2c, ssize_t status),
    const void *cb_data)
{
    SOL_WRN("Unsupported");
    return NULL;
}

SOL_API struct sol_i2c_pending *
sol_i2c_read(struct sol_i2c *i2c, uint8_t *data, size_t count,
    void (*read_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t *data,
    ssize_t status), const void *cb_data)
{
    qm_rc_t ret;

    errno = EINVAL;
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);

    if (qm_i2c_get_status(i2c->bus) != QM_I2C_IDLE) {
        errno = EBUSY;
        return NULL;
    }

    i2c->xfer.type = READ;
    i2c->xfer.rw = read_cb;
    i2c->xfer.user_data = cb_data;
    i2c->xfer.data = data;
    i2c->xfer.length = count;
    i2c->xfer.status = 0;

    ret = begin_transfer(i2c->bus, i2c->slave_addr, i2c->bus, NULL, 0,
        data, i2c->xfer.length, true);
    errno = EINVAL;
    SOL_EXP_CHECK(ret != QM_RC_OK, NULL);

    errno = 0;
    return (struct sol_i2c_pending *)i2c;
}

SOL_API struct sol_i2c_pending *
sol_i2c_write(struct sol_i2c *i2c, uint8_t *data, size_t count,
    void (*write_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t *data,
    ssize_t status), const void *cb_data)
{
    qm_rc_t ret;

    errno = EINVAL;
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);

    if (qm_i2c_get_status(i2c->bus) != QM_I2C_IDLE) {
        errno = EBUSY;
        return NULL;
    }

    i2c->xfer.type = READ;
    i2c->xfer.rw = write_cb;
    i2c->xfer.user_data = cb_data;
    i2c->xfer.data = data;
    i2c->xfer.length = count;
    i2c->xfer.multiple_count = 0;
    i2c->xfer.multiple_done = 0;
    i2c->xfer.status = 0;

    ret = begin_transfer(i2c->bus, i2c->slave_addr, i2c->bus, i2c->xfer.data,
        i2c->xfer.length, NULL, 0, true);
    errno = EINVAL;
    SOL_EXP_CHECK(ret != QM_RC_OK, NULL);

    errno = 0;
    return (struct sol_i2c_pending *)i2c;
}

SOL_API struct sol_i2c_pending *
sol_i2c_read_register(struct sol_i2c *i2c, uint8_t reg, uint8_t *data,
    size_t count, void (*read_reg_cb)(void *cb_data, struct sol_i2c *i2c,
    uint8_t reg, uint8_t *data, ssize_t status), const void *cb_data)
{
    qm_rc_t ret;

    errno = EINVAL;
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);

    if (qm_i2c_get_status(i2c->bus) != QM_I2C_IDLE) {
        errno = EBUSY;
        return NULL;
    }

    i2c->xfer.type = READ_REG;
    i2c->xfer.rw_reg = read_reg_cb;
    i2c->xfer.user_data = cb_data;
    i2c->xfer.data = data;
    i2c->xfer.length = count;
    i2c->xfer.reg = reg;
    i2c->xfer.status = 0;

    ret = begin_transfer(i2c->bus, i2c->slave_addr, i2c->bus, &i2c->xfer.reg, 1,
        data, i2c->xfer.length, true);
    errno = EINVAL;
    SOL_EXP_CHECK(ret != QM_RC_OK, NULL);

    errno = 0;
    return (struct sol_i2c_pending *)i2c;
}

SOL_API struct sol_i2c_pending *
sol_i2c_read_register_multiple(struct sol_i2c *i2c, uint8_t reg, uint8_t *data,
    size_t count, uint8_t times, void (*read_reg_multiple_cb)(void *cb_data,
    struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status),
    const void *cb_data)
{
    qm_rc_t ret;

    errno = EINVAL;
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);

    if (qm_i2c_get_status(i2c->bus) != QM_I2C_IDLE) {
        errno = EBUSY;
        return NULL;
    }

    i2c->xfer.type = READ_REG_MULTIPLE;
    i2c->xfer.rw_reg = read_reg_multiple_cb;
    i2c->xfer.user_data = cb_data;
    i2c->xfer.data = data;
    i2c->xfer.length = count;
    i2c->xfer.multiple_count = times;
    i2c->xfer.multiple_done = 1;
    i2c->xfer.reg = reg;
    i2c->xfer.status = 0;

    ret = begin_transfer(i2c->bus, i2c->slave_addr, i2c->bus, &i2c->xfer.reg, 1,
        data, i2c->xfer.length, false);
    errno = EINVAL;
    SOL_EXP_CHECK(ret != QM_RC_OK, NULL);

    errno = 0;
    return (struct sol_i2c_pending *)i2c;
}

SOL_API struct sol_i2c_pending *
sol_i2c_write_register(struct sol_i2c *i2c, uint8_t reg, const uint8_t *data,
    size_t count, void (*write_reg_cb)(void *cb_data, struct sol_i2c *i2c,
    uint8_t reg, uint8_t *data, ssize_t status), const void *cb_data)
{
    qm_rc_t ret;

    errno = EINVAL;
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);

    if (qm_i2c_get_status(i2c->bus) != QM_I2C_IDLE) {
        errno = EBUSY;
        return NULL;
    }

    i2c->xfer.type = WRITE_REG;
    i2c->xfer.rw_reg = write_reg_cb;
    i2c->xfer.user_data = cb_data;
    i2c->xfer.data = data;
    i2c->xfer.length = count;
    i2c->xfer.multiple_count = 1;
    i2c->xfer.multiple_done = 0;
    i2c->xfer.reg = reg;
    i2c->xfer.status = 0;

    ret = begin_transfer(i2c->bus, i2c->slave_addr, i2c->bus, &i2c->xfer.reg, 1,
        NULL, 0, false);
    errno = EINVAL;
    SOL_EXP_CHECK(ret != QM_RC_OK, NULL);

    errno = 0;
    return (struct sol_i2c_pending *)i2c;
}

SOL_API void
sol_i2c_pending_cancel(struct sol_i2c *i2c, struct sol_i2c_pending *pending)
{
    SOL_NULL_CHECK(i2c);
    SOL_NULL_CHECK(pending);

    if (i2c->xfer.type == NONE)
        return;
    qm_i2c_transfer_terminate(i2c->bus);
}
