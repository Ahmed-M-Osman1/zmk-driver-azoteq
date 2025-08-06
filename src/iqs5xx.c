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
#include <zephyr/pm/device.h>
#include <stdlib.h>
#include "iqs5xx.h"

LOG_MODULE_REGISTER(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

static int iqs_regdump_err = 0;

// Default config - RESTORED from power-management branch
struct iqs5xx_reg_config iqs5xx_reg_config_default () {
    struct iqs5xx_reg_config regconf;

    regconf.activeRefreshRate =         5;    // Fast for responsive tracking
    regconf.idleRefreshRate =           20;
    regconf.singleFingerGestureMask =   GESTURE_SINGLE_TAP | GESTURE_TAP_AND_HOLD;
    regconf.multiFingerGestureMask =    GESTURE_TWO_FINGER_TAP | GESTURE_SCROLLG;
    regconf.tapTime =                   100;  // Quick taps
    regconf.tapDistance =               15;   // Sensitive tap detection
    regconf.touchMultiplier =           0;
    regconf.debounce =                  0;    // Minimal debounce for responsiveness
    regconf.i2cTimeout =                2;    // Short timeout
    regconf.filterSettings =            MAV_FILTER | IIR_FILTER;
    regconf.filterDynBottomBeta =        15;  // Less filtering for responsiveness
    regconf.filterDynLowerSpeed =        10;
    regconf.filterDynUpperSpeed =        200;
    regconf.initScrollDistance =        10;

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

    if (ret < 0) {
        LOG_ERR("I2C read failed: addr=0x%04X, len=%d, ret=%d", start, len, ret);
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

    int err = i2c_transfer(data->i2c, msg, 2, AZOTEQ_IQS5XX_ADDR);

    if (err < 0) {
        LOG_ERR("I2C write failed: addr=0x%04X, len=%d, ret=%d", start_addr, num_bytes, err);
    }

    return err;
}

static int iqs5xx_reg_dump (const struct device *dev) {
    LOG_INF("Writing register dump...");
    int ret = iqs5xx_write(dev, IQS5XX_REG_DUMP_START_ADDRESS, _iqs5xx_regdump, IQS5XX_REG_DUMP_SIZE);
    if (ret < 0) {
        LOG_ERR("Register dump failed: %d", ret);
    } else {
        LOG_INF("Register dump successful");
    }
    return ret;
}

// FIXED: Simplified coordinate transformation (single pass, no double transformation)
static void apply_coordinate_transform(const struct device *dev, int16_t *x, int16_t *y) {
    const struct iqs5xx_config *config = dev->config;
    int16_t orig_x = *x;
    int16_t orig_y = *y;

    // Apply rotation first - ONLY if movement is significant enough
    if (abs(orig_x) < 2 && abs(orig_y) < 2) {
        return; // Skip transformation for tiny movements to avoid noise
    }

    if (config->rotate_90) {
        // 90 degrees clockwise: X becomes Y, Y becomes -X
        *x = orig_y;
        *y = -orig_x;
    } else if (config->rotate_180) {
        // 180 degrees: X becomes -X, Y becomes -Y
        *x = -orig_x;
        *y = -orig_y;
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

    // Log significant transformations for debugging
    if (abs(orig_x) > 5 || abs(orig_y) > 5) {
        LOG_DBG("Transform: (%d,%d) -> (%d,%d) [rot90=%d,rot270=%d,inv_x=%d,inv_y=%d]",
                orig_x, orig_y, *x, *y, config->rotate_90, config->rotate_270,
                config->invert_x, config->invert_y);
    }
}

/**
 * @brief Read data from IQS5XX with stable coordinate handling
*/
static int iqs5xx_sample_fetch (const struct device *dev) {
    uint8_t buffer[44];
    struct iqs5xx_data *data = dev->data;

    int res = iqs5xx_seq_read(dev, GestureEvents0_adr, buffer, 44);
    if (res < 0) {
        LOG_ERR("Failed to read data from IQS5XX: %d", res);
        return res;
    }

    // Always close the window
    iqs5xx_write(dev, END_WINDOW, 0, 1);

    // Parse data
    data->raw_data.gestures0 =      buffer[0];
    data->raw_data.gestures1 =      buffer[1];
    data->raw_data.system_info0 =   buffer[2];
    data->raw_data.system_info1 =   buffer[3];
    data->raw_data.finger_count =   buffer[4];

    // Get raw relative coordinates (signed 16-bit)
    int16_t raw_rx = (int16_t)(buffer[5] << 8 | buffer[6]);
    int16_t raw_ry = (int16_t)(buffer[7] << 8 | buffer[8]);

    // FIXED: Apply coordinate transformation ONCE and store result
    apply_coordinate_transform(dev, &raw_rx, &raw_ry);
    data->raw_data.rx = raw_rx;
    data->raw_data.ry = raw_ry;

    // Parse finger data (absolute coordinates)
    for(int i = 0; i < 5; i++) {
        const int p = 9 + (7 * i);
        data->raw_data.fingers[i].ax = buffer[p + 0] << 8 | buffer[p + 1];
        data->raw_data.fingers[i].ay = buffer[p + 2] << 8 | buffer[p + 3];
        data->raw_data.fingers[i].strength = buffer[p + 4] << 8 | buffer[p + 5];
        data->raw_data.fingers[i].area= buffer[p + 6];

        // Apply transformation to absolute coordinates too (for gesture detection)
        if (data->raw_data.fingers[i].strength > 0) {
            int16_t abs_x = (int16_t)data->raw_data.fingers[i].ax;
            int16_t abs_y = (int16_t)data->raw_data.fingers[i].ay;
            apply_coordinate_transform(dev, &abs_x, &abs_y);
            data->raw_data.fingers[i].ax = (uint16_t)MAX(0, abs_x);
            data->raw_data.fingers[i].ay = (uint16_t)MAX(0, abs_y);
        }
    }

    // DEBUG: Log activity when there's something interesting
    if (data->raw_data.finger_count > 0 || data->raw_data.gestures0 || data->raw_data.gestures1 ||
        raw_rx != 0 || raw_ry != 0) {
        LOG_DBG("Activity: fingers=%d, g0=0x%02x, g1=0x%02x, rel=(%d,%d)",
                data->raw_data.finger_count, data->raw_data.gestures0, data->raw_data.gestures1,
                raw_rx, raw_ry);
    }

    return 0;
}

// FIXED: Simplified work callback without excessive error handling that could cause instability
static void iqs5xx_work_cb(struct k_work *work) {
    struct iqs5xx_data *data = CONTAINER_OF(work, struct iqs5xx_data, work);

    if (k_mutex_lock(&data->i2c_mutex, K_MSEC(100)) != 0) {
        LOG_ERR("Failed to acquire I2C mutex");
        return;
    }

    int ret = iqs5xx_sample_fetch(data->dev);

    if (ret == 0) {
        // Success - reset error counter
        data->consecutive_errors = 0;

        if (data->data_ready_handler != NULL) {
            data->data_ready_handler(data->dev, &data->raw_data);
        } else {
            LOG_WRN("No data ready handler set!");
        }
    } else {
        // Simple error handling - don't try to be too clever
        data->consecutive_errors++;
        LOG_WRN("I2C error %d, consecutive errors: %d", ret, data->consecutive_errors);

        // Only try recovery if we have many consecutive errors
        if (data->consecutive_errors >= 20) {
            LOG_ERR("Too many I2C errors, resetting error counter");
            data->consecutive_errors = 0;
        }
    }

    k_mutex_unlock(&data->i2c_mutex);
}

/**
 * @brief GPIO callback - SIMPLIFIED for stability
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
    LOG_INF("Trigger handler set: %p", (void*)handler);
    return 0;
}

/**
 * @brief RESTORED register initialization from power-management branch
 */
int iqs5xx_registers_init (const struct device *dev, const struct iqs5xx_reg_config *config) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *conf = dev->config;

    LOG_INF("Starting register initialization...");

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

    LOG_INF("Data ready pin active after %d iterations", timeout);

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

    // Configure settings
    int err = 0;
    uint8_t wbuff[16];

    LOG_INF("Writing configuration registers...");

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

    // Set gesture enables
    ret = iqs5xx_write(dev, SFGestureEnable_adr, &config->singleFingerGestureMask, 1);
    if (ret < 0) {
        err |= ret;
        LOG_ERR("Failed to set single finger gestures: %d", ret);
    }

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

// RESTORED initialization from power-management branch
static int iqs5xx_init(const struct device *dev) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *config = dev->config;

    LOG_INF("=== IQS5XX Initialization Starting ===");

    data->dev = dev;
    data->i2c = DEVICE_DT_GET(DT_BUS(DT_DRV_INST(0)));

    if (!data->i2c) {
        LOG_ERR("I2C device not found");
        return -ENODEV;
    }

    LOG_INF("I2C device found: %s", data->i2c->name);

    // Check if I2C device is ready
    if (!device_is_ready(data->i2c)) {
        LOG_ERR("I2C device not ready");
        return -ENODEV;
    }

    LOG_INF("I2C device is ready");

    // Check if GPIO was properly initialized
    if (!device_is_ready(config->dr.port)) {
        if (config->dr.port == NULL) {
            LOG_ERR("Data ready GPIO not configured");
            return -ENODEV;
        } else {
            LOG_ERR("Data ready GPIO device not ready");
            return -ENODEV;
        }
    }

    LOG_INF("Data ready GPIO configured: port=%s, pin=%d",
            config->dr.port->name, config->dr.pin);

    // Log configuration for debugging
    LOG_INF("Coordinate config: rot90=%d, rot180=%d, rot270=%d, inv_x=%d, inv_y=%d, sens=%d",
            config->rotate_90, config->rotate_180, config->rotate_270,
            config->invert_x, config->invert_y, config->sensitivity);

    k_mutex_init(&data->i2c_mutex);
    k_work_init(&data->work, iqs5xx_work_cb);

    // Configure data ready pin
    int ret = gpio_pin_configure_dt(&config->dr, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure data ready pin: %d", ret);
        return ret;
    }

    LOG_INF("Data ready pin configured as input");

    // Initialize interrupt callback
    gpio_init_callback(&data->dr_cb, iqs5xx_gpio_cb, BIT(config->dr.pin));

    // Add callback
    ret = gpio_add_callback(config->dr.port, &data->dr_cb);
    if (ret < 0) {
        LOG_ERR("Failed to add GPIO callback: %d", ret);
        return ret;
    }

    LOG_INF("GPIO callback added");

    // Test I2C communication with a simple read
    uint8_t test_buf[2];
    ret = iqs5xx_seq_read(dev, ProductNumber_adr, test_buf, 2);
    if (ret < 0) {
        LOG_ERR("Failed to communicate with IQS5XX: %d", ret);
        return ret;
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

    LOG_INF("Data ready interrupt configured");

    // Final test - try to read some data
    ret = iqs5xx_sample_fetch(dev);
    if (ret < 0) {
        LOG_WRN("Initial sample fetch failed: %d (this may be normal)", ret);
    }

    LOG_INF("=== IQS5XX trackpad initialized successfully ===");
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
    .invert_x = DT_INST_PROP(0, invert_x),
    .invert_y = DT_INST_PROP(0, invert_y),
    .rotate_90 = DT_INST_PROP(0, rotate_90),
    .rotate_180 = DT_INST_PROP(0, rotate_180),
    .rotate_270 = DT_INST_PROP(0, rotate_270),
    .sensitivity = DT_INST_PROP_OR(0, sensitivity, 128),
};

#ifdef CONFIG_PM_DEVICE
// Simple power management
static int iqs5xx_pm_action(const struct device *dev, enum pm_device_action action) {
    LOG_INF("Power management action: %d", action);

    switch (action) {
        case PM_DEVICE_ACTION_SUSPEND:
            // Device will be suspended - minimal action needed
            return 0;

        case PM_DEVICE_ACTION_RESUME:
            // Device resumed - re-initialize if needed
            struct iqs5xx_reg_config config = iqs5xx_reg_config_default();
            return iqs5xx_registers_init(dev, &config);

        default:
            return 0;
    }
}
PM_DEVICE_DT_INST_DEFINE(0, iqs5xx_pm_action);
#define PM_DEVICE_INST PM_DEVICE_DT_INST_GET(0)
#else
#define PM_DEVICE_INST NULL
#endif

DEVICE_DT_INST_DEFINE(0, iqs5xx_init, PM_DEVICE_INST,
                      &iqs5xx_data_0, &iqs5xx_config_0,
                      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);
