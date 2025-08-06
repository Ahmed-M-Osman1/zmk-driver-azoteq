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

// Forward declaration
static void apply_coordinate_transform(const struct device *dev, int16_t *x, int16_t *y);

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

    int ret = i2c_write_read(data->i2c, AZOTEQ_IQS5XX_ADDR, &nstart, sizeof(nstart), read_buf, len);
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

    int err = i2c_transfer(data->i2c, msg, 2, AZOTEQ_IQS5XX_ADDR);
    return err;
}

static int iqs5xx_reg_dump (const struct device *dev) {
    int ret = iqs5xx_write(dev, IQS5XX_REG_DUMP_START_ADDRESS, _iqs5xx_regdump, IQS5XX_REG_DUMP_SIZE);
    return ret;
}

static void apply_coordinate_transform(const struct device *dev, int16_t *x, int16_t *y) {
    const struct iqs5xx_config *config = dev->config;
    int16_t orig_x = *x;
    int16_t orig_y = *y;

    // Apply rotation first
    if (config->rotate_90) {
        // 90 degrees clockwise: X becomes Y, Y becomes -X
        *x = orig_y;
        *y = -orig_x;
    } else if (config->rotate_270) {
        // 270 degrees clockwise: X becomes -Y, Y becomes X
        *x = -orig_y;
        *y = orig_x;
    }

    // Apply axis inversion after rotation
    if (config->invert_x) {
        *x = -*x;
    }
    if (config->invert_y) {
        *y = -*y;
    }
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
        LOG_ERR("Failed to read data from IQS5XX: %d", res);
        return res;
    }

    // Parse data
    data->raw_data.gestures0 =      buffer[0];
    data->raw_data.gestures1 =      buffer[1];
    data->raw_data.system_info0 =   buffer[2];
    data->raw_data.system_info1 =   buffer[3];
    data->raw_data.finger_count =   buffer[4];

    // Get raw relative coordinates
    int16_t raw_rx = buffer[5] << 8 | buffer[6];
    int16_t raw_ry = buffer[7] << 8 | buffer[8];

    // Apply coordinate transformation
    apply_coordinate_transform(dev, &raw_rx, &raw_ry);

    // Store transformed coordinates
    data->raw_data.rx = raw_rx;
    data->raw_data.ry = raw_ry;

    // Parse finger data - also transform absolute coordinates if needed
    for(int i = 0; i < 5; i++) {
        const int p = 9 + (7 * i);
        data->raw_data.fingers[i].ax = buffer[p + 0] << 8 | buffer[p + 1];
        data->raw_data.fingers[i].ay = buffer[p + 2] << 8 | buffer[p + 3];
        data->raw_data.fingers[i].strength = buffer[p + 4] << 8 | buffer[p + 5];
        data->raw_data.fingers[i].area= buffer[p + 6];
    }

    return 0;
}

static void iqs5xx_work_cb(struct k_work *work) {
    struct iqs5xx_data *data = CONTAINER_OF(work, struct iqs5xx_data, work);

    k_mutex_lock(&data->i2c_mutex, K_MSEC(1000));
    int ret = iqs5xx_sample_fetch(data->dev);

    if (ret == 0) {
        // Success - reset error counter
        data->consecutive_errors = 0;

        if (data->data_ready_handler != NULL) {
            data->data_ready_handler(data->dev, &data->raw_data);
        }
    } else {
        // I2C Error handling
        data->consecutive_errors++;
        int64_t current_time = k_uptime_get();

        // If we have too many consecutive errors, try recovery
        if (data->consecutive_errors >= 15) {
            LOG_WRN("Too many I2C errors, attempting recovery");
            // Reset error counter
            data->consecutive_errors = 0;
            data->last_error_time = current_time;

            k_mutex_unlock(&data->i2c_mutex);

            // Get config for GPIO access
            const struct iqs5xx_config *config = data->dev->config;

            // Disable interrupts temporarily
            gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_DISABLE);

            // Wait for device to settle
            k_msleep(200);

            // Try a simple reset by writing to system control
            uint8_t reset_cmd = RESET_TP;
            iqs5xx_write(data->dev, SystemControl1_adr, &reset_cmd, 1);
            iqs5xx_write(data->dev, END_WINDOW, 0, 1);

            // Wait after reset
            k_msleep(100);

            // Re-enable interrupts
            gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_EDGE_TO_ACTIVE);

            return;
        }

        // If errors persist for too long, disable temporarily
        if ((current_time - data->last_error_time > 3000) && (data->consecutive_errors > 5)) {
            const struct iqs5xx_config *config = data->dev->config;
            gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_DISABLE);

            k_mutex_unlock(&data->i2c_mutex);
            k_msleep(500);
            k_mutex_lock(&data->i2c_mutex, K_MSEC(1000));

            gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_EDGE_TO_ACTIVE);
            data->consecutive_errors = 0;
            data->last_error_time = current_time;
        }
    }

    k_mutex_unlock(&data->i2c_mutex);
}

