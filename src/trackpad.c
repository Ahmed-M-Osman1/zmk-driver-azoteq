/**
 * @file trackpad.c
 * @brief Enhanced trackpad gesture handling for IQS5XX with fixed scroll direction
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

// Enhanced timing constants for better responsiveness
#define TRACKPAD_THREE_FINGER_CLICK_TIME    200  // Reduced from 300ms
#define SCROLL_REPORT_DISTANCE              25   // Reduced from 35 for more sensitive scrolling
#define TAP_DEBOUNCE_TIME                   50   // New: debounce for taps
#define MOVEMENT_THRESHOLD                  3    // Minimum movement to register

// State variables
static bool isHolding = false;
static const struct device *trackpad;
static uint8_t lastFingerCount = 0;
static int64_t threeFingerPressTime = 0;
static int64_t lastTapTime = 0; // For tap debouncing
static int16_t lastXScrollReport = 0;
static int16_t lastYScrollReport = 0; // Add Y scroll tracking
static bool threeFingersPressed = false;
static uint8_t mouseSensitivity = 128;

// Movement accumulator with better precision
struct {
    float x;
    float y;
} accumPos;

// Reference to the trackpad device
static const struct device *trackpad_device = NULL;

// Enhanced input event sending with better error handling
static void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    LOG_DBG("Input event: type=%d, code=%d, value=%d, sync=%d", type, code, value, sync);

    if (trackpad_device) {
        int ret = input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
        if (ret < 0) {
            LOG_WRN("Failed to send input event: %d", ret);
        }
    }
}

// Enhanced gesture detection with better performance and fixed scroll direction
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    bool hasGesture = false;
    int64_t currentTime = k_uptime_get();

    // Three finger detection with improved timing
    if(data->finger_count == 3 && !threeFingersPressed) {
        threeFingerPressTime = currentTime;
        threeFingersPressed = true;
        LOG_DBG("Three fingers detected");
    }

    if(data->finger_count == 0) {
        accumPos.x = 0;
        accumPos.y = 0;

        // Handle three finger middle click
        if(threeFingersPressed &&
           (currentTime - threeFingerPressTime) < TRACKPAD_THREE_FINGER_CLICK_TIME) {
            hasGesture = true;
            LOG_DBG("Three finger middle click");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
        }
        threeFingersPressed = false;

        // Release hold if active
        if(isHolding) {
            send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
            isHolding = false;
            LOG_DBG("Released hold");
        }
    }

    // Reset scroll tracking if not two fingers
    if(data->finger_count != 2) {
        lastXScrollReport = 0;
        lastYScrollReport = 0;
    }

    // Enhanced gesture handling with better debouncing
    if((data->gestures0 || data->gestures1) && !hasGesture) {
        switch(data->gestures1) {
            case GESTURE_TWO_FINGER_TAP:
                if((currentTime - lastTapTime) > TAP_DEBOUNCE_TIME) {
                    hasGesture = true;
                    lastTapTime = currentTime;
                    LOG_DBG("Right click (two finger tap)");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, true);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 0, true);
                }
                break;

            case GESTURE_SCROLLG:
                hasGesture = true;

                // FIXED: Correct scroll direction mapping
                // For vertical scrolling: use data->ry (relative Y)
                // For horizontal scrolling: use data->rx (relative X)
                lastYScrollReport += data->ry;  // Vertical scroll accumulator
                lastXScrollReport += data->rx;  // Horizontal scroll accumulator

                int8_t vertical_scroll = 0;
                int8_t horizontal_scroll = 0;

                // Check for vertical scroll
                if(abs(lastYScrollReport) >= SCROLL_REPORT_DISTANCE) {
                    // Negative ry = scroll up, positive ry = scroll down
                    vertical_scroll = (lastYScrollReport > 0) ? -1 : 1;
                    lastYScrollReport = 0;
                    LOG_DBG("Vertical scroll: %d", vertical_scroll);
                }

                // Check for horizontal scroll
                if(abs(lastXScrollReport) >= SCROLL_REPORT_DISTANCE) {
                    // Negative rx = scroll left, positive rx = scroll right
                    horizontal_scroll = (lastXScrollReport > 0) ? 1 : -1;
                    lastXScrollReport = 0;
                    LOG_DBG("Horizontal scroll: %d", horizontal_scroll);
                }

                // Send scroll events - each with sync=true for immediate processing
                if (vertical_scroll != 0) {
                    send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, vertical_scroll, true);
                }
                if (horizontal_scroll != 0) {
                    send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, horizontal_scroll, true);
                }
                break;
        }

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                if((currentTime - lastTapTime) > TAP_DEBOUNCE_TIME) {
                    hasGesture = true;
                    lastTapTime = currentTime;
                    LOG_DBG("Left click (single tap)");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                }
                break;

            case GESTURE_TAP_AND_HOLD:
                if(!isHolding) {
                    hasGesture = true;
                    LOG_DBG("Drag start (tap and hold)");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    isHolding = true;
                }
                break;
        }
    }

    // Enhanced movement handling with better precision and filtering
    if(!hasGesture && data->finger_count == 1) {
        float sensMp = (float)mouseSensitivity / 128.0F;

        // Apply movement threshold to reduce jitter
        int16_t raw_x = -data->ry;  // Swapped and inverted for correct direction
        int16_t raw_y = data->rx;   // Swapped for correct direction

        if(abs(raw_x) >= MOVEMENT_THRESHOLD || abs(raw_y) >= MOVEMENT_THRESHOLD) {
            accumPos.x += raw_x * sensMp;
            accumPos.y += raw_y * sensMp;

            int16_t xp = (int16_t)accumPos.x;
            int16_t yp = (int16_t)accumPos.y;

            if(abs(xp) >= 1 || abs(yp) >= 1) {
                LOG_DBG("Mouse movement: x=%d, y=%d", xp, yp);
                send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
                send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);

                // Subtract the integer part, keep the fractional part for better precision
                accumPos.x -= xp;
                accumPos.y -= yp;
            }
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

    // Verify device is ready
    if (!device_is_ready(trackpad)) {
        LOG_ERR("IQS5XX device not ready");
        return -ENODEV;
    }

    // Store reference for input events
    trackpad_device = trackpad;

    // Initialize accumulator
    accumPos.x = 0;
    accumPos.y = 0;

    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        LOG_ERR("Failed to set trackpad trigger handler: %d", err);
        return -EINVAL;
    }

    LOG_INF("Enhanced trackpad gesture handler initialized successfully");
    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
