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
#include <math.h>
#include "iqs5xx.h"
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Tuned defines for better performance
#define TRACKPAD_THREE_FINGER_CLICK_TIME    200
#define SCROLL_REPORT_DISTANCE              15
#define MOVEMENT_THRESHOLD                  0.3f  // Reduced for faster response

// State variables for proper drag handling
static bool isDragging = false;
static bool dragStartSent = false;
static const struct device *trackpad;
static uint8_t lastFingerCount = 0;
static int64_t threeFingerPressTime = 0;
static int16_t lastXScrollReport = 0;
static bool threeFingersPressed = false;
static uint8_t mouseSensitivity = 200;
static int64_t last_event_time = 0; // For rate-limiting

// Two-finger state for tap detection
static bool twoFingerActive = false;
static int64_t twoFingerStartTime = 0;

struct {
    float x;
    float y;
} accumPos;

// Reference to the trackpad device
static const struct device *trackpad_device = NULL;
static int event_count = 0;

// Send events through the trackpad device
static void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
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

// Check if current data contains click-worthy events that should bypass rate limiting
static bool has_click_events(const struct iqs5xx_rawdata *data) {
    // 1. Hardware gestures that generate clicks
    if (data->gestures0 & (GESTURE_SINGLE_TAP | GESTURE_TAP_AND_HOLD)) {
        return true;
    }

    // 2. Two-finger tap detection (finger count transition 2->0 quickly)
    if (lastFingerCount == 2 && data->finger_count == 0 && twoFingerActive) {
        return true;
    }

    // 3. Three-finger gesture events
    if (data->finger_count == 3 || (lastFingerCount == 3 && data->finger_count == 0)) {
        return true;
    }

    // 4. Hardware two-finger tap
    if (data->gestures1 & GESTURE_TWO_FINGER_TAP) {
        return true;
    }

    return false;
}

