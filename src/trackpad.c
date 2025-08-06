#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include "iqs5xx.h"
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_REGISTER(trackpad, CONFIG_ZMK_LOG_LEVEL);

static struct gesture_state g_gesture_state = {0};
static const struct device *trackpad_device = NULL;
static int event_count = 0;
static int64_t last_event_time = 0;

// FIXED: Robust input event sending with validation
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    event_count++;

    // Validate and clamp input values to prevent erratic behavior
    if (type == INPUT_EV_REL) {
        // Clamp relative movements to reasonable bounds
        if (abs(value) > 127) {
            LOG_WRN("Clamping large movement: %d -> %d", value, (value > 0) ? 127 : -127);
            value = (value > 0) ? 127 : -127;
        }

        // Filter out tiny movements that might be noise
        if (abs(value) < 1) {
            return;
        }
    }

    // Log important events for debugging
    if (type == INPUT_EV_KEY || (type == INPUT_EV_REL && abs(value) > 5)) {
        LOG_INF("Input event: type=%d, code=%d, value=%d, sync=%d", type, code, value, sync);
    }

    if (trackpad_device) {
        int ret = input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
        if (ret < 0) {
            LOG_ERR("Failed to send input event: %d", ret);
        }
    } else {
        LOG_ERR("Trackpad device not initialized!");
    }
}

// Initialize gesture state functions - RESTORED from power-management branch
void single_finger_init(void) {
    LOG_INF("Initializing single finger gestures");
    g_gesture_state.isDragging = false;
    g_gesture_state.dragStartSent = false;
    g_gesture_state.accumPos.x = 0;
    g_gesture_state.accumPos.y = 0;
}

void two_finger_init(void) {
    LOG_INF("Initializing two finger gestures");
    g_gesture_state.twoFingerActive = false;
    g_gesture_state.lastXScrollReport = 0;
}

void three_finger_init(void) {
    LOG_INF("Initializing three finger gestures");
    g_gesture_state.threeFingersPressed = false;
    g_gesture_state.gestureTriggered = false;
}

// FIXED: Simplified trigger handler focusing on stability
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;
    int64_t current_time = k_uptime_get();

    trigger_count++;

    // CRITICAL: ALWAYS process gestures immediately, regardless of finger count
    bool has_gesture = (data->gestures0 != 0) || (data->gestures1 != 0);
    bool finger_count_changed = (g_gesture_state.lastFingerCount != data->finger_count);

    // Rate limit ONLY movement events, NEVER gesture events
    if (!has_gesture && !finger_count_changed && (current_time - last_event_time < 20)) {
        return; // Skip only movement-only events
    }
    last_event_time = current_time;

    // Log important activity
    if (has_gesture || finger_count_changed || (trigger_count % 100 == 1)) {
        LOG_INF("Trigger #%d: fingers=%d, g0=0x%02x, g1=0x%02x, rel=(%d,%d)",
                trigger_count, data->finger_count, data->gestures0, data->gestures1,
                data->rx, data->ry);
    }

    // FIXED: Process gestures FIRST, before finger count logic
    if (has_gesture) {
        LOG_INF("=== GESTURE DETECTED: g0=0x%02x, g1=0x%02x ===", data->gestures0, data->gestures1);

        // Handle single finger gestures
        if (data->gestures0) {
            LOG_INF("Handling single finger gesture: 0x%02x", data->gestures0);
            handle_single_finger_gestures(dev, data, &g_gesture_state);
        }

        // Handle two finger gestures
        if (data->gestures1) {
            LOG_INF("Handling two finger gesture: 0x%02x", data->gestures1);
            handle_two_finger_gestures(dev, data, &g_gesture_state);
        }
    }

    // THEN handle finger count changes and movement
    switch (data->finger_count) {
        case 0:
            if (g_gesture_state.lastFingerCount != 0) {
                LOG_INF("All fingers lifted - resetting states");
            }
            // Reset all states when no fingers
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;

        case 1:
            if (g_gesture_state.lastFingerCount != 1) {
                LOG_INF("Single finger detected");
            }
            // Only reset others if they were active
            if (g_gesture_state.twoFingerActive) reset_two_finger_state(&g_gesture_state);
            if (g_gesture_state.threeFingersPressed) reset_three_finger_state(&g_gesture_state);

            // Handle single finger movement (but gestures were already handled above)
            if (!has_gesture) {
                handle_single_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 2:
            if (g_gesture_state.lastFingerCount != 2) {
                LOG_INF("Two fingers detected");
            }
            // Only reset others if they were active
            if (g_gesture_state.isDragging) reset_single_finger_state(&g_gesture_state);
            if (g_gesture_state.threeFingersPressed) reset_three_finger_state(&g_gesture_state);

            // Handle two finger gestures (but hardware gestures were already handled above)
            if (!has_gesture) {
                handle_two_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 3:
            if (g_gesture_state.lastFingerCount != 3) {
                LOG_INF("Three fingers detected");
            }
            // Only reset others if they were active
            if (g_gesture_state.isDragging) reset_single_finger_state(&g_gesture_state);
            if (g_gesture_state.twoFingerActive) reset_two_finger_state(&g_gesture_state);
            handle_three_finger_gestures(dev, data, &g_gesture_state);
            break;

        default:
            if (data->finger_count > 3) {
                LOG_INF("%d fingers detected - resetting all", data->finger_count);
            }
            // 4+ fingers - reset all
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;
    }

    // Update finger count when it changes
    if (g_gesture_state.lastFingerCount != data->finger_count) {
        g_gesture_state.lastFingerCount = data->finger_count;
    }
}

static int trackpad_init(void) {
    LOG_INF("=== TRACKPAD INITIALIZATION STARTING ===");

    // Get the IQS5XX device - FIXED device tree access
    const struct device *trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (!trackpad) {
        LOG_ERR("IQS5XX device not found!");
        return -ENODEV;
    }

    LOG_INF("Found IQS5XX device: %s", trackpad->name);

    // Check if device is ready
    if (!device_is_ready(trackpad)) {
        LOG_ERR("IQS5XX device not ready!");
        return -ENODEV;
    }

    LOG_INF("IQS5XX device is ready");

    // Set the global trackpad device reference
    trackpad_device = trackpad;
    LOG_INF("Global trackpad device reference set");

    // Get configuration and initialize gesture state with proper sensitivity
    const struct iqs5xx_config *config = trackpad->config;
    g_gesture_state.mouseSensitivity = config->sensitivity;

    LOG_INF("Trackpad config: sensitivity=%d, rot90=%d, rot180=%d, rot270=%d, inv_x=%d, inv_y=%d",
            config->sensitivity, config->rotate_90, config->rotate_180, config->rotate_270,
            config->invert_x, config->invert_y);

    // Initialize trackpad keyboard events
    int ret = trackpad_keyboard_init(trackpad);
    if (ret < 0) {
        LOG_ERR("Failed to initialize trackpad keyboard events: %d", ret);
        return ret;
    }
    LOG_INF("Trackpad keyboard events initialized");

    // Initialize gesture state
    single_finger_init();
    two_finger_init();
    three_finger_init();
    LOG_INF("All gesture states initialized");

    // Set the trigger handler
    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if (err < 0) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return err;
    }
    LOG_INF("Trigger handler set successfully");

    LOG_INF("=== TRACKPAD INITIALIZATION COMPLETED SUCCESSFULLY ===");
    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
