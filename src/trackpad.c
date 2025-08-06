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

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

static struct gesture_state g_gesture_state = {0};
// REMOVED: unused trackpad variable that was causing warning
static const struct device *trackpad_device = NULL;
static int event_count = 0;
static int64_t last_event_time = 0; // For rate-limiting

// Optimized input event sending
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    event_count++;
    // Log important events
    if (type == INPUT_EV_KEY) {
        // Button press/release
    } else if (abs(value) > 5) { // Only log significant movements
        // Mouse movement
    }

    if (trackpad_device) {
        int ret = input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
        if (ret < 0) {
            return;
        }
    } else {
        return;
    }
}

// Initialize gesture state functions (these were missing)
void single_finger_init(void) {
    // Initialize single finger state if needed
    g_gesture_state.isDragging = false;
    g_gesture_state.dragStartSent = false;
    g_gesture_state.accumPos.x = 0;
    g_gesture_state.accumPos.y = 0;
}

void two_finger_init(void) {
    // Initialize two finger state if needed
    g_gesture_state.twoFingerActive = false;
    g_gesture_state.lastXScrollReport = 0;
}

void three_finger_init(void) {
    // Initialize three finger state if needed
    g_gesture_state.threeFingersPressed = false;
    g_gesture_state.gestureTriggered = false;
}

// FIXED: Handle gestures even when finger_count == 0
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

    // Log when finger count changes or gestures detected
    if (finger_count_changed || has_gesture) {
        // TRIGER #%d: fingers=%d, g0=0x%02x, g1=0x%02x, rel=%d/%d",
    }

    // FIXED: Process gestures FIRST, before finger count logic
    if (has_gesture) {
        // GESTURE DETECTED: g0=0x%02x, g1=0x%02x ===", data->gestures0, data->gestures1);

        // Handle single finger gestures (including taps that happen on finger lift)
        if (data->gestures0) {
            handle_single_finger_gestures(dev, data, &g_gesture_state);
        }

        // Handle two finger gestures
        if (data->gestures1) {
            handle_two_finger_gestures(dev, data, &g_gesture_state);
        }
    }

    // THEN handle finger count changes and movement
    switch (data->finger_count) {
        case 0:
            // Reset all states when no fingers
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;

        case 1:
            // Only reset others if they were active
            if (g_gesture_state.twoFingerActive) reset_two_finger_state(&g_gesture_state);
            if (g_gesture_state.threeFingersPressed) reset_three_finger_state(&g_gesture_state);

            // Handle single finger movement (but gestures were already handled above)
            if (!has_gesture) {
                handle_single_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 2:
            // Only reset others if they were active
            if (g_gesture_state.isDragging) reset_single_finger_state(&g_gesture_state);
            if (g_gesture_state.threeFingersPressed) reset_three_finger_state(&g_gesture_state);

            // Handle two finger gestures (but hardware gestures were already handled above)
            if (!has_gesture) {
                handle_two_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 3:
            // Only reset others if they were active
            if (g_gesture_state.isDragging) reset_single_finger_state(&g_gesture_state);
            if (g_gesture_state.twoFingerActive) reset_two_finger_state(&g_gesture_state);
            handle_three_finger_gestures(dev, data, &g_gesture_state);
            break;

        default:
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
    // Get the IQS5XX device - FIXED device tree access
    const struct device *trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (!trackpad) {
        return -ENODEV;
    }

    // Set the global trackpad device reference
    trackpad_device = trackpad;

    // Initialize trackpad keyboard events
    int ret = trackpad_keyboard_init(trackpad);
    if (ret < 0) {
        return ret;
    }

    // Initialize gesture state
    single_finger_init();
    two_finger_init();
    three_finger_init();

    // Set the trigger handler
    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if (err < 0) {
        return err;
    }

    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
