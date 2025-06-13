/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#define DT_DRV_COMPAT azoteq_iqs5xx

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include "iqs5xx.h"

LOG_MODULE_REGISTER(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

static int iqs_regdump_err = 0;

// Default config
struct iqs5xx_reg_config iqs5xx_reg_config_default () {
    struct iqs5xx_reg_config regconf;

        regconf.activeRefreshRate =         5;    // Increased from 10 for faster response
        regconf.idleRefreshRate =           20;   // Increased from 50
        regconf.singleFingerGestureMask =   GESTURE_SINGLE_TAP | GESTURE_TAP_AND_HOLD;
        regconf.multiFingerGestureMask =    GESTURE_TWO_FINGER_TAP | GESTURE_SCROLLG;
        regconf.tapTime =                   100;  // Reduced for faster taps
        regconf.tapDistance =               15;   // Reduced for more sensitive taps
        regconf.touchMultiplier =           0;
        regconf.debounce =                  0;
        regconf.i2cTimeout =                2;    // Reduced timeout
        regconf.filterSettings =            MAV_FILTER | IIR_FILTER;
        regconf.filterDynBottomBeta =        15;  // Reduced for less filtering
        regconf.filterDynLowerSpeed =        10;  // Reduced for faster response
        regconf.filterDynUpperSpeed =        200; // Increased for better fast movements
        regconf.initScrollDistance =        10;   // Reduced for easier scrolling

        return regconf;
}

/**
 * @brief Read from the iqs550 chip via i2c
 */
static int iqs5xx_seq_read(const struct device *dev, const uint16_t start, uint8_t *read_buf,
                           const uint8_t len) {
    const struct iqs5xx_data *data = dev->data;
    uint16_t nstart = (start << 8 ) | (start >> 8);
    return i2c_write_read(data->i2c, AZOTEQ_IQS5XX_ADDR, &nstart, sizeof(nstart), read_buf, len);
}

/**
 * @brief Write to the iqs550 chip via i2c
 */
static int iqs5xx_write(const struct device *dev, const uint16_t start_addr, const uint8_t *buf,
                        uint32_t num_bytes) {

    const struct iqs5xx_data *data = dev->data;

    uint8_t addr_buffer[2];
    struct i2c_msg msg[2];

    addr_buffer[1] = start_addr & 0xFF;
    addr_buffer[0] = start_addr >> 8;
    msg[0].buf = addr_buffer;
    msg[0].len = 2U;
    msg[0].flags = I2C_MSG_WRITE;

    msg[1].buf = (uint8_t *)buf;
    msg[1].len = num_bytes;
    msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

    int err = i2c_transfer(data->i2c, msg, 2, AZOTEQ_IQS5XX_ADDR);
    return err;
}

static int iqs5xx_reg_dump (const struct device *dev) {
    return iqs5xx_write(dev, IQS5XX_REG_DUMP_START_ADDRESS, _iqs5xx_regdump, IQS5XX_REG_DUMP_SIZE);
}

/**
 * @brief Read data from IQS5XX
*/
static int iqs5xx_sample_fetch (const struct device *dev) {
    uint8_t buffer[44];

    struct iqs5xx_data *data = dev->data;

    int res = iqs5xx_seq_read(dev, GestureEvents0_adr, buffer, 44);
    iqs5xx_write(dev, END_WINDOW, 0, 1);
    if (res < 0) {
        return res;
    }

    // Gestures
    data->raw_data.gestures0 =      buffer[0];
    data->raw_data.gestures1 =      buffer[1];
    // System info
    data->raw_data.system_info0 =   buffer[2];
    data->raw_data.system_info1 =   buffer[3];
    // Number of fingers
    data->raw_data.finger_count =   buffer[4];
    // Relative X position
    data->raw_data.rx =             buffer[5] << 8 | buffer[6];
    // Relative Y position
    data->raw_data.ry =             buffer[7] << 8 | buffer[8];

    // Fingers
    for(int i = 0; i < 5; i++) {
        const int p = 9 + (7 * i);
        // Absolute X
        data->raw_data.fingers[i].ax = buffer[p + 0] << 8 | buffer[p + 1];
        // Absolute Y
        data->raw_data.fingers[i].ay = buffer[p + 2] << 8 | buffer[p + 3];
        // Touch strength
        data->raw_data.fingers[i].strength = buffer[p + 4] << 8 | buffer[p + 5];
        // Area
        data->raw_data.fingers[i].area= buffer[p + 6];
    }
    return 0;
}

static void iqs5xx_work_cb(struct k_work *work) {
    struct iqs5xx_data *data = CONTAINER_OF(work, struct iqs5xx_data, work);

    k_mutex_lock(&data->i2c_mutex, K_MSEC(1000));
    iqs5xx_sample_fetch(data->dev);

    // Trigger callback if registered
    if(data->data_ready_handler != NULL) {
        data->data_ready_handler(data->dev, &data->raw_data);
    }
    k_mutex_unlock(&data->i2c_mutex);
}

/**
 * @brief Called when data ready pin goes active. Submits work to workqueue.
 */
static void iqs5xx_gpio_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    struct iqs5xx_data *data = CONTAINER_OF(cb, struct iqs5xx_data, dr_cb);

    LOG_DBG("Data ready pin asserted");
    k_work_submit(&data->work);
}

/**
 * @brief Sets the trigger handler
*/
int iqs5xx_trigger_set(const struct device *dev, iqs5xx_trigger_handler_t handler) {
    struct iqs5xx_data *data = dev->data;
    data->data_ready_handler = handler;
    return 0;
}

/**
 * @brief Sets registers to initial values
 */
