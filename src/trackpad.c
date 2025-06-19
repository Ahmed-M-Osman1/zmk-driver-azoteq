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
static const struct device *trackpad;
static const struct device *trackpad_device = NULL;
static int event_count = 0;
static int64_t last_event_time = 0; // For rate-limiting

// Optimized input event sending
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    event_count++;
    // REDUCED logging - only log important events
    if (type == INPUT_EV_KEY) {
        LOG_INF("CLICK #%d: btn=%d, val=%d", event_count, code, value);
    } else if (abs(value) > 5) { // Only log significant movements
        LOG_DBG("MOVE #%d: type=%d, code=%d, val=%d", event_count, type, code, value);
    }

    if (trackpad_device) {
        int ret = input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
        if (ret < 0) {
            LOG_ERR("Input event failed: %d", ret);
        }
    } else {
        LOG_ERR("No trackpad device");
    }
}

// Check if current data contains click-worthy events that should bypass rate limiting
static bool has_click_events(const struct iqs5xx_rawdata *data, const struct gesture_state *state) {
    // 1. Hardware gestures that generate clicks
    if (data->gestures0 & (GESTURE_SINGLE_TAP | GESTURE_TAP_AND_HOLD)) {
        return true;
    }

    // 2. Two-finger tap detection (finger count transition 2->0 quickly)
    if (state->lastFingerCount == 2 && data->finger_count == 0 && state->twoFingerActive) {
        return true;
    }

    // 3. Three-finger gesture events
    if (data->finger_count == 3 || (state->lastFingerCount == 3 && data->finger_count == 0)) {
        return true;
    }

    // 4. Any gesture flags that could trigger clicks
    if (data->gestures1 & GESTURE_TWO_FINGER_TAP) {
        return true;
    }

    return false;
}

// UPDATED trigger handler with CLICK PRIORITY - NO RATE LIMITING for clicks
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;
    int64_t current_time = k_uptime_get();

    trigger_count++;

    // CRITICAL: Check for click events first
    bool has_clicks = has_click_events(data, &g_gesture_state);
    bool has_gesture = (data->gestures0 != 0) || (data->gestures1 != 0);
    bool finger_count_changed = (g_gesture_state.lastFingerCount != data->finger_count);

    // UPDATED RATE LIMITING: Skip rate limiting for clicks, gestures, and finger count changes
    if (!has_clicks && !has_gesture && !finger_count_changed &&
        (current_time - last_event_time < 20)) {
        return; // Skip only pure movement events
    }
    last_event_time = current_time;

    // ENHANCED logging - show when we bypass rate limiting for clicks
    static uint8_t last_logged_fingers = 255;
    if (finger_count_changed || has_gesture || has_clicks) {
        const char* reason = has_clicks ? " [CLICK-PRIORITY]" :
                           has_gesture ? " [GESTURE]" : " [FINGER-CHANGE]";
        LOG_INF("TRIGGER #%d: fingers=%d, g0=0x%02x, g1=0x%02x, rel=%d/%d%s",
                trigger_count, data->finger_count, data->gestures0, data->gestures1,
                data->rx, data->ry, reason);
        last_logged_fingers = data->finger_count;
    }

    // FAST gesture processing - direct switch without complex logic
    switch (data->finger_count) {
        case 0:
            // IMMEDIATE cleanup - no delays
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;

        case 1:
            // Only reset others if they were active
            if (g_gesture_state.twoFingerActive) reset_two_finger_state(&g_gesture_state);
            if (g_gesture_state.threeFingersPressed) reset_three_finger_state(&g_gesture_state);
            handle_single_finger_gestures(dev, data, &g_gesture_state);
            break;

        case 2:
            // Only reset others if they were active
            if (g_gesture_state.isDragging) reset_single_finger_state(&g_gesture_state);
            if (g_gesture_state.threeFingersPressed) reset_three_finger_state(&g_gesture_state);
            handle_two_finger_gestures(dev, data, &g_gesture_state);
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

    // Update finger count ONLY when it changes
    if (g_gesture_state.lastFingerCount != data->finger_count) {
        g_gesture_state.lastFingerCount = data->finger_count;
    }
}

static int trackpad_init(void) {
    LOG_INF("=== OPTIMIZED MODULAR TRACKPAD INIT START ===");

    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }
    LOG_INF("Found IQS5XX device: %p", trackpad);

    trackpad_device = trackpad;
    LOG_INF("Set trackpad device reference: %p", trackpad_device);

    // Initialize the keyboard events system
    int ret = trackpad_keyboard_init(trackpad_device);
    if (ret < 0) {
        LOG_ERR("Failed to initialize trackpad keyboard events: %d", ret);
        return ret;
    }

    // OPTIMIZED: Initialize gesture state with performance settings
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
    g_gesture_state.mouseSensitivity = 200; // Match your overlay sensitivity
    LOG_INF("Initialized optimized gesture state - NO RATE LIMIT for clicks");

    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return -EINVAL;
    }
    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
