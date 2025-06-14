// =============================================================================
// FILE: src/input/mouse_handler.c
// Optimized mouse movement and button handling
// =============================================================================

#include "../iqs5xx_enhanced.h"

LOG_MODULE_DECLARE(iqs5xx_driver);

// Mouse tracking state for performance optimization
static struct {
    int16_t accumulated_x;      // Accumulated movement for smoothing
    int16_t accumulated_y;
    uint32_t last_movement_time;
    bool button_state[3];       // Left, Right, Middle button states
} mouse_state = {0};

/**
 * Generate optimized mouse events with movement smoothing
 */
void generate_mouse_events(const struct device *dev, const struct iqs5xx_rawdata *data) {
    // Process movement with acceleration and smoothing
    process_mouse_movement(dev, data);

    // Handle button states based on finger pressure/area
    process_mouse_buttons(dev, data);

    // Update ZMK mouse HID report efficiently
    update_zmk_mouse_report(data);
}

/**
 * Process mouse movement with acceleration and smoothing
 */
static void process_mouse_movement(const struct device *dev, const struct iqs5xx_rawdata *data) {
    // Skip processing if no relative movement
    if (data->rel_x == 0 && data->rel_y == 0) {
        return;
    }

    // Apply movement scaling based on sensitivity
    struct azoteq_iqs5xx_data *dev_data = dev->data;
    int16_t scaled_x = (data->rel_x * dev_data->sensitivity_level) / 5;
    int16_t scaled_y = (data->rel_y * dev_data->sensitivity_level) / 5;

    // Accumulate movement for smoothing (reduce jitter)
    mouse_state.accumulated_x += scaled_x;
    mouse_state.accumulated_y += scaled_y;

    // Apply movement threshold to prevent micro-movements
    const struct azoteq_iqs5xx_config *config = dev->config;
    int16_t movement_x = 0, movement_y = 0;

    if (abs(mouse_state.accumulated_x) >= config->movement_threshold) {
        movement_x = mouse_state.accumulated_x;
        mouse_state.accumulated_x = 0;
    }

    if (abs(mouse_state.accumulated_y) >= config->movement_threshold) {
        movement_y = mouse_state.accumulated_y;
        mouse_state.accumulated_y = 0;
    }

    // Generate input events if movement is significant
    if (movement_x != 0 || movement_y != 0) {
        LOG_DBG("Mouse movement: dx=%d, dy=%d", movement_x, movement_y);

        input_report_rel(dev, INPUT_REL_X, movement_x, false, K_NO_WAIT);
        input_report_rel(dev, INPUT_REL_Y, movement_y, true, K_NO_WAIT);

        mouse_state.last_movement_time = k_uptime_get_32();
    }
}

/**
 * Process mouse buttons based on finger pressure and gestures
 */
static void process_mouse_buttons(const struct device *dev, const struct iqs5xx_rawdata *data) {
    // Button state is primarily handled by gesture detection
    // This function can be extended for pressure-based clicking

    if (data->finger_count == 1) {
        const struct iqs5xx_finger *finger = &data->fingers[0];

        // Example: High pressure could trigger button press
        bool should_press = finger->strength > 200; // Adjust threshold as needed

        if (should_press != mouse_state.button_state[0]) {
            mouse_state.button_state[0] = should_press;
            input_report_key(dev, INPUT_BTN_LEFT, should_press ? 1 : 0, true, K_NO_WAIT);
            LOG_DBG("Pressure click: %s", should_press ? "pressed" : "released");
        }
    }
}

/**
 * Update ZMK mouse HID report efficiently
 */
static void update_zmk_mouse_report(const struct iqs5xx_rawdata *data) {
    // Only update ZMK if there's actual movement to report
    if (data->rel_x != 0 || data->rel_y != 0) {
        // Scale movement for ZMK HID reporting
        int8_t zmk_x = CLAMP(data->rel_x, INT8_MIN, INT8_MAX);
        int8_t zmk_y = CLAMP(data->rel_y, INT8_MIN, INT8_MAX);

        zmk_hid_mouse_movement_update(zmk_x, zmk_y);
        zmk_usb_hid_send_mouse_report();
    }
}
