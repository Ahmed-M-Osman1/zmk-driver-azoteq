#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include "iqs5xx.h"
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Function declarations
static void handle_i2c_error(const struct device *dev);
static struct gesture_state g_gesture_state = {0};
static const struct device *trackpad;
static const struct device *trackpad_device = NULL;
static int event_count = 0;
static int consecutive_i2c_errors = 0;
static int64_t last_error_time = 0;

// NEW: Send keyboard events using input system
// This approach sends keyboard events through the input system
// which should work better for external modules
void send_keyboard_key(uint16_t keycode) {
    event_count++;
    LOG_INF("KEYBOARD EVENT #%d: keycode=%d", event_count, keycode);

    if (trackpad_device) {
        // Send key press
        int ret = input_report(trackpad_device, INPUT_EV_KEY, keycode, 1, false, K_NO_WAIT);
        if (ret < 0) {
            LOG_ERR("Failed to send key press: %d", ret);
            return;
        }

        // Send sync
        ret = input_report(trackpad_device, INPUT_EV_SYN, INPUT_SYN_REPORT, 0, true, K_NO_WAIT);
        if (ret < 0) {
            LOG_ERR("Failed to send sync after key press: %d", ret);
            return;
        }

        // Small delay
        k_msleep(10);

        // Send key release
        ret = input_report(trackpad_device, INPUT_EV_KEY, keycode, 0, false, K_NO_WAIT);
        if (ret < 0) {
            LOG_ERR("Failed to send key release: %d", ret);
            return;
        }

        // Send sync
        ret = input_report(trackpad_device, INPUT_EV_SYN, INPUT_SYN_REPORT, 0, true, K_NO_WAIT);
        if (ret < 0) {
            LOG_ERR("Failed to send sync after key release: %d", ret);
            return;
        }

        LOG_DBG("Keyboard event sent successfully");
    } else {
        LOG_ERR("Trackpad device is NULL - cannot send keyboard events");
    }
}

void send_keyboard_combo(uint16_t modifier, uint16_t keycode) {
    event_count++;
    LOG_INF("KEYBOARD COMBO #%d: modifier=%d + key=%d", event_count, modifier, keycode);

    if (trackpad_device) {
        // Press modifier
        input_report(trackpad_device, INPUT_EV_KEY, modifier, 1, false, K_NO_WAIT);
        input_report(trackpad_device, INPUT_EV_SYN, INPUT_SYN_REPORT, 0, true, K_NO_WAIT);
        k_msleep(10);

        // Press main key
        input_report(trackpad_device, INPUT_EV_KEY, keycode, 1, false, K_NO_WAIT);
        input_report(trackpad_device, INPUT_EV_SYN, INPUT_SYN_REPORT, 0, true, K_NO_WAIT);
        k_msleep(10);

        // Release main key
        input_report(trackpad_device, INPUT_EV_KEY, keycode, 0, false, K_NO_WAIT);
        input_report(trackpad_device, INPUT_EV_SYN, INPUT_SYN_REPORT, 0, true, K_NO_WAIT);
        k_msleep(10);

        // Release modifier
        input_report(trackpad_device, INPUT_EV_KEY, modifier, 0, false, K_NO_WAIT);
        input_report(trackpad_device, INPUT_EV_SYN, INPUT_SYN_REPORT, 0, true, K_NO_WAIT);

        LOG_DBG("Keyboard combo sent successfully");
    } else {
        LOG_ERR("Trackpad device is NULL - cannot send keyboard combo");
    }
}

// Keep existing send_input_event for mouse events
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    event_count++;
    LOG_INF("INPUT EVENT #%d: type=%d, code=%d, value=%d, sync=%d",
            event_count, type, code, value, sync);

    if (trackpad_device) {
        int ret = input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
        if (ret < 0) {
            LOG_ERR("Failed to send input event: %d", ret);
        } else {
            LOG_DBG("Input event sent successfully");
        }
    } else {
        LOG_ERR("Trackpad device is NULL - cannot send input events");
    }
}

