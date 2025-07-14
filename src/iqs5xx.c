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

// Default config
struct iqs5xx_reg_config iqs5xx_reg_config_default () {
    struct iqs5xx_reg_config regconf;

    regconf.activeRefreshRate =         8;
    regconf.idleRefreshRate =           30;
    regconf.singleFingerGestureMask =   GESTURE_SINGLE_TAP | GESTURE_TAP_AND_HOLD;
    regconf.multiFingerGestureMask =    GESTURE_TWO_FINGER_TAP | GESTURE_SCROLLG;
    regconf.tapTime =                   120;
    regconf.tapDistance =               20;
    regconf.touchMultiplier =           0;
    regconf.debounce =                  1;
    regconf.i2cTimeout =                5;
    regconf.filterSettings =            MAV_FILTER | IIR_FILTER;
    regconf.filterDynBottomBeta =        25;
    regconf.filterDynLowerSpeed =        15;
    regconf.filterDynUpperSpeed =        150;
    regconf.initScrollDistance =        15;

    return regconf;
}

/**
 * @brief Read from the iqs550 chip via i2c
 */
static int iqs5xx_seq_read(const struct device *dev, const uint16_t start, uint8_t *read_buf,
                           const uint8_t len) {
    const struct iqs5xx_data *data = dev->data;
    uint16_t nstart = (start << 8 ) | (start >> 8);

    LOG_DBG("I2C read attempt: addr=0x%04x, len=%d", start, len);
    int ret = i2c_write_read(data->i2c, AZOTEQ_IQS5XX_ADDR, &nstart, sizeof(nstart), read_buf, len);
    if (ret < 0) {
        LOG_ERR("I2C read FAILED: addr=0x%04x, len=%d, error=%d", start, len, ret);
    } else {
        LOG_DBG("I2C read SUCCESS: addr=0x%04x, len=%d", start, len);
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

    LOG_DBG("I2C write attempt: addr=0x%04x, len=%d", start_addr, num_bytes);
    int err = i2c_transfer(data->i2c, msg, 2, AZOTEQ_IQS5XX_ADDR);
    if (err < 0) {
        LOG_ERR("I2C write FAILED: addr=0x%04x, len=%d, error=%d", start_addr, num_bytes, err);
    } else {
        LOG_DBG("I2C write SUCCESS: addr=0x%04x, len=%d", start_addr, num_bytes);
    }
    return err;
}

static int iqs5xx_reg_dump (const struct device *dev) {
    LOG_INF("Writing register dump");
    int ret = iqs5xx_write(dev, IQS5XX_REG_DUMP_START_ADDRESS, _iqs5xx_regdump, IQS5XX_REG_DUMP_SIZE);
    LOG_INF("Register dump write result: %d", ret);
    return ret;
}

/**
 * @brief Read data from IQS5XX with improved error handling
*/
static int iqs5xx_sample_fetch (const struct device *dev) {
    uint8_t buffer[44];
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *config = dev->config;

    LOG_DBG("Sample fetch start");
    int res = iqs5xx_seq_read(dev, GestureEvents0_adr, buffer, 44);
    if (res < 0) {
        LOG_ERR("Sample fetch failed: %d", res);
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

    for(int i = 0; i < 5; i++) {
        const int p = 9 + (7 * i);
        data->raw_data.fingers[i].ax = buffer[p + 0] << 8 | buffer[p + 1];
        data->raw_data.fingers[i].ay = buffer[p + 2] << 8 | buffer[p + 3];
        data->raw_data.fingers[i].strength = buffer[p + 4] << 8 | buffer[p + 5];
        data->raw_data.fingers[i].area= buffer[p + 6];

        // Apply finger coordinate transformation
        apply_finger_transform(&data->raw_data.fingers[i], config);
    }

    LOG_DBG("Sample fetch complete: fingers=%d, g0=0x%02x, g1=0x%02x",
            data->raw_data.finger_count, data->raw_data.gestures0, data->raw_data.gestures1);
    return 0;
}

static void iqs5xx_work_cb(struct k_work *work) {
    struct iqs5xx_data *data = CONTAINER_OF(work, struct iqs5xx_data, work);

    LOG_DBG("Work callback triggered");

    if (k_mutex_lock(&data->i2c_mutex, K_MSEC(100)) != 0) {
        LOG_ERR("Failed to acquire I2C mutex");
        return;
    }

    int ret = iqs5xx_sample_fetch(data->dev);

    if (ret == 0) {
        // Success - reset error counter
        data->consecutive_errors = 0;

        if (data->data_ready_handler != NULL) {
            LOG_DBG("Calling data ready handler");
            data->data_ready_handler(data->dev, &data->raw_data);
        } else {
            LOG_WRN("No data ready handler set!");
        }
    } else {
        // I2C Error handling
        data->consecutive_errors++;
        LOG_ERR("I2C error %d, consecutive errors: %d", ret, data->consecutive_errors);
    }

    k_mutex_unlock(&data->i2c_mutex);
}

/**
 * @brief Called when data ready pin goes active. Submits work to workqueue.
 */
static void iqs5xx_gpio_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    struct iqs5xx_data *data = CONTAINER_OF(cb, struct iqs5xx_data, dr_cb);

    LOG_INF("GPIO interrupt triggered! port=0x%p, pins=0x%08x", port, pins);
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
 * @brief Enhanced register initialization with wake-from-sleep recovery
 */
int iqs5xx_registers_init (const struct device *dev, const struct iqs5xx_reg_config *config) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *conf = dev->config;

    LOG_INF("=== Starting IQS5XX register initialization ===");

    if (k_mutex_lock(&data->i2c_mutex, K_MSEC(5000)) != 0) {
        LOG_ERR("Failed to acquire mutex for register init");
        return -ETIMEDOUT;
    }

    // Check GPIO state
    int gpio_state = gpio_pin_get_dt(&conf->dr);
    LOG_INF("Data ready pin state: %d", gpio_state);

    // Extended wait for dataready
    int timeout = 0;
    while(!gpio_pin_get_dt(&conf->dr) && timeout < 2000) {
        k_usleep(500);
        timeout++;
        if (timeout % 100 == 0) {
            LOG_INF("Waiting for data ready pin... (%d/2000)", timeout);
        }
    }

    if (timeout >= 2000) {
        LOG_ERR("TIMEOUT waiting for data ready pin!");
        k_mutex_unlock(&data->i2c_mutex);
        return -ETIMEDOUT;
    }

    LOG_INF("Data ready pin active after %d iterations", timeout);

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
    k_msleep(50);

    // Wait for ready after reset
    timeout = 0;
    while(!gpio_pin_get_dt(&conf->dr) && timeout < 2000) {
        k_usleep(500);
        timeout++;
    }

    if (timeout >= 2000) {
        LOG_ERR("TIMEOUT after reset");
        k_mutex_unlock(&data->i2c_mutex);
        return -ETIMEDOUT;
    }

    LOG_INF("Device ready after reset");

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
        LOG_ERR("TIMEOUT after regdump");
        k_mutex_unlock(&data->i2c_mutex);
        return -ETIMEDOUT;
    }

    LOG_INF("Device ready after regdump");

    // Configure basic settings (simplified for diagnostic)
    int err = 0;
    uint8_t wbuff[16];

    // Set active refresh rate
    *((uint16_t*)wbuff) = SWPEND16(config->activeRefreshRate);
    ret = iqs5xx_write(dev, ActiveRR_adr, wbuff, 2);
    if (ret < 0) {
        LOG_ERR("Failed to set active refresh rate: %d", ret);
        err |= ret;
    }

    // Set gesture enables
    ret = iqs5xx_write(dev, SFGestureEnable_adr, &config->singleFingerGestureMask, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set single finger gestures: %d", ret);
        err |= ret;
    }

    ret = iqs5xx_write(dev, MFGestureEnable_adr, &config->multiFingerGestureMask, 1);
    if (ret < 0) {
        LOG_ERR("Failed to set multi finger gestures: %d", ret);
        err |= ret;
    }

    // Terminate transaction
    iqs5xx_write(dev, END_WINDOW, 0, 1);

    k_mutex_unlock(&data->i2c_mutex);

    if (err == 0) {
        LOG_INF("=== IQS5XX registers initialized successfully ===");
        return 0;
    } else {
        LOG_ERR("=== IQS5XX register initialization FAILED: %d ===", err);
        return err;
    }
}

