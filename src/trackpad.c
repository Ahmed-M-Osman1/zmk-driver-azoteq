/**
 * @file trackpad.c
 * @brief Simple enhanced trackpad gestures - builds on your working version
 */
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <math.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include "iqs5xx.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Keep your existing constants
#define TRACKPAD_THREE_FINGER_CLICK_TIME    300
#define SCROLL_REPORT_DISTANCE              35

// Add new gesture constants
#define THREE_FINGER_SWIPE_THRESHOLD        150     // Larger threshold for reliable detection
#define TRACKPAD_WIDTH                      1024    // Adjust to match your IQS5XX
#define TRACKPAD_HEIGHT                     1024
#define CIRCULAR_SCROLL_RIM_PERCENT         20      // Larger rim area (20% instead of 15%)

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Keep your existing variables
static bool isHolding = false;
static const struct device *trackpad;
static uint8_t lastFingerCount = 0;
static int64_t threeFingerPressTime = 0;
static int16_t lastXScrollReport = 0;
static bool threeFingersPressed = false;
static uint8_t mouseSensitivity = 128;

// Add enhanced gesture tracking
static bool enhanced_gestures_enabled = true;
static uint16_t center_x = TRACKPAD_WIDTH / 2;
static uint16_t center_y = TRACKPAD_HEIGHT / 2;
static uint32_t inner_radius_sq, outer_radius_sq;
static bool circular_scroll_active = false;
static float last_scroll_angle = 0;

// Three-finger swipe tracking
static bool three_finger_swipe_started = false;
static uint16_t three_finger_start_x = 0, three_finger_start_y = 0;
static int64_t three_finger_swipe_start_time = 0;

struct {
    float x;
    float y;
} accumPos;

static const struct device *trackpad_device = NULL;

// Helper functions for enhanced gestures
static float calculate_angle(uint16_t x, uint16_t y) {
    float dx = (float)(x - center_x);
    float dy = (float)(y - center_y);
    return atan2f(dy, dx) * 180.0f / M_PI;
}

static bool is_in_scroll_zone(uint16_t x, uint16_t y) {
    uint32_t dist_sq = (x - center_x) * (x - center_x) + (y - center_y) * (y - center_y);
    return (dist_sq >= inner_radius_sq && dist_sq <= outer_radius_sq);
}

static float normalize_angle_diff(float angle1, float angle2) {
    float diff = angle2 - angle1;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    return diff;
}

// Send events through the trackpad device
static void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    LOG_DBG("Input event: type=%d, code=%d, value=%d, sync=%d", type, code, value, sync);

    if (trackpad_device) {
        input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
    }
}

// Send Mission Control key (F3)
static void send_mission_control_key(void) {
    LOG_INF("=== 3-FINGER SWIPE UP - MISSION CONTROL ===");
    send_input_event(INPUT_EV_KEY, INPUT_KEY_F3, 1, true);
    send_input_event(INPUT_EV_KEY, INPUT_KEY_F3, 0, true);
}

// Send desktop switching keys
static void send_desktop_switch_key(bool next) {
    LOG_INF("=== 3-FINGER SWIPE %s - DESKTOP SWITCH ===", next ? "RIGHT" : "LEFT");
    // Send Ctrl+Arrow for desktop switching
    send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 1, false);
    send_input_event(INPUT_EV_KEY, next ? INPUT_KEY_RIGHT : INPUT_KEY_LEFT, 1, false);
    send_input_event(INPUT_EV_KEY, next ? INPUT_KEY_RIGHT : INPUT_KEY_LEFT, 0, false);
    send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 0, true);
}

// Enhanced finger detection (improved version of your logic)
static uint8_t detect_enhanced_finger_count(const struct iqs5xx_rawdata *data) {
    // Start with hardware finger count if available
    if (data->finger_count > 0) {
        return data->finger_count;
    }

    // Fallback: analyze movement magnitude and finger strength
    uint32_t movement_magnitude = abs(data->rx) + abs(data->ry);
    uint16_t total_strength = 0;
    uint8_t active_fingers = 0;

    // Count fingers with significant strength
    for (int i = 0; i < 5; i++) {
        if (data->fingers[i].strength > 100) {  // Adjust threshold as needed
            active_fingers++;
            total_strength += data->fingers[i].strength;
        }
    }

    if (active_fingers > 0) {
        return active_fingers;
    }

    // Final fallback: movement-based detection
    if (movement_magnitude > 50) {
        return 3;  // Large movement suggests multiple fingers
    } else if (movement_magnitude > 20) {
        return 2;  // Medium movement
    } else if (movement_magnitude > 0) {
        return 1;  // Small movement
    }

    return 0;  // No movement
}

