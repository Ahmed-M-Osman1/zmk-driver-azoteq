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

    regconf.activeRefreshRate =         8;    // Slightly slower for stability
    regconf.idleRefreshRate =           30;   // Better power management
    regconf.singleFingerGestureMask =   GESTURE_SINGLE_TAP | GESTURE_TAP_AND_HOLD;
    regconf.multiFingerGestureMask =    GESTURE_TWO_FINGER_TAP | GESTURE_SCROLLG;
    regconf.tapTime =                   120;  // Slightly longer for reliability
    regconf.tapDistance =               20;   // Increased for less sensitivity
    regconf.touchMultiplier =           0;
    regconf.debounce =                  1;    // Add debounce to prevent jumps
    regconf.i2cTimeout =                5;    // Increased timeout
    regconf.filterSettings =            MAV_FILTER | IIR_FILTER;
    regconf.filterDynBottomBeta =        25;  // More filtering to reduce noise
    regconf.filterDynLowerSpeed =        15;  // More conservative filtering
    regconf.filterDynUpperSpeed =        150; // Reduced for smoother tracking
    regconf.initScrollDistance =        15;   // Slightly increased

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
        LOG_DBG("I2C read: addr=0x%04x, len=%d", start, len);
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
        LOG_DBG("I2C write: addr=0x%04x, len=%d", start_addr, num_bytes);
    }
    return err;
}

static int iqs5xx_reg_dump (const struct device *dev) {
    int ret = iqs5xx_write(dev, IQS5XX_REG_DUMP_START_ADDRESS, _iqs5xx_regdump, IQS5XX_REG_DUMP_SIZE);
    return ret;
}

