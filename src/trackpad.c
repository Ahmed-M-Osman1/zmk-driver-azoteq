// src/trackpad.c - ALL GESTURES: Clicks, Scrolling, Three-finger swipes
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

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Tuned defines for better performance
#define TRACKPAD_THREE_FINGER_CLICK_TIME    200
#define SCROLL_REPORT_DISTANCE              15
#define MOVEMENT_THRESHOLD                  0.3f
#define THREE_FINGER_SWIPE_MIN_DIST         50    // Minimum distance for swipe

// State variables for proper drag handling
static bool isDragging = false;
static bool dragStartSent = false;
static const struct device *trackpad;
static uint8_t lastFingerCount = 0;
static int64_t threeFingerPressTime = 0;
static int16_t lastXScrollReport = 0;
static bool threeFingersPressed = false;
static uint8_t mouseSensitivity = 200;
static int64_t last_event_time = 0;

// Single-finger state for SOFTWARE tap detection
static bool singleFingerActive = false;
static int64_t singleFingerStartTime = 0;

// Two-finger state for SOFTWARE tap detection and scrolling
static bool twoFingerActive = false;
static int64_t twoFingerStartTime = 0;
static struct {
    uint16_t x, y;
} twoFingerStartPos[2];
static bool twoFingerScrolling = false;

// Three-finger state for swipe detection
static struct {
    int16_t x, y;
} threeFingerStartPos[3];
static bool threeFingerGestureTriggered = false;

struct {
    float x;
    float y;
} accumPos;

// Reference to the trackpad device
static const struct device *trackpad_device = NULL;
static int event_count = 0;

