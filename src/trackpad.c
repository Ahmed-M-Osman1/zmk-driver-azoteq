// src/trackpad.c - FIXED modular version with proper gesture flow
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
static int64_t last_event_time = 0;

// FIXED: Input event sending with proper sync behavior
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    event_count++;

    if (type == INPUT_EV_KEY) {
        LOG_INF("CLICK #%d: btn=%d, val=%d", event_count, code, value);
    } else if (abs(value) > 5) {
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

// FIXED: Direct hardware gesture processing like the simple version
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;
    int64_t current_time = k_uptime_get();

    trigger_count++;

    // FIXED: Handle hardware gestures DIRECTLY first, just like the simple version
    bool gesture_handled = false;

    if (data->gestures0 || data->gestures1) {
        LOG_INF("TRIGGER #%d: fingers=%d, g0=0x%02x, g1=0x%02x, rel=%d/%d",
                trigger_count, data->finger_count, data->gestures0, data->gestures1, data->rx, data->ry);

        // Handle gestures1 (two-finger gestures) - DIRECT like simple version
        if (data->gestures1) {
            switch (data->gestures1) {
                case GESTURE_TWO_FINGER_TAP:
                    LOG_INF("*** DIRECT TWO FINGER TAP -> RIGHT CLICK ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_RIGHT, 1, false);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_RIGHT, 0, true);
                    gesture_handled = true;
                    break;

                case GESTURE_SCROLLG:
                    LOG_INF("*** DIRECT SCROLL: rx=%d, ry=%d ***", data->rx, data->ry);
                    int8_t scroll_y = -data->ry / 15;
                    int8_t scroll_x = data->rx / 15;

                    if (scroll_y != 0) {
                        send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, scroll_y, false);
                    }
                    if (scroll_x != 0) {
                        send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, scroll_x, true);
                    }
                    gesture_handled = true;
                    break;
            }
        }

        // Handle gestures0 (single-finger gestures) - DIRECT like simple version
        if (data->gestures0) {
            switch (data->gestures0) {
                case GESTURE_SINGLE_TAP:
                    // DIRECT single tap handling - no complex state checking
                    if (!g_gesture_state.isDragging) {
                        LOG_INF("*** DIRECT SINGLE TAP -> LEFT CLICK ***");
                        send_input_event(INPUT_EV_KEY, INPUT_BTN_LEFT, 1, false);
                        send_input_event(INPUT_EV_KEY, INPUT_BTN_LEFT, 0, true);
                        gesture_handled = true;
                    }
                    break;

                case GESTURE_TAP_AND_HOLD:
                    // DIRECT drag start
                    if (!g_gesture_state.isDragging) {
                        LOG_INF("*** DIRECT TAP AND HOLD -> DRAG START ***");
                        send_input_event(INPUT_EV_KEY, INPUT_BTN_LEFT, 1, true);
                        g_gesture_state.isDragging = true;
                        g_gesture_state.dragStartSent = true;
                        gesture_handled = true;
                    }
                    break;
            }
        }
    } else {
        // Only log finger count changes when no gestures
        static uint8_t last_logged_fingers = 255;
        if (g_gesture_state.lastFingerCount != data->finger_count) {
            LOG_INF("TRIGGER #%d: fingers=%d->%d, g0=0x%02x, g1=0x%02x, rel=%d/%d",
                    trigger_count, g_gesture_state.lastFingerCount, data->finger_count,
                    data->gestures0, data->gestures1, data->rx, data->ry);
        }
    }

    // Rate limit ONLY non-gesture events
    if (!gesture_handled && (current_time - last_event_time < 20)) {
        return;
    }
    last_event_time = current_time;

    // FIXED: Finger count transitions - handle resets immediately
    switch (data->finger_count) {
        case 0:
            // IMMEDIATE cleanup when all fingers lifted
            if (g_gesture_state.isDragging) {
                LOG_INF("*** DRAG END - IMMEDIATE RELEASE ***");
                send_input_event(INPUT_EV_KEY, INPUT_BTN_LEFT, 0, true);
                g_gesture_state.isDragging = false;
                g_gesture_state.dragStartSent = false;
            }
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;

        case 1:
            // Reset other gestures, handle single finger
            if (g_gesture_state.twoFingerActive) reset_two_finger_state(&g_gesture_state);
            if (g_gesture_state.threeFingersPressed) reset_three_finger_state(&g_gesture_state);

            // Only call modular handler if no direct gesture was processed
            if (!gesture_handled) {
                handle_single_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 2:
            // Reset other gestures, handle two finger (if not already handled directly)
            if (g_gesture_state.isDragging && !gesture_handled) {
                LOG_INF("*** DRAG END - TWO FINGERS ***");
                send_input_event(INPUT_EV_KEY, INPUT_BTN_LEFT, 0, true);
                g_gesture_state.isDragging = false;
                g_gesture_state.dragStartSent = false;
                reset_single_finger_state(&g_gesture_state);
            }
            if (g_gesture_state.threeFingersPressed) reset_three_finger_state(&g_gesture_state);

            // Only call modular handler if no direct gesture was processed
            if (!gesture_handled) {
                handle_two_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 3:
            // Reset other gestures, handle three finger
            if (g_gesture_state.isDragging) {
                LOG_INF("*** DRAG END - THREE FINGERS ***");
                send_input_event(INPUT_EV_KEY, INPUT_BTN_LEFT, 0, true);
                g_gesture_state.isDragging = false;
                g_gesture_state.dragStartSent = false;
                reset_single_finger_state(&g_gesture_state);
            }
            if (g_gesture_state.twoFingerActive) reset_two_finger_state(&g_gesture_state);

            handle_three_finger_gestures(dev, data, &g_gesture_state);
            break;

        default:
            // 4+ fingers - reset all
            if (g_gesture_state.isDragging) {
                send_input_event(INPUT_EV_KEY, INPUT_BTN_LEFT, 0, true);
                g_gesture_state.isDragging = false;
                g_gesture_state.dragStartSent = false;
            }
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;
    }

    // Update finger count
    g_gesture_state.lastFingerCount = data->finger_count;
}

static int trackpad_init(void) {
    LOG_INF("=== FIXED MODULAR TRACKPAD INIT START ===");

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

    // Initialize gesture state
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
    g_gesture_state.mouseSensitivity = 200;
    LOG_INF("Initialized gesture state with sensitivity 200");

    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return -EINVAL;
    }
    LOG_INF("Trigger handler set successfully");

    LOG_INF("=== FIXED MODULAR TRACKPAD INIT COMPLETE ===");
    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