// Enhanced gesture handler - builds on your existing logic
static void enhanced_trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    int64_t now = k_uptime_get();
    bool hasGesture = false;

    // Get enhanced finger count
    uint8_t current_finger_count = detect_enhanced_finger_count(data);

    // Log finger count changes for debugging
    if (current_finger_count != lastFingerCount) {
        LOG_INF("*** FINGER COUNT CHANGED: %d -> %d ***", lastFingerCount, current_finger_count);
        if (current_finger_count >= 3) {
            LOG_INF("*** THREE+ FINGERS DETECTED! ***");
        }
    }

    // === THREE FINGER GESTURES (Enhanced) ===
    if (current_finger_count >= 3 && !threeFingersPressed) {
        threeFingerPressTime = now;
        threeFingersPressed = true;
        three_finger_swipe_started = false;

        // Use first finger position for reference
        three_finger_start_x = data->fingers[0].ax;
        three_finger_start_y = data->fingers[0].ay;
        three_finger_swipe_start_time = now;

        LOG_INF("=== 3-FINGER GESTURE STARTED at (%d, %d) ===",
                three_finger_start_x, three_finger_start_y);
    }

    // Handle ongoing three-finger gesture
    if (threeFingersPressed && current_finger_count >= 3) {
        if (!three_finger_swipe_started) {
            // Check for swipe movement
            uint16_t current_x = data->fingers[0].ax;
            uint16_t current_y = data->fingers[0].ay;

            int16_t delta_x = current_x - three_finger_start_x;
            int16_t delta_y = current_y - three_finger_start_y;
            uint32_t total_movement = abs(delta_x) + abs(delta_y);

            LOG_DBG("3-finger movement: dx=%d, dy=%d, total=%d", delta_x, delta_y, total_movement);

            if (total_movement > THREE_FINGER_SWIPE_THRESHOLD) {
                three_finger_swipe_started = true;
                hasGesture = true;

                // Determine swipe direction
                if (abs(delta_y) > abs(delta_x)) {
                    // Vertical swipe
                    if (delta_y < 0) {  // Swipe up
                        send_mission_control_key();
                    } else {  // Swipe down
                        send_mission_control_key();  // Also Mission Control for now
                    }
                } else {
                    // Horizontal swipe
                    if (delta_x > 0) {  // Swipe right
                        send_desktop_switch_key(true);   // Next desktop
                    } else {  // Swipe left
                        send_desktop_switch_key(false);  // Previous desktop
                    }
                }
            }
        }
    }

    // === CIRCULAR SCROLL (Enhanced) ===
    if (current_finger_count == 1 && enhanced_gestures_enabled) {
        uint16_t touch_x = data->fingers[0].ax;
        uint16_t touch_y = data->fingers[0].ay;

        // Check if touch is in scroll rim
        if (is_in_scroll_zone(touch_x, touch_y)) {
            if (!circular_scroll_active) {
                circular_scroll_active = true;
                last_scroll_angle = calculate_angle(touch_x, touch_y);
                LOG_INF("=== CIRCULAR SCROLL STARTED ===");
            } else {
                // Handle circular scrolling
                float current_angle = calculate_angle(touch_x, touch_y);
                float angle_diff = normalize_angle_diff(last_scroll_angle, current_angle);

                if (fabsf(angle_diff) > 10.0f) {  // Minimum angle change
                    int scroll_value = (int)(angle_diff / 20.0f);  // Scale factor
                    if (scroll_value != 0) {
                        send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, scroll_value, true);
                        last_scroll_angle = current_angle;
                        hasGesture = true;
                        LOG_DBG("Circular scroll: angle=%.1f, scroll=%d", (double)angle_diff, scroll_value);
                    }
                }
            }
        } else {
            circular_scroll_active = false;
        }
    } else {
        circular_scroll_active = false;
    }

    // === TOUCH END HANDLING ===
    if (current_finger_count == 0) {
        accumPos.x = 0;
        accumPos.y = 0;

        // Handle three-finger click (quick tap)
        if (threeFingersPressed && !three_finger_swipe_started &&
            (now - threeFingerPressTime) < TRACKPAD_THREE_FINGER_CLICK_TIME) {

            LOG_INF("=== 3-FINGER TAP - MIDDLE CLICK ===");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
            hasGesture = true;
        }

        threeFingersPressed = false;
        circular_scroll_active = false;
    }

    // Reset scroll if not two fingers
    if (current_finger_count != 2) {
        lastXScrollReport = 0;
    }

    // === YOUR EXISTING GESTURE HANDLING (Keep exactly as is) ===
    if ((data->gestures0 || data->gestures1) && !hasGesture) {
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
                    send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, pan, true);
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

    // === YOUR EXISTING MOVEMENT HANDLING (Keep exactly as is) ===
    if (!hasGesture && current_finger_count == 1 && !circular_scroll_active) {
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

    lastFingerCount = current_finger_count;
}

static int enhanced_trackpad_init(void) {
    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }

    // Store reference for input events
    trackpad_device = trackpad;

    accumPos.x = 0;
    accumPos.y = 0;

    // Calculate circular scroll parameters
    uint16_t max_radius = MIN(center_x, center_y);
    uint16_t rim_width = (max_radius * CIRCULAR_SCROLL_RIM_PERCENT) / 100;
    uint16_t inner_radius = max_radius - rim_width;

    inner_radius_sq = inner_radius * inner_radius;
    outer_radius_sq = max_radius * max_radius;

    LOG_INF("Enhanced trackpad gestures initialized");
    LOG_INF("Circular scroll zone: inner_r=%d, outer_r=%d", inner_radius, max_radius);
    LOG_INF("3-finger swipe threshold: %d pixels", THREE_FINGER_SWIPE_THRESHOLD);

    int err = iqs5xx_trigger_set(trackpad, enhanced_trackpad_trigger_handler);
    if(err) {
        return -EINVAL;
    }

    return 0;
}

SYS_INIT(enhanced_trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