static int iqs5xx_init(const struct device *dev) {
    struct iqs5xx_data *data = dev->data;
    const struct iqs5xx_config *config = dev->config;

    LOG_INF("=== STARTING IQS5XX INITIALIZATION ===");
    printk("IQS5XX: Starting initialization\n");

    data->dev = dev;
    data->i2c = DEVICE_DT_GET(DT_BUS(DT_DRV_INST(0)));

    if (!data->i2c) {
        LOG_ERR("I2C device not found");
        printk("IQS5XX: I2C device not found\n");
        return -ENODEV;
    }

    if (!device_is_ready(data->i2c)) {
        LOG_ERR("I2C device not ready");
        printk("IQS5XX: I2C device not ready\n");
        return -ENODEV;
    }

    LOG_INF("I2C device ready: %s", data->i2c->name);
    printk("IQS5XX: I2C device ready: %s\n", data->i2c->name);

    // Check GPIO configuration
    if (!device_is_ready(config->dr.port)) {
        if (config->dr.port == NULL) {
            LOG_ERR("Data ready GPIO not configured in devicetree");
            printk("IQS5XX: Data ready GPIO not configured\n");
            return -ENODEV;
        } else {
            LOG_ERR("Data ready GPIO device not ready");
            printk("IQS5XX: Data ready GPIO device not ready\n");
            return -ENODEV;
        }
    }

    LOG_INF("GPIO device ready: %s, pin: %d", config->dr.port->name, config->dr.pin);
    printk("IQS5XX: GPIO device ready: %s, pin: %d\n", config->dr.port->name, config->dr.pin);

    LOG_INF("IQS5XX config: inv_x=%d, inv_y=%d, rot90=%d, rot180=%d, rot270=%d, sens=%d",
            config->invert_x, config->invert_y, config->rotate_90,
            config->rotate_180, config->rotate_270, config->sensitivity);

    k_mutex_init(&data->i2c_mutex);
    k_work_init(&data->work, iqs5xx_work_cb);

    // Configure data ready pin
    int ret = gpio_pin_configure_dt(&config->dr, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure data ready pin: %d", ret);
        printk("IQS5XX: Failed to configure GPIO: %d\n", ret);
        return ret;
    }

    LOG_INF("GPIO pin configured successfully");

    // Initialize interrupt callback
    gpio_init_callback(&data->dr_cb, iqs5xx_gpio_cb, BIT(config->dr.pin));

    // Add callback
    ret = gpio_add_callback(config->dr.port, &data->dr_cb);
    if (ret < 0) {
        LOG_ERR("Failed to add GPIO callback: %d", ret);
        printk("IQS5XX: Failed to add GPIO callback: %d\n", ret);
        return ret;
    }

    LOG_INF("GPIO callback added successfully");

    // Test I2C communication FIRST
    LOG_INF("Testing I2C communication...");
    printk("IQS5XX: Testing I2C communication...\n");

    uint8_t test_buf[2];
    ret = iqs5xx_seq_read(dev, ProductNumber_adr, test_buf, 2);
    if (ret < 0) {
        LOG_ERR("I2C communication test FAILED: %d", ret);
        printk("IQS5XX: I2C communication FAILED: %d\n", ret);
        return ret;
    } else {
        uint16_t product_number = (test_buf[0] << 8) | test_buf[1];
        LOG_INF("I2C communication SUCCESS! Product Number: 0x%04x", product_number);
        printk("IQS5XX: I2C SUCCESS! Product: 0x%04x\n", product_number);
    }

    // Initialize device registers
    LOG_INF("Initializing device registers...");
    struct iqs5xx_reg_config iqs5xx_registers = iqs5xx_reg_config_default();
    ret = iqs5xx_registers_init(dev, &iqs5xx_registers);
    if(ret) {
        LOG_ERR("Failed to initialize registers: %d", ret);
        printk("IQS5XX: Register init FAILED: %d\n", ret);
        return ret;
    }

    // Configure data ready interrupt AFTER initialization
    LOG_INF("Configuring interrupt...");
    ret = gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure data ready interrupt: %d", ret);
        printk("IQS5XX: Interrupt config FAILED: %d\n", ret);
        return ret;
    }

    LOG_INF("Interrupt configured successfully");

    // Final test - try to read some data
    LOG_INF("Final communication test...");
    ret = iqs5xx_sample_fetch(dev);
    if (ret < 0) {
        LOG_WRN("Initial sample fetch failed: %d (continuing anyway)", ret);
        printk("IQS5XX: Sample fetch failed: %d\n", ret);
    } else {
        LOG_INF("Sample fetch SUCCESS!");
        printk("IQS5XX: Sample fetch SUCCESS!\n");
    }

    LOG_INF("=== IQS5XX INITIALIZATION COMPLETE ===");
    printk("IQS5XX: Initialization COMPLETE!\n");
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
// Simplified power management for diagnostic
static int iqs5xx_pm_action(const struct device *dev, enum pm_device_action action) {
    LOG_INF("Power management action: %d", action);
    return 0;
}
PM_DEVICE_DT_INST_DEFINE(0, iqs5xx_pm_action);
#define PM_DEVICE_INST PM_DEVICE_DT_INST_GET(0)
#else
#define PM_DEVICE_INST NULL
#endif

DEVICE_DT_INST_DEFINE(0, iqs5xx_init, PM_DEVICE_INST,
                      &iqs5xx_data_0, &iqs5xx_config_0,
                      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);
