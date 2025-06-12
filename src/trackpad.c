/**
 * @file trackpad.c
 * @brief Trackpad gesture handling for IQS5XX
 */
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <math.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include "iqs5xx.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// All your existing defines:
#define TRACKPAD_THREE_FINGER_CLICK_TIME    300
#define SCROLL_REPORT_DISTANCE              35

// All your existing static variables:
static bool isHolding = false;
static const struct device *trackpad;
static uint8_t lastFingerCount = 0;
static int64_t threeFingerPressTime = 0;
static int16_t lastXScrollReport = 0;
static bool threeFingersPressed = false;
static uint8_t mouseSensitivity = 128;

struct {
    float x;
    float y;
} accumPos;

// Simple input device reference
static const struct device *input_dev = NULL;

// Since we can't access ZMK headers directly in external modules,
// we'll use the input system which ZMK will handle
static void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    LOG_DBG("ahmed :::: Input event: type=%d, code=%d, value=%d, sync=%d", type, code, value, sync);

    // Try to get an input device - this might work since input is part of Zephyr
    if (!input_dev) {
        // For now, just log - we'll need ZMK integration later
        return;
    }

    // Use Zephyr's input system which ZMK should pick up
    input_report(input_dev, type, code, value, sync, K_NO_WAIT);
}

// Your gesture detection logic (now using real ZMK mouse):
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    bool hasGesture = false;

    // Three finger detection
    if(data->finger_count == 3 && !threeFingersPressed) {
        threeFingerPressTime = k_uptime_get();
        threeFingersPressed = true;
    }

    if(data->finger_count == 0) {
        accumPos.x = 0;
        accumPos.y = 0;
        if(threeFingersPressed && k_uptime_get() - threeFingerPressTime < TRACKPAD_THREE_FINGER_CLICK_TIME) {
            hasGesture = true;
            // Middle click via input event
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
        }
        threeFingersPressed = false;
    }

    // Reset scroll if not two fingers
    if(data->finger_count != 2) {
        lastXScrollReport = 0;
    }

    // Gesture handling
    if((data->gestures0 || data->gestures1) && !hasGesture) {
        switch(data->gestures1) {
            case GESTURE_TWO_FINGER_TAP:
                hasGesture = true;
                // Right click
                send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, true);
                send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 0, true);
                break;
            case GESTURE_SCROLLG:
                hasGesture = true;
                lastXScrollReport += data->rx;
                int8_t pan = -data->ry;
                int8_t scroll = 0;
                if(abs(lastXScrollReport) - (int16_t)SCROLL_REPORT_DISTANCE > 0) {
                    scroll = lastXScrollReport >= 0 ? 1 : -1;
                    lastXScrollReport = 0;
                }
                // Send scroll events
                if (pan != 0) {
                    send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, pan, false);
                }
                if (scroll != 0) {
                    send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, scroll, true);
                }
                break;
        }

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                hasGesture = true;
                // Left click
                send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                break;
            case GESTURE_TAP_AND_HOLD:
                // Drag n drop - hold left button
                send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                isHolding = true;
                break;
        }
    }

    // Movement handling
    if(!hasGesture && data->finger_count == 1) {
        float sensMp = (float)mouseSensitivity/128.0F;
        accumPos.x += -data->ry * sensMp;
        accumPos.y += data->rx * sensMp;
        int16_t xp = accumPos.x;
        int16_t yp = accumPos.y;

        if(fabsf(accumPos.x) >= 1 || fabsf(accumPos.y) >= 1) {
            // Send movement events
            send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
            send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);
            accumPos.x = 0;
            accumPos.y = 0;
        }
    }

    lastFingerCount = data->finger_count;
}

static int trackpad_init(void) {
    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }

    accumPos.x = 0;
    accumPos.y = 0;

    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        return -EINVAL;
    }

    LOG_INF("Trackpad gesture handler initialized");
    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