// Send Control+Up key combination for Mission Control
static void send_control_up(void) {
    LOG_INF("*** SENDING CONTROL+UP (MISSION CONTROL) ***");

    // Clear any existing HID state first
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Press Control
    int ret1 = zmk_hid_keyboard_press(LCTRL);
    if (ret1 < 0) {
        LOG_ERR("Failed to press CTRL: %d", ret1);
        return;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Press Up Arrow
    int ret2 = zmk_hid_keyboard_press(UP_ARROW);
    if (ret2 < 0) {
        LOG_ERR("Failed to press UP: %d", ret2);
        zmk_hid_keyboard_release(LCTRL);
        zmk_endpoints_send_report(0x07);
        return;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(50);

    // Release Up Arrow
    zmk_hid_keyboard_release(UP_ARROW);
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Release Control
    zmk_hid_keyboard_release(LCTRL);
    zmk_endpoints_send_report(0x07);

    LOG_INF("Control+Up complete - Mission Control should appear!");
}

// Send Control+Down for Application Windows
static void send_control_down(void) {
    LOG_INF("*** SENDING CONTROL+DOWN (APPLICATION WINDOWS) ***");

    // Clear any existing HID state first
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Press Control
    int ret1 = zmk_hid_keyboard_press(LCTRL);
    if (ret1 < 0) {
        LOG_ERR("Failed to press CTRL: %d", ret1);
        return;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Press Down Arrow
    int ret2 = zmk_hid_keyboard_press(DOWN_ARROW);
    if (ret2 < 0) {
        LOG_ERR("Failed to press DOWN: %d", ret2);
        zmk_hid_keyboard_release(LCTRL);
        zmk_endpoints_send_report(0x07);
        return;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(50);

    // Release Down Arrow
    zmk_hid_keyboard_release(DOWN_ARROW);
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Release Control
    zmk_hid_keyboard_release(LCTRL);
    zmk_endpoints_send_report(0x07);

    LOG_INF("Control+Down complete - Application Windows should appear!");
}

// Send events through the trackpad device
static void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    event_count++;
    if (type == INPUT_EV_KEY) {
        LOG_INF("CLICK #%d: btn=%d, val=%d", event_count, code, value);
    } else if (abs(value) > 3) {
        LOG_INF("SCROLL/MOVE #%d: type=%d, code=%d, val=%d", event_count, type, code, value);
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

    // 2. Single-finger tap detection
    if (lastFingerCount == 1 && data->finger_count == 0 && singleFingerActive) {
        return true;
    }

    // 3. Two-finger tap detection
    if (lastFingerCount == 2 && data->finger_count == 0 && twoFingerActive) {
        return true;
    }

    // 4. Three-finger gesture events
    if (data->finger_count == 3 || (lastFingerCount == 3 && data->finger_count == 0)) {
        return true;
    }

    // 5. Hardware gestures
    if (data->gestures1 & (GESTURE_TWO_FINGER_TAP | GESTURE_SCROLLG)) {
        return true;
    }

    return false;
}

// Calculate average Y position of three fingers
static float calculate_average_y(const struct iqs5xx_rawdata *data) {
    float sum = 0;
    for (int i = 0; i < 3; i++) {
        sum += data->fingers[i].ay;
    }
    return sum / 3.0f;
}

// SOFTWARE gesture detection with ALL gestures
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    bool hasGesture = false;
    static int trigger_count = 0;
    int64_t current_time = k_uptime_get();

    trigger_count++;

    // CRITICAL: Check for click events first
    bool has_clicks = has_click_events(data);
    bool has_gesture = (data->gestures0 != 0) || (data->gestures1 != 0);
    bool finger_count_changed = (lastFingerCount != data->finger_count);

    // Rate limiting bypass for clicks/gestures
    if (!has_clicks && !has_gesture && !finger_count_changed &&
        (current_time - last_event_time < 20)) {
        return;
    }
    last_event_time = current_time;

    // Enhanced logging
    if (finger_count_changed || has_gesture || has_clicks) {
        const char* reason = has_clicks ? " [CLICK-PRIORITY]" :
                           has_gesture ? " [GESTURE]" : " [FINGER-CHANGE]";
        LOG_INF("TRIGGER #%d: fingers=%d, g0=0x%02x, g1=0x%02x, rel=%d/%d%s",
                trigger_count, data->finger_count, data->gestures0, data->gestures1,
                data->rx, data->ry, reason);
    }

    // ============================================================================
    // PRIORITY 1: FINGER DETECTION - Start timers and store positions
    // ============================================================================

    // Single finger detection
    if(data->finger_count == 1 && !singleFingerActive) {
        singleFingerStartTime = current_time;
        singleFingerActive = true;
        LOG_INF("*** SINGLE FINGER DETECTED - START ***");
    }

    // Two finger detection
    if(data->finger_count == 2 && !twoFingerActive) {
        twoFingerStartTime = current_time;
        twoFingerActive = true;
        twoFingerScrolling = false;
        // Store initial positions for scroll detection
        twoFingerStartPos[0].x = data->fingers[0].ax;
        twoFingerStartPos[0].y = data->fingers[0].ay;
        twoFingerStartPos[1].x = data->fingers[1].ax;
        twoFingerStartPos[1].y = data->fingers[1].ay;
        LOG_INF("*** TWO FINGERS DETECTED - START ***");
    }

    // Three finger detection
    if(data->finger_count == 3 && !threeFingersPressed) {
        threeFingerPressTime = current_time;
        threeFingersPressed = true;
        threeFingerGestureTriggered = false;
        // Store initial positions for swipe detection
        for (int i = 0; i < 3; i++) {
            threeFingerStartPos[i].x = data->fingers[i].ax;
            threeFingerStartPos[i].y = data->fingers[i].ay;
        }
        LOG_INF("*** THREE FINGERS DETECTED - START ***");
    }

    // ============================================================================
    // PRIORITY 2: ACTIVE GESTURE PROCESSING
    // ============================================================================

    // Handle two-finger scrolling
    if (data->finger_count == 2 && twoFingerActive &&
        current_time - twoFingerStartTime > 100) { // Wait 100ms before scrolling

        // Calculate movement from start position
        float dx0 = (float)(data->fingers[0].ax - twoFingerStartPos[0].x);
        float dy0 = (float)(data->fingers[0].ay - twoFingerStartPos[0].y);
        float dx1 = (float)(data->fingers[1].ax - twoFingerStartPos[1].x);
        float dy1 = (float)(data->fingers[1].ay - twoFingerStartPos[1].y);

        // Check if both fingers moved in same direction (scrolling)
        float avg_dx = (dx0 + dx1) / 2.0f;
        float avg_dy = (dy0 + dy1) / 2.0f;

        if (fabsf(avg_dy) > 20.0f || fabsf(avg_dx) > 20.0f) { // Minimum movement for scroll
            if (!twoFingerScrolling) {
                twoFingerScrolling = true;
                LOG_INF("*** TWO FINGER SCROLLING STARTED ***");
            }

            // Send scroll events
            if (fabsf(avg_dy) > fabsf(avg_dx)) {
                // Vertical scroll
                int scroll_y = (int)(avg_dy / 15.0f);
                if (abs(scroll_y) > 0) {
                    LOG_INF("*** VERTICAL SCROLL: %d ***", -scroll_y);
                    send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, -scroll_y, true);
                    // Update start positions
                    twoFingerStartPos[0].y = data->fingers[0].ay;
                    twoFingerStartPos[1].y = data->fingers[1].ay;
                }
            } else {
                // Horizontal scroll
                int scroll_x = (int)(avg_dx / 15.0f);
                if (abs(scroll_x) > 0) {
                    LOG_INF("*** HORIZONTAL SCROLL: %d ***", -scroll_x);
                    send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, -scroll_x, true);
                    // Update start positions
                    twoFingerStartPos[0].x = data->fingers[0].ax;
                    twoFingerStartPos[1].x = data->fingers[1].ax;
                }
            }
            hasGesture = true;
        }
    }

    // Handle three-finger swipe detection
    if (data->finger_count == 3 && threeFingersPressed && !threeFingerGestureTriggered &&
        current_time - threeFingerPressTime > 100) { // Wait 100ms before checking

        // Calculate average movement in Y direction
        float initialAvgY = (float)(threeFingerStartPos[0].y + threeFingerStartPos[1].y + threeFingerStartPos[2].y) / 3.0f;
        float currentAvgY = calculate_average_y(data);
        float yMovement = currentAvgY - initialAvgY;

        if (fabsf(yMovement) > THREE_FINGER_SWIPE_MIN_DIST) {
            threeFingerGestureTriggered = true;
            hasGesture = true;

            if (yMovement > 0) {
                LOG_INF("*** THREE FINGER SWIPE DOWN -> APPLICATION WINDOWS ***");
                send_control_down();
            } else {
                LOG_INF("*** THREE FINGER SWIPE UP -> MISSION CONTROL ***");
                send_control_up();
            }
        }
    }

    // ============================================================================
    // PRIORITY 3: FINGER RELEASE - Handle clicks
    // ============================================================================

    if(data->finger_count == 0) {
        // Reset accumulated position
        accumPos.x = 0;
        accumPos.y = 0;

        // IMMEDIATE single finger click detection
        if(singleFingerActive && current_time - singleFingerStartTime < 300) {
            hasGesture = true;
            LOG_INF("*** IMMEDIATE SINGLE FINGER CLICK -> LEFT CLICK ***");
            send_input_event(INPUT_EV_KEY, 0x110, 1, false);
            send_input_event(INPUT_EV_KEY, 0x110, 0, true);
        }

        // IMMEDIATE two finger click detection (only if not scrolling)
        if(twoFingerActive && !twoFingerScrolling && current_time - twoFingerStartTime < 300) {
            hasGesture = true;
            LOG_INF("*** IMMEDIATE TWO FINGER CLICK -> RIGHT CLICK ***");
            send_input_event(INPUT_EV_KEY, 0x111, 1, false);
            send_input_event(INPUT_EV_KEY, 0x111, 0, true);
        }

        // IMMEDIATE three finger click detection (only if no swipe)
        if(threeFingersPressed && !threeFingerGestureTriggered &&
           current_time - threeFingerPressTime < TRACKPAD_THREE_FINGER_CLICK_TIME) {
            hasGesture = true;
            LOG_INF("*** IMMEDIATE THREE FINGER CLICK -> MIDDLE CLICK ***");
            send_input_event(INPUT_EV_KEY, 0x112, 1, false);
            send_input_event(INPUT_EV_KEY, 0x112, 0, true);
        }

        // IMMEDIATE drag end handling
        if (isDragging && dragStartSent) {
            LOG_INF("*** IMMEDIATE DRAG END - RELEASING BUTTON ***");
            send_input_event(INPUT_EV_KEY, 0x110, 0, true);
            isDragging = false;
            dragStartSent = false;
        }

        // Reset all finger states
        if (singleFingerActive) {
            singleFingerActive = false;
            LOG_INF("*** SINGLE FINGER RELEASED ***");
        }
        if (twoFingerActive) {
            twoFingerActive = false;
            twoFingerScrolling = false;
            LOG_INF("*** TWO FINGERS RELEASED ***");
        }
        if (threeFingersPressed) {
            threeFingersPressed = false;
            threeFingerGestureTriggered = false;
            LOG_INF("*** THREE FINGERS RELEASED ***");
        }
    }

    // Reset scroll if not two fingers
    if(data->finger_count != 2) {
        lastXScrollReport = 0;
    }

    // ============================================================================
    // PRIORITY 4: HARDWARE GESTURE PROCESSING (fallback)
    // ============================================================================

    if((data->gestures0 || data->gestures1) && !hasGesture) {
        LOG_INF("*** HARDWARE GESTURE: g0=0x%02x, g1=0x%02x ***", data->gestures0, data->gestures1);

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                if (!isDragging) {
                    hasGesture = true;
                    LOG_INF("*** HARDWARE SINGLE TAP -> LEFT CLICK ***");
                    send_input_event(INPUT_EV_KEY, 0x110, 1, false);
                    send_input_event(INPUT_EV_KEY, 0x110, 0, true);
                }
                break;
            case GESTURE_TAP_AND_HOLD:
                if (!isDragging) {
                    LOG_INF("*** HARDWARE TAP AND HOLD -> DRAG START ***");
                    send_input_event(INPUT_EV_KEY, 0x110, 1, true);
                    isDragging = true;
                    dragStartSent = true;
                }
                break;
        }

        switch(data->gestures1) {
            case GESTURE_TWO_FINGER_TAP:
                hasGesture = true;
                LOG_INF("*** HARDWARE TWO FINGER TAP -> RIGHT CLICK ***");
                send_input_event(INPUT_EV_KEY, 0x111, 1, false);
                send_input_event(INPUT_EV_KEY, 0x111, 0, true);
                break;
            case GESTURE_SCROLLG:
                hasGesture = true;
                // Use hardware scroll if available
                if (data->ry != 0) {
                    int scroll = -data->ry / 10;
                    if (abs(scroll) > 0) {
                        LOG_INF("*** HARDWARE SCROLL: %d ***", scroll);
                        send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, scroll, true);
                    }
                }
                break;
        }
    }

    // ============================================================================
    // PRIORITY 5: MOVEMENT PROCESSING
    // ============================================================================

    if(!hasGesture && data->finger_count == 1) {
        float sensMp = (float)mouseSensitivity/128.0F;

        if (data->rx != 0 || data->ry != 0) {
            accumPos.x += -data->rx * sensMp;
            accumPos.y += -data->ry * sensMp;

            int16_t xp = (int16_t)accumPos.x;
            int16_t yp = (int16_t)accumPos.y;

            if(fabsf(accumPos.x) >= MOVEMENT_THRESHOLD || fabsf(accumPos.y) >= MOVEMENT_THRESHOLD) {
                send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
                send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);

                accumPos.x -= xp;
                accumPos.y -= yp;
            }
        }
    }

    // Update finger count
    if (lastFingerCount != data->finger_count) {
        LOG_INF("Finger count changed: %d -> %d", lastFingerCount, data->finger_count);
        lastFingerCount = data->finger_count;
    }
}

static int trackpad_init(void) {
    LOG_INF("=== COMPLETE GESTURE TRACKPAD INIT START ===");

    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }
    LOG_INF("Found IQS5XX device: %p", trackpad);

    trackpad_device = trackpad;
    LOG_INF("Set trackpad device reference: %p", trackpad_device);

    // Initialize all state
    accumPos.x = 0;
    accumPos.y = 0;
    isDragging = false;
    dragStartSent = false;
    singleFingerActive = false;
    twoFingerActive = false;
    twoFingerScrolling = false;
    threeFingersPressed = false;
    threeFingerGestureTriggered = false;
    LOG_INF("All gestures enabled: Clicks, Scrolling, Three-finger swipes");

    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return -EINVAL;
    }

    LOG_INF("=== COMPLETE GESTURE TRACKPAD INIT COMPLETE ===");
    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
