/**
 * @file trackpad.c
 * @brief Trackpad gesture handling for IQS5XX with extensive debugging
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

// Tuned defines for better performance:
#define TRACKPAD_THREE_FINGER_CLICK_TIME    200
#define SCROLL_REPORT_DISTANCE              15
#define MOVEMENT_THRESHOLD                  0.5f

// All your existing static variables:
static bool isHolding = false;
static const struct device *trackpad;
static uint8_t lastFingerCount = 0;
static int64_t threeFingerPressTime = 0;
static int16_t lastXScrollReport = 0;
static bool threeFingersPressed = false;
static uint8_t mouseSensitivity = 200;

struct {
    float x;
    float y;
} accumPos;

// Reference to the trackpad device which will have the input listener
static const struct device *trackpad_device = NULL;
static int event_count = 0;

// Send events through the trackpad device
static void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
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

// Your gesture detection logic with extensive debugging:
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    bool hasGesture = false;
    static int trigger_count = 0;

    trigger_count++;
    LOG_DBG("=== TRIGGER #%d ===", trigger_count);
    LOG_INF("Raw data: fingers=%d, gestures0=0x%02x, gestures1=0x%02x, rx=%d, ry=%d",
            data->finger_count, data->gestures0, data->gestures1, data->rx, data->ry);

    // Log finger details
    for (int i = 0; i < data->finger_count && i < 5; i++) {
        if (data->fingers[i].strength > 0) {
            LOG_DBG("Finger %d: pos=(%d,%d), strength=%d, area=%d",
                    i, data->fingers[i].ax, data->fingers[i].ay,
                    data->fingers[i].strength, data->fingers[i].area);
        }
    }

    // Three finger detection
    if(data->finger_count == 3 && !threeFingersPressed) {
        threeFingerPressTime = k_uptime_get();
        threeFingersPressed = true;
        LOG_INF("*** THREE FINGERS DETECTED - START ***");
    }

    if(data->finger_count == 0) {
        if (accumPos.x != 0 || accumPos.y != 0) {
            LOG_DBG("Resetting accumulated position: was %.2f,%.2f", accumPos.x, accumPos.y);
        }
        accumPos.x = 0;
        accumPos.y = 0;

        if(threeFingersPressed && k_uptime_get() - threeFingerPressTime < TRACKPAD_THREE_FINGER_CLICK_TIME) {
            hasGesture = true;
            LOG_INF("*** THREE FINGER CLICK DETECTED ***");
            // Middle click via input event
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
        }

        if (threeFingersPressed) {
            threeFingersPressed = false;
            LOG_INF("*** THREE FINGERS RELEASED ***");
        }
    }

    // Reset scroll if not two fingers
    if(data->finger_count != 2) {
        if (lastXScrollReport != 0) {
            LOG_DBG("Resetting scroll accumulator: was %d", lastXScrollReport);
            lastXScrollReport = 0;
        }
    }

    // Gesture handling
    if((data->gestures0 || data->gestures1) && !hasGesture) {
        LOG_INF("Hardware gesture detected: g0=0x%02x, g1=0x%02x", data->gestures0, data->gestures1);

        switch(data->gestures1) {
            case GESTURE_TWO_FINGER_TAP:
                hasGesture = true;
                LOG_INF("*** TWO FINGER TAP -> RIGHT CLICK ***");
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

                LOG_INF("*** SCROLL: pan=%d, scroll=%d (rx=%d, ry=%d) ***", pan, scroll, data->rx, data->ry);

                if (pan != 0) {
                    send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, pan, false);
                }
                if (scroll != 0) {
                    send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, scroll, true);
                }
                break;
            default:
                if (data->gestures1 != 0) {
                    LOG_WRN("Unknown gesture1: 0x%02x", data->gestures1);
                }
