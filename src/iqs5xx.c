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
    LOG_INF("Creating default IQS5XX register configuration");
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

        LOG_INF("Default config: refresh=%d/%d, gestures=0x%02x/0x%02x",
                regconf.activeRefreshRate, regconf.idleRefreshRate,
                regconf.singleFingerGestureMask, regconf.multiFingerGestureMask);
        return regconf;
}

/**
 * @brief Read from the iqs550 chip via i2c
 */
static int iqs5xx_seq_read(const struct device *dev, const uint16_t start, uint8_t *read_buf,
                           const uint8_t len) {
    const struct iqs5xx_data *data = dev->data;
    uint16_t nstart = (start << 8 ) | (start >> 8);

    LOG_DBG("I2C read: addr=0x%04x, len=%d", start, len);
    int ret = i2c_write_read(data->i2c, AZOTEQ_IQS5XX_ADDR, &nstart, sizeof(nstart), read_buf, len);
    if (ret < 0) {
        LOG_ERR("I2C read failed: addr=0x%04x, ret=%d", start, ret);
    }
    return ret;
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

    LOG_DBG("I2C write: addr=0x%04x, len=%d", start_addr, num_bytes);
    int err = i2c_transfer(data->i2c, msg, 2, AZOTEQ_IQS5XX_ADDR);
    if (err < 0) {
        LOG_ERR("I2C write failed: addr=0x%04x, ret=%d", start_addr, err);
    }
    return err;
}

static int iqs5xx_reg_dump (const struct device *dev) {
    LOG_INF("Writing register dump (%d bytes to 0x%04x)",
            IQS5XX_REG_DUMP_SIZE, IQS5XX_REG_DUMP_START_ADDRESS);
    int ret = iqs5xx_write(dev, IQS5XX_REG_DUMP_START_ADDRESS, _iqs5xx_regdump, IQS5XX_REG_DUMP_SIZE);
    if (ret < 0) {
        LOG_ERR("Register dump failed: %d", ret);
    } else {
        LOG_INF("Register dump completed successfully");
    }
    return ret;
}

/**
 * @brief Read data from IQS5XX
*/
static int iqs5xx_sample_fetch (const struct device *dev) {
    uint8_t buffer[44];
    struct iqs5xx_data *data = dev->data;

    LOG_DBG("Fetching sample data");
    int res = iqs5xx_seq_read(dev, GestureEvents0_adr, buffer, 44);
    iqs5xx_write(dev, END_WINDOW, 0, 1);

    if (res < 0) {
        LOG_ERR("Sample fetch failed: %d", res);
        return res;
    }

    // Parse data
    data->raw_data.gestures0 =      buffer[0];
    data->raw_data.gestures1 =      buffer[1];
    data->raw_data.system_info0 =   buffer[2];
    data->raw_data.system_info1 =   buffer[3];
    data->raw_data.finger_count =   buffer[4];
    data->raw_data.rx =             buffer[5] << 8 | buffer[6];
    data->raw_data.ry =             buffer[7] << 8 | buffer[8];

    // Log interesting data
    if (data->raw_data.finger_count > 0 || data->raw_data.gestures0 || data->raw_data.gestures1) {
        LOG_INF("Sample: fingers=%d, gestures=0x%02x/0x%02x, rel=%d/%d",
                data->raw_data.finger_count, data->raw_data.gestures0, data->raw_data.gestures1,
                data->raw_data.rx, data->raw_data.ry);
    }

    // Parse finger data
    for(int i = 0; i < 5; i++) {
        const int p = 9 + (7 * i);
        data->raw_data.fingers[i].ax = buffer[p + 0] << 8 | buffer[p + 1];
        data->raw_data.fingers[i].ay = buffer[p + 2] << 8 | buffer[p + 3];
        data->raw_data.fingers[i].strength = buffer[p + 4] << 8 | buffer[p + 5];
        data->raw_data.fingers[i].area= buffer[p + 6];

        if (i < data->raw_data.finger_count && data->raw_data.fingers[i].strength > 0) {
            LOG_DBG("Finger %d: pos=%d/%d, strength=%d, area=%d", i,
                    data->raw_data.fingers[i].ax, data->raw_data.fingers[i].ay,
                    data->raw_data.fingers[i].strength, data->raw_data.fingers[i].area);
        }
    }

    return 0;
}