// Main gesture handler - coordinates all gesture types
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;

    // Reset error counter on successful data
    consecutive_i2c_errors = 0;

    trigger_count++;
    LOG_DBG("=== TRIGGER #%d ===", trigger_count);
    LOG_INF("Raw data: fingers=%d, gestures0=0x%02x, gestures1=0x%02x, rx=%d, ry=%d",
            data->finger_count, data->gestures0, data->gestures1, data->rx, data->ry);

    // Log finger details for debugging
    for (int i = 0; i < data->finger_count && i < 5; i++) {
        if (data->fingers[i].strength > 0) {
            LOG_DBG("Finger %d: pos=(%d,%d), strength=%d, area=%d",
                    i, data->fingers[i].ax, data->fingers[i].ay,
                    data->fingers[i].strength, data->fingers[i].area);
        }
    }

    // Debug two finger positions if enabled
    if (data->finger_count == 2) {
        debug_two_finger_positions(data, &g_gesture_state);
    }

    // Handle gestures based on finger count
    switch (data->finger_count) {
        case 0:
            // Handle finger release - reset all states
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;

        case 1:
            // Reset multi-finger states
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);

            // Handle single finger gestures
            handle_single_finger_gestures(dev, data, &g_gesture_state);
            break;

        case 2:
            // Reset other finger states
            reset_single_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);

            // Handle two finger gestures
            handle_two_finger_gestures(dev, data, &g_gesture_state);
            break;

        case 3:
            // Reset other finger states
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);

            // Handle three finger gestures
            handle_three_finger_gestures(dev, data, &g_gesture_state);
            break;

        default:
            // 4+ fingers - reset all states
            LOG_DBG("4+ fingers detected (%d), resetting all states", data->finger_count);
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;
    }

    // Log finger count changes
    if (g_gesture_state.lastFingerCount != data->finger_count) {
        LOG_INF("Finger count changed: %d -> %d",
                g_gesture_state.lastFingerCount, data->finger_count);
        g_gesture_state.lastFingerCount = data->finger_count;
    }
}

// Error handler for I2C failures
static void handle_i2c_error(const struct device *dev) {
    consecutive_i2c_errors++;
    int64_t current_time = k_uptime_get();

    LOG_ERR("I2C error #%d", consecutive_i2c_errors);

    // If we have too many consecutive errors, try to recover
    if (consecutive_i2c_errors >= 10) {
        LOG_ERR("Too many I2C errors (%d), attempting recovery", consecutive_i2c_errors);

        // Reset error counter to prevent infinite loop
        consecutive_i2c_errors = 0;
        last_error_time = current_time;

        // Try to reset the device state
        memset(&g_gesture_state, 0, sizeof(g_gesture_state));
        g_gesture_state.mouseSensitivity = 200;

        // Disable interrupts temporarily
        const struct iqs5xx_config *config = dev->config;
        gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_DISABLE);

        // Wait a bit
        k_msleep(100);

        // Re-enable interrupts
        gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_EDGE_TO_ACTIVE);

        LOG_INF("Recovery attempt completed");
    }

    // If errors persist for too long, disable temporarily
    if (current_time - last_error_time > 5000 && consecutive_i2c_errors > 0) {
        LOG_WRN("Disabling interrupts for 1 second due to persistent errors");
        const struct iqs5xx_config *config = dev->config;
        gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_DISABLE);
        k_msleep(1000);
        gpio_pin_interrupt_configure_dt(&config->dr, GPIO_INT_EDGE_TO_ACTIVE);
        consecutive_i2c_errors = 0;
    }
}

static int trackpad_init(void) {
    LOG_INF("=== TRACKPAD GESTURE HANDLER INIT START ===");

    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }
    LOG_INF("Found IQS5XX device: %p", trackpad);

    // Store reference for input events
    trackpad_device = trackpad;
    LOG_INF("Set trackpad device reference: %p", trackpad_device);

    // Initialize global gesture state
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
    g_gesture_state.mouseSensitivity = 200;  // Default sensitivity
    LOG_INF("Initialized gesture state with mouse sensitivity: %d",
            g_gesture_state.mouseSensitivity);

    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return -EINVAL;
    }
    LOG_INF("Trigger handler set successfully");

    LOG_INF("=== TRACKPAD GESTURE HANDLER INIT COMPLETE ===");
    LOG_INF("Supported gestures:");
    LOG_INF("  1 finger: tap (left click), tap-hold (drag), movement");
    LOG_INF("  2 finger: tap (right click), scroll, zoom (Ctrl+/-)");//
    LOG_INF("  3 finger: tap (middle click), swipe up (F3), swipe down (F4)");

    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
