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

// NEW: Send consumer control keys (these work better with ZMK)
void send_consumer_key(uint16_t usage_id) {
    event_count++;
    LOG_INF("CONSUMER KEY #%d: usage=0x%04x", event_count, usage_id);

    if (trackpad_device) {
        // Send consumer control event
        int ret = input_report(trackpad_device, INPUT_EV_KEY, usage_id, 1, false, K_NO_WAIT);
        if (ret < 0) {
            LOG_ERR("Failed to send consumer key press: %d", ret);
            return;
        }

        k_msleep(10);

        ret = input_report(trackpad_device, INPUT_EV_KEY, usage_id, 0, true, K_NO_WAIT);
        if (ret < 0) {
            LOG_ERR("Failed to send consumer key release: %d", ret);
            return;
        }

        LOG_DBG("Consumer key sent successfully");
    } else {
        LOG_ERR("Trackpad device is NULL - cannot send consumer key");
    }
}

// Map gestures to consumer control keys
void send_keyboard_key(uint16_t keycode) {
    if (keycode == INPUT_KEY_F3) {
        // Use consumer control F13 for F3 gesture
        send_consumer_key(KEY_F13);
    } else if (keycode == INPUT_KEY_F4) {
        // Use consumer control F14 for F4 gesture
        send_consumer_key(KEY_F14);
    }
}

void send_keyboard_combo(uint16_t modifier, uint16_t keycode) {
    if (modifier == INPUT_KEY_LEFTCTRL && keycode == INPUT_KEY_EQUAL) {
        // Use consumer zoom in
        send_consumer_key(KEY_ZOOMIN);
    } else if (modifier == INPUT_KEY_LEFTCTRL && keycode == INPUT_KEY_MINUS) {
        // Use consumer zoom out
        send_consumer_key(KEY_ZOOMOUT);
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

// All the existing gesture handler code remains the same...
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;

    consecutive_i2c_errors = 0;

    trigger_count++;
    LOG_DBG("=== TRIGGER #%d ===", trigger_count);
    LOG_INF("Raw data: fingers=%d, gestures0=0x%02x, gestures1=0x%02x, rx=%d, ry=%d",
            data->finger_count, data->gestures0, data->gestures1, data->rx, data->ry);

    for (int i = 0; i < data->finger_count && i < 5; i++) {
        if (data->fingers[i].strength > 0) {
            LOG_DBG("Finger %d: pos=(%d,%d), strength=%d, area=%d",
                    i, data->fingers[i].ax, data->fingers[i].ay,
                    data->fingers[i].strength, data->fingers[i].area);
        }
    }

    if (data->finger_count == 2) {
        debug_two_finger_positions(data, &g_gesture_state);
    }

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

    LOG_INF("=== TRACKPAD GESTURE HANDLER INIT COMPLETE ===");
    LOG_INF("Supported gestures:");
    LOG_INF("  1 finger: tap (left click), tap-hold (drag), movement");
    LOG_INF("  2 finger: tap (right click), scroll, zoom (F13/F14)");
    LOG_INF("  3 finger: tap (middle click), swipe up/down (ZOOM keys)");

    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
