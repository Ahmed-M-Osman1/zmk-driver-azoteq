// src/trackpad.c - Simplified using ZMK's kp behavior for F3/F4
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include "iqs5xx.h"
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

static struct gesture_state g_gesture_state = {0};
static const struct device *trackpad;
static const struct device *trackpad_device = NULL;
static int event_count = 0;

// Send keyboard keys using ZMK's keymap behavior system
void send_keyboard_key(uint16_t keycode) {
    LOG_INF("Sending keyboard key: %d", keycode);

    uint32_t zmk_keycode;

    // Map input codes to ZMK keycodes (use F3/F4 directly)
    switch(keycode) {
        case INPUT_KEY_F3:
            zmk_keycode = HID_USAGE_KEY_KEYBOARD_F3;
            break;
        case INPUT_KEY_F4:
            zmk_keycode = HID_USAGE_KEY_KEYBOARD_F4;
            break;
        default:
            LOG_WRN("Unknown keycode: %d", keycode);
            return;
    }

    // Create a key press binding for the behavior system
    struct zmk_behavior_binding binding = {
        .behavior_dev = "kp",  // key press behavior
        .param1 = zmk_keycode,
        .param2 = 0
    };

    // Send key press
    int ret = zmk_behavior_binding_pressed(&binding, &binding);
    if (ret < 0) {
        LOG_ERR("Failed to send key press: %d", ret);
        return;
    }

    // Schedule key release after short delay
    k_msleep(50);

    ret = zmk_behavior_binding_released(&binding, &binding);
    if (ret < 0) {
        LOG_ERR("Failed to send key release: %d", ret);
    } else {
        LOG_INF("Key sent successfully via behavior system");
    }
}

void send_keyboard_combo(uint16_t modifier, uint16_t keycode) {
    LOG_INF("Sending keyboard combo: mod=%d, key=%d", modifier, keycode);

    uint32_t zmk_modifier, zmk_keycode;

    // Map modifier and keycode
    switch(modifier) {
        case INPUT_KEY_LEFTCTRL:
            zmk_modifier = HID_USAGE_KEY_KEYBOARD_LEFTCONTROL;
            break;
        default:
            LOG_WRN("Unknown modifier: %d", modifier);
            return;
    }

    switch(keycode) {
        case INPUT_KEY_EQUAL:
            zmk_keycode = HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS;
            break;
        case INPUT_KEY_MINUS:
            zmk_keycode = HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE;
            break;
        default:
            LOG_WRN("Unknown keycode: %d", keycode);
            return;
    }

    // Create bindings for modifier and key
    struct zmk_behavior_binding mod_binding = {
        .behavior_dev = "kp",
        .param1 = zmk_modifier,
        .param2 = 0
    };

    struct zmk_behavior_binding key_binding = {
        .behavior_dev = "kp",
        .param1 = zmk_keycode,
        .param2 = 0
    };

    // Send modifier press
    int ret = zmk_behavior_binding_pressed(&mod_binding, &mod_binding);
    if (ret < 0) {
        LOG_ERR("Failed to send modifier press: %d", ret);
        return;
    }

    k_msleep(10);

    // Send key press
    ret = zmk_behavior_binding_pressed(&key_binding, &key_binding);
    if (ret < 0) {
        LOG_ERR("Failed to send key press: %d", ret);
        goto release_mod;
    }

    k_msleep(50);

    // Release key then modifier
    zmk_behavior_binding_released(&key_binding, &key_binding);
    k_msleep(10);

release_mod:
    zmk_behavior_binding_released(&mod_binding, &mod_binding);

    LOG_INF("Combo sent successfully via behavior system");
}

// Keep existing send_input_event for mouse events (unchanged)
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

static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;

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

    // Initialize the keyboard events system
    int ret = trackpad_keyboard_init(trackpad_device);
    if (ret < 0) {
        LOG_ERR("Failed to initialize trackpad keyboard events: %d", ret);
        return ret;
    }

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
    LOG_INF("  2 finger: tap (right click), scroll, zoom (Ctrl+Plus/Minus)");
    LOG_INF("  3 finger: tap (middle click), swipe up/down (F3/F4)");

    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