// WORKING gesture detection from your early version + rate limiting bypass
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    bool hasGesture = false;
    static int trigger_count = 0;
    int64_t current_time = k_uptime_get();

    trigger_count++;

    // CRITICAL: Check for click events first
    bool has_clicks = has_click_events(data);
    bool has_gesture = (data->gestures0 != 0) || (data->gestures1 != 0);
    bool finger_count_changed = (lastFingerCount != data->finger_count);

    // UPDATED RATE LIMITING: Skip rate limiting for clicks, gestures, and finger count changes
    if (!has_clicks && !has_gesture && !finger_count_changed &&
        (current_time - last_event_time < 20)) {
        return; // Skip only pure movement events
    }
    last_event_time = current_time;

    // Enhanced logging with click priority indicator
    if (finger_count_changed || has_gesture || has_clicks) {
        const char* reason = has_clicks ? " [CLICK-PRIORITY]" :
                           has_gesture ? " [GESTURE]" : " [FINGER-CHANGE]";
        LOG_INF("TRIGGER #%d: fingers=%d, g0=0x%02x, g1=0x%02x, rel=%d/%d%s",
                trigger_count, data->finger_count, data->gestures0, data->gestures1,
                data->rx, data->ry, reason);
    }

    // Log finger details
    for (int i = 0; i < data->finger_count && i < 5; i++) {
        if (data->fingers[i].strength > 0) {
            LOG_DBG("Finger %d: pos=(%d,%d), strength=%d, area=%d",
                    i, data->fingers[i].ax, data->fingers[i].ay,
                    data->fingers[i].strength, data->fingers[i].area);
        }
    }

    // ============================================================================
    // PRIORITY 1: IMMEDIATE CLICK PROCESSING - Handle ALL clicks FIRST
    // ============================================================================

    // Three finger detection
    if(data->finger_count == 3 && !threeFingersPressed) {
        threeFingerPressTime = current_time;
        threeFingersPressed = true;
        LOG_INF("*** THREE FINGERS DETECTED - START ***");
    }

    // Two finger detection
    if(data->finger_count == 2 && !twoFingerActive) {
        twoFingerStartTime = current_time;
        twoFingerActive = true;
        LOG_INF("*** TWO FINGERS DETECTED - START ***");
    }

    // Handle finger release (end of gestures) - IMMEDIATE CLICK PROCESSING
    if(data->finger_count == 0) {
        // Reset accumulated position
        if (accumPos.x != 0 || accumPos.y != 0) {
            LOG_DBG("Resetting accumulated position: was %.2f,%.2f", accumPos.x, accumPos.y);
        }
        accumPos.x = 0;
        accumPos.y = 0;

        // IMMEDIATE three finger click detection
        if(threeFingersPressed && current_time - threeFingerPressTime < TRACKPAD_THREE_FINGER_CLICK_TIME) {
            hasGesture = true;
            LOG_INF("*** IMMEDIATE THREE FINGER CLICK -> MIDDLE CLICK ***");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, false);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
        }

        // IMMEDIATE two finger click detection
        if(twoFingerActive && current_time - twoFingerStartTime < 300) {
            hasGesture = true;
            LOG_INF("*** IMMEDIATE TWO FINGER CLICK -> RIGHT CLICK ***");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, false);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 0, true);
        }

        // IMMEDIATE drag end handling
        if (isDragging && dragStartSent) {
            LOG_INF("*** IMMEDIATE DRAG END - RELEASING BUTTON ***");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
            isDragging = false;
            dragStartSent = false;
        }

        // Reset states
        if (threeFingersPressed) {
            threeFingersPressed = false;
            LOG_INF("*** THREE FINGERS RELEASED ***");
        }
        if (twoFingerActive) {
            twoFingerActive = false;
            LOG_INF("*** TWO FINGERS RELEASED ***");
        }
    }

    // Reset scroll if not two fingers
    if(data->finger_count != 2) {
        if (lastXScrollReport != 0) {
            LOG_DBG("Resetting scroll accumulator: was %d", lastXScrollReport);
            lastXScrollReport = 0;
        }
    }

    // IMMEDIATE hardware gesture handling with ABSOLUTE PRIORITY
    if((data->gestures0 || data->gestures1) && !hasGesture) {
        LOG_INF("*** IMMEDIATE HARDWARE GESTURE: g0=0x%02x, g1=0x%02x ***", data->gestures0, data->gestures1);

        // Process gesture1 events
        switch(data->gestures1) {
            case GESTURE_TWO_FINGER_TAP:
                hasGesture = true;
                LOG_INF("*** IMMEDIATE HARDWARE TWO FINGER TAP -> RIGHT CLICK ***");
                send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, false);
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

                LOG_INF("*** IMMEDIATE SCROLL: pan=%d, scroll=%d ***", pan, scroll);

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
                break;
        }

        // Process gesture0 events
        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // Only handle single tap if we're not already dragging
                if (!isDragging) {
                    hasGesture = true;
                    LOG_INF("*** IMMEDIATE SINGLE TAP -> LEFT CLICK ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                }
                break;
            case GESTURE_TAP_AND_HOLD:
                // IMMEDIATE drag start - only send the button press ONCE
                if (!isDragging) {
                    LOG_INF("*** IMMEDIATE TAP AND HOLD -> DRAG START ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    isDragging = true;
                    dragStartSent = true;
                } else {
                    LOG_DBG("Already dragging, ignoring repeated tap-and-hold");
                }
                break;
            default:
                if (data->gestures0 != 0) {
                    LOG_WRN("Unknown gesture0: 0x%02x", data->gestures0);
                }
                break;
        }

        // RETURN IMMEDIATELY after processing gestures - don't process movement
        if (hasGesture) {
            goto update_finger_count;
        }
    }

    // ============================================================================
    // PRIORITY 2: MOVEMENT PROCESSING - Only if no clicks were processed above
    // ============================================================================

    // Movement handling - works during normal movement AND during drag
    if(!hasGesture && data->finger_count == 1) {
        float sensMp = (float)mouseSensitivity/128.0F;

        // Process movement if we have any
        if (data->rx != 0 || data->ry != 0) {
            // Direct accumulation with correct axis mapping
            accumPos.x += -data->rx * sensMp;  // Invert X for natural movement
            accumPos.y += -data->ry * sensMp;  // Invert Y for natural movement

            int16_t xp = (int16_t)accumPos.x;
            int16_t yp = (int16_t)accumPos.y;

            // Lower threshold for smoother movement
            if(fabsf(accumPos.x) >= MOVEMENT_THRESHOLD || fabsf(accumPos.y) >= MOVEMENT_THRESHOLD) {
                LOG_DBG("Mouse movement: rx=%d,ry=%d -> move=%d,%d", data->rx, data->ry, xp, yp);

                // Send movement events (works both for normal movement and drag)
                send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
                send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);

                // Reset accumulation, keeping fractional part
                accumPos.x -= xp;
                accumPos.y -= yp;
            }
        }
    }

update_finger_count:
    // Update finger count
    if (lastFingerCount != data->finger_count) {
        LOG_INF("Finger count changed: %d -> %d", lastFingerCount, data->finger_count);
        lastFingerCount = data->finger_count;
    }
}

static int trackpad_init(void) {
    LOG_INF("=== WORKING TRACKPAD INIT START ===");

    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }
    LOG_INF("Found IQS5XX device: %p", trackpad);

    // Store reference for input events
    trackpad_device = trackpad;
    LOG_INF("Set trackpad device reference: %p", trackpad_device);

    // Initialize state
    accumPos.x = 0;
    accumPos.y = 0;
    isDragging = false;
    dragStartSent = false;
    LOG_INF("Reset accumulated position and drag state - NO RATE LIMIT for clicks");

    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return -EINVAL;
    }
    LOG_INF("Trigger handler set successfully");

    LOG_INF("=== WORKING TRACKPAD INIT COMPLETE ===");
    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