/**
 * @brief Read data from IQS5XX with improved error handling
*/
static int iqs5xx_sample_fetch (const struct device *dev) {
    uint8_t buffer[44];
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *config = dev->config;

    int res = iqs5xx_seq_read(dev, GestureEvents0_adr, buffer, 44);
    if (res < 0) {
        LOG_DBG("Sample fetch failed: %d", res);
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

    // Parse relative movement (signed 16-bit values)
    int16_t raw_rx = (int16_t)(buffer[5] << 8 | buffer[6]);
    int16_t raw_ry = (int16_t)(buffer[7] << 8 | buffer[8]);

    // Apply coordinate transformation to relative movement
    struct coord_transform rel_transformed = apply_coordinate_transform(raw_rx, raw_ry, config);
    data->raw_data.rx = rel_transformed.x;
    data->raw_data.ry = rel_transformed.y;

    LOG_DBG("Coordinate transform: (%d,%d) -> (%d,%d) [rot90=%d, rot270=%d, inv_x=%d, inv_y=%d]",
            raw_rx, raw_ry, rel_transformed.x, rel_transformed.y,
            config->rotate_90, config->rotate_270, config->invert_x, config->invert_y);

    for(int i = 0; i < 5; i++) {
        const int p = 9 + (7 * i);
        data->raw_data.fingers[i].ax = buffer[p + 0] << 8 | buffer[p + 1];
        data->raw_data.fingers[i].ay = buffer[p + 2] << 8 | buffer[p + 3];
        data->raw_data.fingers[i].strength = buffer[p + 4] << 8 | buffer[p + 5];
        data->raw_data.fingers[i].area= buffer[p + 6];

        // Apply finger coordinate transformation
        apply_finger_transform(&data->raw_data.fingers[i], config);

        // Log finger data for debugging
        if (data->raw_data.fingers[i].strength > 0) {
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

    if (k_mutex_lock(&data->i2c_mutex, K_MSEC(100)) != 0) {
        LOG_WRN("Failed to acquire I2C mutex");
        return;
    }

    int ret = iqs5xx_sample_fetch(data->dev);

    if (ret == 0) {
        // Success - reset error counter
        data->consecutive_errors = 0;

        if (data->data_ready_handler != NULL) {
            data->data_ready_handler(data->dev, &data->raw_data);
        }
    } else {
        // I2C Error handling - IMPROVED
        data->consecutive_errors++;
        int64_t current_time = k_uptime_get();

        LOG_WRN("I2C error %d, consecutive errors: %d", ret, data->consecutive_errors);

        // If we have too many consecutive errors, try recovery
        if (data->consecutive_errors >= 10) { // Reduced threshold
            LOG_ERR("Too many I2C errors, attempting recovery");

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

            // Try device recovery sequence
            uint8_t reset_cmd = RESET_TP;
            int reset_ret = iqs5xx_write(data->dev, SystemControl1_adr, &reset_cmd, 1);
            if (reset_ret == 0) {
                iqs5xx_write(data->dev, END_WINDOW, 0, 1);

                // Wait after reset
                k_msleep(100);

                // Re-initialize registers after reset
                struct iqs5xx_reg_config iqs5xx_registers = iqs5xx_reg_config_default();
                int init_ret = iqs5xx_registers_init(data->dev, &iqs5xx_registers);
                if (init_ret != 0) {
                    LOG_ERR("Failed to re-initialize registers after reset: %d", init_ret);
                }
            } else {
                LOG_ERR("Failed to reset device: %d", reset_ret);
            }

            // Re-enable interrupts
            gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_EDGE_TO_ACTIVE);

            return;
        }

        // If errors persist for too long, disable temporarily
        if ((current_time - data->last_error_time > 2000) && (data->consecutive_errors > 3)) {
            LOG_WRN("Temporary disable due to persistent errors");

            const struct iqs5xx_config *config = data->dev->config;
            gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_DISABLE);

            k_mutex_unlock(&data->i2c_mutex);
            k_msleep(300);
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

    LOG_DBG("GPIO interrupt: port=0x%p, pins=0x%08x", port, pins);
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
 * @brief Enhanced register initialization with wake-from-sleep recovery
 */
int iqs5xx_registers_init (const struct device *dev, const struct iqs5xx_reg_config *config) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *conf = dev->config;

    LOG_INF("Initializing IQS5XX registers");

    if (k_mutex_lock(&data->i2c_mutex, K_MSEC(5000)) != 0) {
        LOG_ERR("Failed to acquire mutex for register init");
        return -ETIMEDOUT;
    }

    // Extended wait for dataready - important for wake-from-sleep
    int timeout = 0;
    while(!gpio_pin_get_dt(&conf->dr) && timeout < 2000) { // Increased timeout
        k_usleep(500); // Longer intervals
        timeout++;
    }

    if (timeout >= 2000) {
        LOG_ERR("Timeout waiting for data ready pin");
        k_mutex_unlock(&data->i2c_mutex);
        return -ETIMEDOUT;
    }

    // Reset device - CRITICAL for wake recovery
    uint8_t buf = RESET_TP;
    int ret = iqs5xx_write(dev, SystemControl1_adr, &buf, 1);
    if (ret < 0) {
        LOG_ERR("Failed to reset device: %d", ret);
        k_mutex_unlock(&data->i2c_mutex);
        return ret;
    }

    iqs5xx_write(dev, END_WINDOW, 0, 1);
    k_msleep(50); // Longer reset time

    // Wait for ready after reset - CRITICAL
    timeout = 0;
    while(!gpio_pin_get_dt(&conf->dr) && timeout < 2000) {
        k_usleep(500);
        timeout++;
    }

    if (timeout >= 2000) {
        LOG_ERR("Timeout after reset");
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
    while(!gpio_pin_get_dt(&conf->dr) && timeout < 2000) {
        k_usleep(500);
        timeout++;
    }

    if (timeout >= 2000) {
        LOG_ERR("Timeout after regdump");
        k_mutex_unlock(&data->i2c_mutex);
        return -ETIMEDOUT;
    }

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
    wbuff[0] = ND_ENABLE; // Enable noise detection
    ret = iqs5xx_write(dev, HardwareSettingsA_adr, wbuff, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set hardware settings A: %d", ret);
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
        LOG_ERR("Failed to set scroll init distance: %d", ret);
        err |= ret;
    }

    // CRITICAL: Set coordinate system settings to ensure transform works
    uint8_t xy_config = 0;
    // Don't set any hardware transformation flags - we do it in software
    ret = iqs5xx_write(dev, XYConfig0_adr, &xy_config, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set XY config: %d", ret);
        err |= ret;
    }

    // Terminate transaction
    iqs5xx_write(dev, END_WINDOW, 0, 1);

    k_mutex_unlock(&data->i2c_mutex);

    if (err == 0) {
        LOG_INF("IQS5XX registers initialized successfully");
        return 0;
    } else {
        LOG_ERR("IQS5XX register initialization failed with error: %d", err);
        return err;
    }
}

/**
 * @brief Device power management - handle sleep/wake
 */
static int iqs5xx_pm_action(const struct device *dev, enum pm_device_action action) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *config = dev->config;
    int ret = 0;

    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        LOG_INF("IQS5XX suspending");
        // Disable interrupts
        gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_DISABLE);

        // Put device to sleep
        if (k_mutex_lock(&data->i2c_mutex, K_MSEC(1000)) == 0) {
            uint8_t suspend_cmd = SUSPEND;
            iqs5xx_write(dev, SystemControl1_adr, &suspend_cmd, 1);
            iqs5xx_write(dev, END_WINDOW, 0, 1);
            k_mutex_unlock(&data->i2c_mutex);
        }
        break;

    case PM_DEVICE_ACTION_RESUME:
        LOG_INF("IQS5XX resuming");
        // Re-initialize the device completely
        struct iqs5xx_reg_config iqs5xx_registers = iqs5xx_reg_config_default();
        ret = iqs5xx_registers_init(dev, &iqs5xx_registers);
        if (ret == 0) {
            // Re-enable interrupts
            gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_EDGE_TO_ACTIVE);
            LOG_INF("IQS5XX resume successful");
        } else {
            LOG_ERR("IQS5XX resume failed: %d", ret);
        }
        break;

    default:
        ret = -ENOTSUP;
        break;
    }

    return ret;
}