int iqs5xx_registers_init (const struct device *dev, const struct iqs5xx_reg_config *config) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *conf = dev->config;

    k_mutex_lock(&data->i2c_mutex, K_MSEC(5000));

    // Wait for dataready
    while(!gpio_pin_get_dt(&conf->dr)) {
        k_usleep(200);
    }

    uint8_t buf = RESET_TP;
    // Reset device
    iqs5xx_write(dev, SystemControl1_adr, &buf, 1);
    iqs5xx_write(dev, END_WINDOW, 0, 1);
    k_msleep(10);

    while(!gpio_pin_get_dt(&conf->dr)) {
        k_usleep(200);
    }

    // Write register dump
    iqs_regdump_err = iqs5xx_reg_dump(dev);

    while(!gpio_pin_get_dt(&conf->dr)) {
        k_usleep(200);
    }

    int err = 0;

    // 16 or 32 bit values must be swapped to big endian
    uint8_t wbuff[16];

    // Set active refresh rate
    *((uint16_t*)wbuff) = SWPEND16(config->activeRefreshRate);
    err |= iqs5xx_write(dev, ActiveRR_adr, wbuff, 2);

    // Set idle refresh rate
    *((uint16_t*)wbuff) = SWPEND16(config->idleRefreshRate);
    err |= iqs5xx_write(dev, IdleRR_adr, wbuff, 2);

    // Set single finger gestures
    err |= iqs5xx_write(dev, SFGestureEnable_adr, &config->singleFingerGestureMask, 1);

    // Set multi finger gestures
    err |= iqs5xx_write(dev, MFGestureEnable_adr, &config->multiFingerGestureMask, 1);

    // Set tap time
    *((uint16_t*)wbuff) = SWPEND16(config->tapTime);
    err |= iqs5xx_write(dev, TapTime_adr, wbuff, 2);

    // Set tap distance
    *((uint16_t*)wbuff) = SWPEND16(config->tapDistance);
    err |= iqs5xx_write(dev, TapDistance_adr, wbuff, 2);

    // Set touch multiplier
    err |= iqs5xx_write(dev, GlobalTouchSet_adr, &config->touchMultiplier, 1);

    // Set debounce settings
    err |= iqs5xx_write(dev, ProxDb_adr, &config->debounce, 1);
    err |= iqs5xx_write(dev, TouchSnapDb_adr, &config->debounce, 1);

    wbuff[0] = 0;
    // Set noise reduction
    err |= iqs5xx_write(dev, HardwareSettingsA_adr, wbuff, 1);

    // Set i2c timeout
    err |= iqs5xx_write(dev, I2CTimeout_adr, &config->i2cTimeout, 1);

    // Set filter settings
    err |= iqs5xx_write(dev, FilterSettings0_adr, &config->filterSettings, 1);
    err |= iqs5xx_write(dev, DynamicBottomBeta_adr, &config->filterDynBottomBeta, 1);
    err |= iqs5xx_write(dev, DynamicLowerSpeed_adr, &config->filterDynLowerSpeed, 1);

    *((uint16_t*)wbuff) = SWPEND16(config->filterDynUpperSpeed);
    err |= iqs5xx_write(dev, DynamicUpperSpeed_adr, wbuff, 2);

    // Set initial scroll distance
    *((uint16_t*)wbuff) = SWPEND16(config->initScrollDistance);
    err |= iqs5xx_write(dev, ScrollInitDistance_adr, wbuff, 2);

    // Terminate transaction
    iqs5xx_write(dev, END_WINDOW, 0, 1);

    k_mutex_unlock(&data->i2c_mutex);

    return err;
}

static int iqs5xx_init(const struct device *dev) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *config = dev->config;

    data->dev = dev;
    data->i2c = DEVICE_DT_GET(DT_BUS(DT_DRV_INST(0)));

    k_mutex_init(&data->i2c_mutex);
    k_work_init(&data->work, iqs5xx_work_cb);

    // Configure data ready pin using devicetree spec
    int ret = gpio_pin_configure_dt(&config->dr, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure data ready pin: %d", ret);
        return ret;
    }

    // Initialize interrupt callback
    gpio_init_callback(&data->dr_cb, iqs5xx_gpio_cb, BIT(config->dr.pin));

    // Add callback
    ret = gpio_add_callback(config->dr.port, &data->dr_cb);
    if (ret < 0) {
        LOG_ERR("Failed to add GPIO callback: %d", ret);
        return ret;
    }

    // Configure data ready interrupt
    ret = gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure interrupt: %d", ret);
        return ret;
    }

    // Initialize device registers
    struct iqs5xx_reg_config iqs5xx_registers = iqs5xx_reg_config_default();
    ret = iqs5xx_registers_init(dev, &iqs5xx_registers);
    if(ret) {
        LOG_ERR("Failed to initialize IQS5xx registers: %d", ret);
        return ret;
    }

    LOG_INF("IQS5XX initialized successfully");
    return 0;
}

// Device instance data
static struct iqs5xx_data iqs5xx_data_0 = {
    .data_ready_handler = NULL
};

// Device configuration from devicetree
static const struct iqs5xx_config iqs5xx_config_0 = {
    .dr = GPIO_DT_SPEC_GET_OR(DT_DRV_INST(0), dr_gpios, {}),
    .invert_x = DT_INST_PROP_OR(0, invert_x, false),
    .invert_y = DT_INST_PROP_OR(0, invert_y, false),
};

DEVICE_DT_INST_DEFINE(0, iqs5xx_init, NULL, &iqs5xx_data_0, &iqs5xx_config_0,
                      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);
