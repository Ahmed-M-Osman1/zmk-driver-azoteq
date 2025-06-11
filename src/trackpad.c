/**
 * @file trackpad.c
 * @brief Trackpad gesture handling for IQS5XX
 */
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <iqs5xx.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <math.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// All your existing defines:
#define TRACKPAD_THREE_FINGER_CLICK_TIME    300
#define SCROLL_REPORT_DISTANCE              35

// All your existing static variables:
static bool isHolding = false;
static const struct device *trackpad;
static bool inputEventActive = false;
static uint8_t lastFingerCount = 0;
static int64_t threeFingerPressTime = 0;
static int16_t lastXScrollReport = 0;
static bool threeFingersPressed = false;
static uint8_t mouseSensitivity = 128;

struct {
    float x;
    float y;
} accumPos;

// Your gesture detection logic (modified to use input events):
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    // Get the input device
    const struct device *input_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(trackpad_input));
    if (!input_dev) return;

   // bool multiTouch = data->finger_count > 1;
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
            input_report(input_dev, INPUT_EV_KEY, INPUT_BTN_2, 1, true, K_NO_WAIT);
            input_report(input_dev, INPUT_EV_KEY, INPUT_BTN_2, 0, true, K_NO_WAIT);
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
                input_report(input_dev, INPUT_EV_KEY, INPUT_BTN_1, 1, true, K_NO_WAIT);
                input_report(input_dev, INPUT_EV_KEY, INPUT_BTN_1, 0, true, K_NO_WAIT);
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
                    input_report(input_dev, INPUT_EV_REL, INPUT_REL_HWHEEL, pan, false, K_NO_WAIT);
                }
                if (scroll != 0) {
                    input_report(input_dev, INPUT_EV_REL, INPUT_REL_WHEEL, scroll, true, K_NO_WAIT);
                }
                break;
        }

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                hasGesture = true;
                // Left click
                input_report(input_dev, INPUT_EV_KEY, INPUT_BTN_0, 1, true, K_NO_WAIT);
                input_report(input_dev, INPUT_EV_KEY, INPUT_BTN_0, 0, true, K_NO_WAIT);
                break;
            case GESTURE_TAP_AND_HOLD:
                // Drag n drop - hold left button
                input_report(input_dev, INPUT_EV_KEY, INPUT_BTN_0, 1, true, K_NO_WAIT);
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
            input_report(input_dev, INPUT_EV_REL, INPUT_REL_X, xp, false, K_NO_WAIT);
            input_report(input_dev, INPUT_EV_REL, INPUT_REL_Y, yp, true, K_NO_WAIT);
            accumPos.x = 0;
            accumPos.y = 0;
        }
    }

    lastFingerCount = data->finger_count;
}

static int trackpad_init(void) {
    trackpad = DEVICE_DT_GET(DT_NODELABEL(trackpad));
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

    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
