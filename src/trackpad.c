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

// Check if devicetree behaviors exist
#if DT_NODE_EXISTS(DT_NODELABEL(gesture_f3))
    #define HAS_GESTURE_F3 1
#else
    #define HAS_GESTURE_F3 0
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(gesture_f4))
    #define HAS_GESTURE_F4 1
#else
    #define HAS_GESTURE_F4 0
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(gesture_zoom_in))
    #define HAS_GESTURE_ZOOM_IN 1
#else
    #define HAS_GESTURE_ZOOM_IN 0
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(gesture_zoom_out))
    #define HAS_GESTURE_ZOOM_OUT 1
#else
    #define HAS_GESTURE_ZOOM_OUT 0
#endif

// NEW: Function to trigger ZMK behaviors from devicetree
void trigger_zmk_behavior(const char* behavior_name) {
    event_count++;
    LOG_INF("TRIGGERING BEHAVIOR #%d: %s", event_count, behavior_name);

    // We'll use a simple approach: send a specific unused mouse button
    // that can be mapped in the keymap to trigger the desired behavior

    if (trackpad_device) {
        if (strcmp(behavior_name, "f3") == 0) {
            // Send mouse button 10 for F3 (you'll map this in keymap)
            send_input_event(INPUT_EV_KEY, INPUT_BTN_SIDE, 1, false);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_SIDE, 0, true);
        } else if (strcmp(behavior_name, "f4") == 0) {
            // Send mouse button 11 for F4 (you'll map this in keymap)
            send_input_event(INPUT_EV_KEY, INPUT_BTN_EXTRA, 1, false);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_EXTRA, 0, true);
        } else if (strcmp(behavior_name, "zoom_in") == 0) {
            // Send mouse button 12 for zoom in (you'll map this in keymap)
            send_input_event(INPUT_EV_KEY, INPUT_BTN_FORWARD, 1, false);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_FORWARD, 0, true);
        } else if (strcmp(behavior_name, "zoom_out") == 0) {
            // Send mouse button 13 for zoom out (you'll map this in keymap)
            send_input_event(INPUT_EV_KEY, INPUT_BTN_BACK, 1, false);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_BACK, 0, true);
        }

        LOG_DBG("Behavior trigger sent successfully");
    } else {
        LOG_ERR("Trackpad device is NULL - cannot trigger behavior");
    }
}

// Updated functions that map to behaviors
void send_keyboard_key(uint16_t keycode) {
    if (keycode == INPUT_KEY_F3) {
        trigger_zmk_behavior("f3");
    } else if (keycode == INPUT_KEY_F4) {
        trigger_zmk_behavior("f4");
    }
}

void send_keyboard_combo(uint16_t modifier, uint16_t keycode) {
    if (modifier == INPUT_KEY_LEFTCTRL && keycode == INPUT_KEY_EQUAL) {
        trigger_zmk_behavior("zoom_in");
    } else if (modifier == INPUT_KEY_LEFTCTRL && keycode == INPUT_KEY_MINUS) {
        trigger_zmk_behavior("zoom_out");
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

// Rest of trackpad.c remains the same...
// [Include all the existing gesture handler code]

static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;

    // Reset error counter on successful data
    consecutive_i2c_errors = 0;

    trigger_count++;
    LOG_DBG("=== TRIGGER #%d ===", trigger_count);
    LOG_INF("Raw data: fingers=%d, gestures0=0x%02x, gestures1=0x%02x, rx=%d, ry=%d",
            data->finger_count, data->gestures0, data->gestures1, data->rx, data->ry);

    // Handle gestures based on finger count
    switch (data->finger_count) {
        case 0:
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;

        case 1:
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            handle_single_finger_gestures(dev, data, &g_gesture_state);
            break;

        case 2:
            reset_single_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            handle_two_finger_gestures(dev, data, &g_gesture_state);
            break;

        case 3:
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            handle_three_finger_gestures(dev, data, &g_gesture_state);
            break;

        default:
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;
    }

    if (g_gesture_state.lastFingerCount != data->finger_count) {
        LOG_INF("Finger count changed: %d -> %d",
                g_gesture_state.lastFingerCount, data->finger_count);
        g_gesture_state.lastFingerCount = data->finger_count;
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

    trackpad_device = trackpad;
    LOG_INF("Set trackpad device reference: %p", trackpad_device);

    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
    g_gesture_state.mouseSensitivity = 200;
    LOG_INF("Initialized gesture state");

    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return -EINVAL;
    }
    LOG_INF("Trigger handler set successfully");

    // Log which behaviors are available
    LOG_INF("Available gesture behaviors:");
    LOG_INF("  F3: %s", HAS_GESTURE_F3 ? "YES" : "NO");
    LOG_INF("  F4: %s", HAS_GESTURE_F4 ? "YES" : "NO");
    LOG_INF("  Zoom In: %s", HAS_GESTURE_ZOOM_IN ? "YES" : "NO");
    LOG_INF("  Zoom Out: %s", HAS_GESTURE_ZOOM_OUT ? "YES" : "NO");

    LOG_INF("=== TRACKPAD GESTURE HANDLER INIT COMPLETE ===");
    LOG_INF("Supported gestures:");
    LOG_INF("  1 finger: tap (left click), tap-hold (drag), movement");
    LOG_INF("  2 finger: tap (right click), scroll, zoom (mapped to mouse buttons)");
    LOG_INF("  3 finger: tap (middle click), swipe up/down (mapped to mouse buttons)");

    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