static void iqs5xx_work_cb(struct k_work *work) {
    struct iqs5xx_data *data = CONTAINER_OF(work, struct iqs5xx_data, work);

    LOG_DBG("Work callback triggered");

    k_mutex_lock(&data->i2c_mutex, K_MSEC(1000));
    int ret = iqs5xx_sample_fetch(data->dev);

    if (ret == 0 && data->data_ready_handler != NULL) {
        LOG_DBG("Calling data ready handler");
        data->data_ready_handler(data->dev, &data->raw_data);
    } else if (ret != 0) {
        LOG_ERR("Sample fetch failed in work callback: %d", ret);
    } else {
        LOG_WRN("No data ready handler registered");
    }

    k_mutex_unlock(&data->i2c_mutex);
}

/**
 * @brief Called when data ready pin goes active. Submits work to workqueue.
 */
static void iqs5xx_gpio_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    struct iqs5xx_data *data = CONTAINER_OF(cb, struct iqs5xx_data, dr_cb);

    LOG_DBG("GPIO interrupt: port=%p, pins=0x%08x", port, pins);
    k_work_submit(&data->work);
}

/**
 * @brief Sets the trigger handler
*/
int iqs5xx_trigger_set(const struct device *dev, iqs5xx_trigger_handler_t handler) {
    struct iqs5xx_data *data = dev->data;
    LOG_INF("Setting trigger handler: %p", handler);
    data->data_ready_handler = handler;
    return 0;
}

/**
 * @brief Sets registers to initial values
 */
