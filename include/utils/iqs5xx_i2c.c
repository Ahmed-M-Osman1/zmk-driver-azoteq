// =============================================================================
// FILE: src/utils/iqs5xx_i2c.c
// Optimized I2C communication utilities
// =============================================================================

#include "../iqs5xx_enhanced.h"

LOG_MODULE_DECLARE(iqs5xx_driver);

// I2C transaction cache for performance optimization
static struct {
    uint16_t last_read_addr;
    uint32_t last_read_time;
    uint8_t cached_data[64];    // Cache for frequently read registers
    bool cache_valid;
} i2c_cache = {0};

/**
 * Optimized register read with caching
 */
int iqs5xx_read_registers(const struct device *dev, uint16_t start_addr,
                         uint8_t *buffer, uint32_t length) {
    const struct azoteq_iqs5xx_config *config = dev->config;

    // Validate parameters
    if (!buffer || length == 0 || length > 64) {
        return -EINVAL;
    }

    // Check cache for recent reads (performance optimization)
    uint32_t current_time = k_uptime_get_32();
    if (i2c_cache.cache_valid &&
        i2c_cache.last_read_addr == start_addr &&
        (current_time - i2c_cache.last_read_time) < 5) { // 5ms cache validity

        memcpy(buffer, i2c_cache.cached_data, length);
        LOG_DBG("Using cached I2C data for addr 0x%04x", start_addr);
        return 0;
    }

    // Prepare address bytes (big-endian format)
    uint8_t addr_bytes[2];
    addr_bytes[0] = (start_addr >> 8) & 0xFF;
    addr_bytes[1] = start_addr & 0xFF;

    // Perform I2C write-read transaction
    int ret = i2c_write_read_dt(&config->i2c_bus, addr_bytes, sizeof(addr_bytes),
                               buffer, length);

    if (ret == 0) {
        // Update cache for future reads
        i2c_cache.last_read_addr = start_addr;
        i2c_cache.last_read_time = current_time;
        memcpy(i2c_cache.cached_data, buffer, MIN(length, sizeof(i2c_cache.cached_data)));
        i2c_cache.cache_valid = true;

        LOG_DBG("I2C read successful: addr=0x%04x, len=%d", start_addr, length);
    } else {
        i2c_cache.cache_valid = false; // Invalidate cache on error
        LOG_ERR("I2C read failed: addr=0x%04x, error=%d", start_addr, ret);
    }

    return ret;
}

/**
 * Optimized register write with error handling
 */
int iqs5xx_write_registers(const struct device *dev, uint16_t start_addr,
                          const uint8_t *buffer, uint32_t length) {
    const struct azoteq_iqs5xx_config *config = dev->config;

    // Validate parameters
    if (!buffer || length == 0) {
        return -EINVAL;
    }

    // Prepare transaction buffer (address + data)
    uint8_t tx_buffer[66]; // 2 bytes addr + up to 64 bytes data
    if (length > 64) {
        return -EINVAL;
    }

    // Pack address and data
    tx_buffer[0] = (start_addr >> 8) & 0xFF;
    tx_buffer[1] = start_addr & 0xFF;
    memcpy(&tx_buffer[2], buffer, length);

    // Perform I2C write transaction
    int ret = i2c_write_dt(&config->i2c_bus, tx_buffer, length + 2);

    if (ret == 0) {
        LOG_DBG("I2C write successful: addr=0x%04x, len=%d", start_addr, length);
        // Invalidate cache since registers may have changed
        i2c_cache.cache_valid = false;
    } else {
        LOG_ERR("I2C write failed: addr=0x%04x, error=%d", start_addr, ret);
    }

    return ret;
}

/**
 * Read complete gesture data packet efficiently
 */
int iqs5xx_read_gesture_data(const struct device *dev, struct iqs5xx_rawdata *data) {
    uint8_t raw_buffer[44]; // Maximum data packet size
    int ret;

    // Read the complete gesture data packet in one transaction
    ret = iqs5xx_read_registers(dev, IQS5XX_GESTURE_EVENTS0, raw_buffer, sizeof(raw_buffer));
    if (ret != 0) {
        return ret;
    }

    // Parse raw data into structured format
    parse_gesture_data(raw_buffer, data);

    // Send end window command to acknowledge read
    uint8_t end_cmd = 0;
    ret = iqs5xx_write_registers(dev, IQS5XX_END_WINDOW, &end_cmd, 1);
    if (ret != 0) {
        LOG_WRN("Failed to send end window command: %d", ret);
    }

    // Add timestamp for performance tracking
    data->timestamp = k_uptime_get_32();

    return 0;
}

/**
 * Parse raw gesture data into structured format
 */
static void parse_gesture_data(const uint8_t *raw_buffer, struct iqs5xx_rawdata *data) {
    // Parse header information
    data->gestures0 = raw_buffer[0];
    data->gestures1 = raw_buffer[1];
    data->system_info0 = raw_buffer[2];
    data->system_info1 = raw_buffer[3];
    data->finger_count = raw_buffer[4] & 0x0F; // Lower 4 bits

    // Parse relative movement (signed 16-bit, big-endian)
    data->rel_x = (int16_t)((raw_buffer[5] << 8) | raw_buffer[6]);
    data->rel_y = (int16_t)((raw_buffer[7] << 8) | raw_buffer[8]);

    // Parse finger data (up to 5 fingers)
    for (int i = 0; i < MIN(data->finger_count, IQS5XX_MAX_FINGERS); i++) {
        int offset = 9 + (i * 7); // Each finger uses 7 bytes

        struct iqs5xx_finger *finger = &data->fingers[i];

        // Parse absolute coordinates (16-bit, big-endian)
        finger->abs_x = (raw_buffer[offset] << 8) | raw_buffer[offset + 1];
        finger->abs_y = (raw_buffer[offset + 2] << 8) | raw_buffer[offset + 3];

        // Parse strength and area
        finger->strength = (raw_buffer[offset + 4] << 8) | raw_buffer[offset + 5];
        finger->area = raw_buffer[offset + 6];

        // Mark finger as active
        finger->active = true;
        finger->timestamp = k_uptime_get_32();
    }

    // Mark unused fingers as inactive
    for (int i = data->finger_count; i < IQS5XX_MAX_FINGERS; i++) {
        data->fingers[i].active = false;
    }
}