static int iqs5xx_init(const struct device *dev) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *config = dev->config;

    LOG_INF("Initializing IQS5XX trackpad driver");

    data->dev = dev;
    data->i2c = DEVICE_DT_GET(DT_BUS(DT_DRV_INST(0)));

    if (!data->i2c) {
        LOG_ERR("I2C device not found");
        return -ENODEV;
    }

    if (!device_is_ready(data->i2c)) {
        LOG_ERR("I2C device not ready");
        return -ENODEV;
    }

    // Check GPIO configuration
    if (!device_is_ready(config->dr.port)) {
        if (config->dr.port == NULL) {
            LOG_ERR("Data ready GPIO not configured in devicetree");
            return -ENODEV;
        } else {
            LOG_ERR("Data ready GPIO device not ready");
            return -ENODEV;
        }
    }

    LOG_INF("IQS5XX config: inv_x=%d, inv_y=%d, rot90=%d, rot180=%d, rot270=%d, sens=%d",
            config->invert_x, config->invert_y, config->rotate_90,
            config->rotate_180, config->rotate_270, config->sensitivity);

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
        LOG_WRN("Initial I2C test failed: %d (will continue anyway)", ret);
    } else {
        uint16_t product_number = (test_buf[0] << 8) | test_buf[1];
        LOG_INF("IQS5XX Product Number: 0x%04x", product_number);
    }

    // Initialize device registers
    struct iqs5xx_reg_config iqs5xx_registers = iqs5xx_reg_config_default();
    ret = iqs5xx_registers_init(dev, &iqs5xx_registers);
    if(ret) {
        LOG_ERR("Failed to initialize registers: %d", ret);
        return ret;
    }

    // Configure data ready interrupt AFTER initialization
    ret = gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure data ready interrupt: %d", ret);
        return ret;
    }

    // Final test - try to read some data
    ret = iqs5xx_sample_fetch(dev);
    if (ret < 0) {
        LOG_WRN("Initial sample fetch failed: %d (will continue anyway)", ret);
    }

    LOG_INF("IQS5XX initialization complete");
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

PM_DEVICE_DT_INST_DEFINE(0, iqs5xx_pm_action);

DEVICE_DT_INST_DEFINE(0, iqs5xx_init, PM_DEVICE_DT_INST_GET(0),
                      &iqs5xx_data_0, &iqs5xx_config_0,
                      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);