int iqs5xx_registers_init (const struct device *dev, const struct iqs5xx_reg_config *config) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *conf = dev->config;

    LOG_INF("Starting register initialization");

    k_mutex_lock(&data->i2c_mutex, K_MSEC(5000));

    // Wait for dataready
    LOG_INF("Waiting for initial data ready...");
    int timeout = 0;
    while(!gpio_pin_get_dt(&conf->dr) && timeout < 1000) {
        k_usleep(200);
        timeout++;
    }

    if (timeout >= 1000) {
        LOG_ERR("Timeout waiting for initial data ready");
        k_mutex_unlock(&data->i2c_mutex);
        return -ETIMEDOUT;
    }
    LOG_INF("Data ready pin is active");

    // Reset device
    LOG_INF("Resetting device...");
    uint8_t buf = RESET_TP;
    int ret = iqs5xx_write(dev, SystemControl1_adr, &buf, 1);
    if (ret < 0) {
        LOG_ERR("Failed to reset device: %d", ret);
        k_mutex_unlock(&data->i2c_mutex);
        return ret;
    }

    iqs5xx_write(dev, END_WINDOW, 0, 1);
    k_msleep(10);

    // Wait for ready after reset
    LOG_INF("Waiting for data ready after reset...");
    timeout = 0;
    while(!gpio_pin_get_dt(&conf->dr) && timeout < 1000) {
        k_usleep(200);
        timeout++;
    }

    if (timeout >= 1000) {
        LOG_ERR("Timeout waiting for data ready after reset");
        k_mutex_unlock(&data->i2c_mutex);
        return -ETIMEDOUT;
    }

    // Write register dump
    iqs_regdump_err = iqs5xx_reg_dump(dev);
    if (iqs_regdump_err < 0) {
        LOG_ERR("Register dump failed: %d", iqs_regdump_err);
        k_mutex_unlock(&data->i2c_mutex);
        return iqs_regdump_err;
    }

    // Wait for ready after regdump
    LOG_INF("Waiting for data ready after regdump...");
    timeout = 0;
    while(!gpio_pin_get_dt(&conf->dr) && timeout < 1000) {
        k_usleep(200);
        timeout++;
    }

    if (timeout >= 1000) {
        LOG_ERR("Timeout waiting for data ready after regdump");
        k_mutex_unlock(&data->i2c_mutex);
        return -ETIMEDOUT;
    }

    LOG_INF("Configuring individual registers...");
    int err = 0;
    uint8_t wbuff[16];

    // Set active refresh rate
    *((uint16_t*)wbuff) = SWPEND16(config->activeRefreshRate);
    ret = iqs5xx_write(dev, ActiveRR_adr, wbuff, 2);
    if (ret < 0) {
        LOG_ERR("Failed to set active refresh rate: %d", ret);
        err |= ret;
    }

    // Set idle refresh rate
    *((uint16_t*)wbuff) = SWPEND16(config->idleRefreshRate);
    ret = iqs5xx_write(dev, IdleRR_adr, wbuff, 2);
    if (ret < 0) {
        LOG_ERR("Failed to set idle refresh rate: %d", ret);
        err |= ret;
    }

    // Set single finger gestures
    ret = iqs5xx_write(dev, SFGestureEnable_adr, &config->singleFingerGestureMask, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set single finger gestures: %d", ret);
        err |= ret;
    }

    // Set multi finger gestures
    ret = iqs5xx_write(dev, MFGestureEnable_adr, &config->multiFingerGestureMask, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set multi finger gestures: %d", ret);
        err |= ret;
    }

    // Set tap time
    *((uint16_t*)wbuff) = SWPEND16(config->tapTime);
    ret = iqs5xx_write(dev, TapTime_adr, wbuff, 2);
    if (ret < 0) {
        LOG_ERR("Failed to set tap time: %d", ret);
        err |= ret;
    }

    // Set tap distance
    *((uint16_t*)wbuff) = SWPEND16(config->tapDistance);
    ret = iqs5xx_write(dev, TapDistance_adr, wbuff, 2);
    if (ret < 0) {
        LOG_ERR("Failed to set tap distance: %d", ret);
        err |= ret;
    }

    // Set touch multiplier
    ret = iqs5xx_write(dev, GlobalTouchSet_adr, &config->touchMultiplier, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set touch multiplier: %d", ret);
        err |= ret;
    }

    // Set debounce settings
    ret = iqs5xx_write(dev, ProxDb_adr, &config->debounce, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set prox debounce: %d", ret);
        err |= ret;
    }

    ret = iqs5xx_write(dev, TouchSnapDb_adr, &config->debounce, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set touch snap debounce: %d", ret);
        err |= ret;
    }

    // Set noise reduction
    wbuff[0] = 0;
    ret = iqs5xx_write(dev, HardwareSettingsA_adr, wbuff, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set hardware settings: %d", ret);
        err |= ret;
    }

    // Set i2c timeout
    ret = iqs5xx_write(dev, I2CTimeout_adr, &config->i2cTimeout, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set I2C timeout: %d", ret);
        err |= ret;
    }

    // Set filter settings
    ret = iqs5xx_write(dev, FilterSettings0_adr, &config->filterSettings, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set filter settings: %d", ret);
        err |= ret;
    }

    ret = iqs5xx_write(dev, DynamicBottomBeta_adr, &config->filterDynBottomBeta, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set dynamic bottom beta: %d", ret);
        err |= ret;
    }

    ret = iqs5xx_write(dev, DynamicLowerSpeed_adr, &config->filterDynLowerSpeed, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set dynamic lower speed: %d", ret);
        err |= ret;
    }

    *((uint16_t*)wbuff) = SWPEND16(config->filterDynUpperSpeed);
    ret = iqs5xx_write(dev, DynamicUpperSpeed_adr, wbuff, 2);
    if (ret < 0) {
        LOG_ERR("Failed to set dynamic upper speed: %d", ret);
        err |= ret;
    }

    // Set initial scroll distance
    *((uint16_t*)wbuff) = SWPEND16(config->initScrollDistance);
    ret = iqs5xx_write(dev, ScrollInitDistance_adr, wbuff, 2);
    if (ret < 0) {
        LOG_ERR("Failed to set scroll distance: %d", ret);
        err |= ret;
    }

    // Terminate transaction
    iqs5xx_write(dev, END_WINDOW, 0, 1);

    k_mutex_unlock(&data->i2c_mutex);

    if (err == 0) {
        LOG_INF("Register initialization completed successfully");
    } else {
        LOG_ERR("Register initialization completed with errors: %d", err);
    }

    return err;
}