/**
 * @brief Called when data ready pin goes active. Submits work to workqueue.
 */
static void iqs5xx_gpio_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    struct iqs5xx_data *data = CONTAINER_OF(cb, struct iqs5xx_data, dr_cb);

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
    int timeout = 0;
    while(!gpio_pin_get_dt(&conf->dr) && timeout < 1000) {
        k_usleep(200);
        timeout++;
    }

    if (timeout >= 1000) {
        LOG_ERR("Timeout waiting for data ready pin");
        k_mutex_unlock(&data->i2c_mutex);
        return -ETIMEDOUT;
    }

    // Reset device
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
        LOG_ERR("Failed to write register dump: %d", iqs_regdump_err);
        k_mutex_unlock(&data->i2c_mutex);
        return iqs_regdump_err;
    }

    // Wait for ready after regdump
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

    int err = 0;
    uint8_t wbuff[16];

    // Set active refresh rate
    *((uint16_t*)wbuff) = SWPEND16(config->activeRefreshRate);
    ret = iqs5xx_write(dev, ActiveRR_adr, wbuff, 2);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set active refresh rate: %d", ret);
    }

    // Set idle refresh rate
    *((uint16_t*)wbuff) = SWPEND16(config->idleRefreshRate);
    ret = iqs5xx_write(dev, IdleRR_adr, wbuff, 2);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set idle refresh rate: %d", ret);
    }

    // Set single finger gestures
    ret = iqs5xx_write(dev, SFGestureEnable_adr, &config->singleFingerGestureMask, 1);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set single finger gestures: %d", ret);
    }

    // Set multi finger gestures
    ret = iqs5xx_write(dev, MFGestureEnable_adr, &config->multiFingerGestureMask, 1);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set multi finger gestures: %d", ret);
    }

    // Set tap time
    *((uint16_t*)wbuff) = SWPEND16(config->tapTime);
    ret = iqs5xx_write(dev, TapTime_adr, wbuff, 2);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set tap time: %d", ret);
    }

    // Set tap distance
    *((uint16_t*)wbuff) = SWPEND16(config->tapDistance);
    ret = iqs5xx_write(dev, TapDistance_adr, wbuff, 2);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set tap distance: %d", ret);
    }

    // Set touch multiplier
    ret = iqs5xx_write(dev, GlobalTouchSet_adr, &config->touchMultiplier, 1);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set touch multiplier: %d", ret);
    }

    // Set debounce settings
    ret = iqs5xx_write(dev, ProxDb_adr, &config->debounce, 1);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set prox debounce: %d", ret);
    }

    ret = iqs5xx_write(dev, TouchSnapDb_adr, &config->debounce, 1);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set touch snap debounce: %d", ret);
    }

    // Set noise reduction
    wbuff[0] = 0;
    ret = iqs5xx_write(dev, HardwareSettingsA_adr, wbuff, 1);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set hardware settings: %d", ret);
    }

    // Set i2c timeout
    ret = iqs5xx_write(dev, I2CTimeout_adr, &config->i2cTimeout, 1);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set i2c timeout: %d", ret);
    }

    // Set filter settings
    ret = iqs5xx_write(dev, FilterSettings0_adr, &config->filterSettings, 1);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set filter settings: %d", ret);
    }

    ret = iqs5xx_write(dev, DynamicBottomBeta_adr, &config->filterDynBottomBeta, 1);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set dynamic bottom beta: %d", ret);
    }

    ret = iqs5xx_write(dev, DynamicLowerSpeed_adr, &config->filterDynLowerSpeed, 1);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set dynamic lower speed: %d", ret);
    }

    *((uint16_t*)wbuff) = SWPEND16(config->filterDynUpperSpeed);
    ret = iqs5xx_write(dev, DynamicUpperSpeed_adr, wbuff, 2);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set dynamic upper speed: %d", ret);
    }

    // Set initial scroll distance
    *((uint16_t*)wbuff) = SWPEND16(config->initScrollDistance);
    ret = iqs5xx_write(dev, ScrollInitDistance_adr, wbuff, 2);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set scroll init distance: %d", ret);
    }

    // Terminate transaction
    iqs5xx_write(dev, END_WINDOW, 0, 1);

    k_mutex_unlock(&data->i2c_mutex);

    if (err == 0) {
        LOG_INF("IQS5XX registers initialized successfully");
        return 0;
    } else {
        LOG_ERR("Some register writes failed, error: %d", err);
        return err;
    }
}

static int iqs5xx_init(const struct device *dev) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *config = dev->config;

    data->dev = dev;
    data->i2c = DEVICE_DT_GET(DT_BUS(DT_DRV_INST(0)));

    if (!data->i2c) {
        LOG_ERR("I2C device not found");
        return -ENODEV;
    }

    // Check if I2C device is ready
    if (!device_is_ready(data->i2c)) {
        LOG_ERR("I2C device not ready");
        return -ENODEV;
    }

    // Check if GPIO was properly initialized
    if (!device_is_ready(config->dr.port)) {
        // Check if it's an empty GPIO spec (fallback case)
        if (config->dr.port == NULL) {
            LOG_ERR("Data ready GPIO not configured");
            return -ENODEV;
        } else {
            LOG_ERR("Data ready GPIO device not ready");
            return -ENODEV;
        }
    }

    k_mutex_init(&data->i2c_mutex);
    k_work_init(&data->work, iqs5xx_work_cb);

    // Configure data ready pin
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

    // Test I2C communication with a simple read
    uint8_t test_buf[2];
    ret = iqs5xx_seq_read(dev, ProductNumber_adr, test_buf, 2);
    if (ret < 0) {
        LOG_ERR("Failed to communicate with IQS5XX: %d", ret);
        return ret; // Return the actual error, don't continue
    }

    uint16_t product_number = (test_buf[0] << 8) | test_buf[1];
    LOG_INF("IQS5XX Product Number: 0x%04X", product_number);

    // Initialize device registers
    struct iqs5xx_reg_config iqs5xx_registers = iqs5xx_reg_config_default();
    ret = iqs5xx_registers_init(dev, &iqs5xx_registers);
    if(ret) {
        LOG_ERR("Failed to initialize registers: %d", ret);
        return ret;
    }

    // Configure data ready interrupt AFTER successful initialization
    ret = gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure data ready interrupt: %d", ret);
        return ret;
    }

    // Final test - try to read some data
    ret = iqs5xx_sample_fetch(dev);
    if (ret < 0) {
        LOG_WRN("Initial sample fetch failed: %d (this may be normal)", ret);
        // Don't fail initialization for this
    }

    LOG_INF("IQS5XX trackpad initialized successfully");
    return 0;
}

// Device instance data
static struct iqs5xx_data iqs5xx_data_0 = {
    .data_ready_handler = NULL,
    .consecutive_errors = 0,
    .last_error_time = 0
};

// Device configuration from devicetree
static const struct iqs5xx_config iqs5xx_config_0 = {
    .dr = GPIO_DT_SPEC_GET_OR(DT_DRV_INST(0), dr_gpios, {}),
    .rotate_90 = DT_INST_PROP(0, rotate_90),
    .rotate_270 = DT_INST_PROP(0, rotate_270),
    .invert_x = DT_INST_PROP(0, invert_x),
    .invert_y = DT_INST_PROP(0, invert_y),
    .sensitivity = DT_INST_PROP_OR(0, sensitivity, 128),
    .refresh_rate_active = DT_INST_PROP_OR(0, refresh_rate_active, 5),
    .refresh_rate_idle = DT_INST_PROP_OR(0, refresh_rate_idle, 20),
};

DEVICE_DT_INST_DEFINE(0, iqs5xx_init, NULL, &iqs5xx_data_0, &iqs5xx_config_0,
                      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);