static int iqs5xx_init(const struct device *dev) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *config = dev->config;

    LOG_INF("=== IQS5XX Driver Initialization Start ===");
    LOG_INF("Device: %p, Data: %p, Config: %p", dev, data, config);

    data->dev = dev;
    data->i2c = DEVICE_DT_GET(DT_BUS(DT_DRV_INST(0)));

    if (!data->i2c) {
        LOG_ERR("Failed to get I2C device");
        return -ENODEV;
    }
    LOG_INF("I2C device: %p", data->i2c);

    // Check if GPIO spec is valid
    LOG_INF("ahmed ::: print config dr: %p", config->dr);
    LOG_INF("ahmed ::: print config: %p", config);

    if (!config->dr.port) {
        LOG_INF("ahmed 22 ::: print config: %p", config);
        LOG_ERR("Data ready GPIO port is NULL - check devicetree configuration");
        return -ENODEV;
    }
    LOG_INF("DR GPIO: port=%p, pin=%d, dt_flags=0x%02x",
            config->dr.port, config->dr.pin, config->dr.dt_flags);

    k_mutex_init(&data->i2c_mutex);
    k_work_init(&data->work, iqs5xx_work_cb);
    LOG_INF("Mutex and work queue initialized");

    // Configure data ready pin
    int ret = gpio_pin_configure_dt(&config->dr, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure data ready pin: %d", ret);
        return ret;
    }
    LOG_INF("Data ready pin configured successfully");

    // Test GPIO pin reading
    int pin_state = gpio_pin_get_dt(&config->dr);
    LOG_INF("Initial DR pin state: %d", pin_state);

    // Initialize interrupt callback
    gpio_init_callback(&data->dr_cb, iqs5xx_gpio_cb, BIT(config->dr.pin));
    LOG_INF("GPIO callback initialized for pin %d", config->dr.pin);

    // Add callback
    ret = gpio_add_callback(config->dr.port, &data->dr_cb);
    if (ret < 0) {
        LOG_ERR("Failed to add GPIO callback: %d", ret);
        return ret;
    }
    LOG_INF("GPIO callback added successfully");

    // Configure data ready interrupt
    ret = gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure interrupt: %d", ret);
        return ret;
    }
    LOG_INF("GPIO interrupt configured successfully");

    // Test I2C communication with a simple read
    LOG_INF("Testing I2C communication...");
    uint8_t test_buf[2];
    ret = iqs5xx_seq_read(dev, ProductNumber_adr, test_buf, 2);
    if (ret < 0) {
        LOG_WRN("I2C test read failed: %d (this might be normal before init)", ret);
    } else {
        LOG_INF("I2C test successful - Product ID: 0x%02x%02x", test_buf[0], test_buf[1]);
    }

    // Initialize device registers
    LOG_INF("Starting register initialization...");
    struct iqs5xx_reg_config iqs5xx_registers = iqs5xx_reg_config_default();
    ret = iqs5xx_registers_init(dev, &iqs5xx_registers);
    if(ret) {
        LOG_ERR("Failed to initialize IQS5xx registers: %d", ret);
        return ret;
    }

    // Final test - try to read some data
    LOG_INF("Performing final communication test...");
    ret = iqs5xx_sample_fetch(dev);
    if (ret < 0) {
        LOG_WRN("Final test failed: %d (might be normal if no touch)", ret);
    } else {
        LOG_INF("Final test successful");
    }

    LOG_INF("=== IQS5XX Driver Initialization Complete ===");
    return 0;
}

// Device instance data
static struct iqs5xx_data iqs5xx_data_0 = {
    .data_ready_handler = NULL
};

// Device configuration from devicetree - SIMPLIFIED
static const struct iqs5xx_config iqs5xx_config_0 = {
    .dr = GPIO_DT_SPEC_GET_OR(DT_DRV_INST(0), dr_gpios, {}),
};

DEVICE_DT_INST_DEFINE(0, iqs5xx_init, NULL, &iqs5xx_data_0, &iqs5xx_config_0,
                      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);
