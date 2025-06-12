/**
 * @file trackpad.c
 * @brief Enhanced trackpad gesture handling for IQS5XX with advanced gestures
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

// Enhanced gesture configuration
#define TRACKPAD_WIDTH                      1024    // IQS5XX resolution
#define TRACKPAD_HEIGHT                     1024
#define THREE_FINGER_CLICK_TIME             300
#define THREE_FINGER_SWIPE_THRESHOLD        100     // Distance for 3-finger swipe
#define CIRCULAR_SCROLL_RIM_PERCENT         15      // Outer 15% for circular scroll
#define TAP_TIMEOUT_MS                      150
#define INERTIAL_THRESHOLD                  20
#define INERTIAL_DECAY_PERCENT             25
#define SCROLL_REPORT_DISTANCE              35

// Gesture states
enum gesture_state {
    GESTURE_NONE,
    GESTURE_TAP_PENDING,
    GESTURE_CURSOR_MOVE,
    GESTURE_CIRCULAR_SCROLL,
    GESTURE_TWO_FINGER_SCROLL,
    GESTURE_THREE_FINGER_SWIPE,
    GESTURE_INERTIAL_CURSOR,
    GESTURE_DRAG_HOLD
};

// Enhanced gesture data structure
struct enhanced_gesture_data {
    // Basic trackpad state
    bool touching;
    enum gesture_state current_gesture;
    uint8_t finger_count;
    uint8_t last_finger_count;
    int64_t touch_start_time;
    int64_t last_touch_time;

    // Position tracking
    struct {
        float x, y;
    } accum_pos;

    uint16_t start_x, start_y;
    uint16_t current_x, current_y;
    uint16_t last_x, last_y;
    int16_t delta_x, delta_y;

    // Tap detection
    struct k_work_delayable tap_timeout_work;
    bool tap_pending;

    // Circular scroll
    bool in_scroll_zone;
    float last_angle;
    uint16_t center_x, center_y;
    uint32_t inner_radius_sq, outer_radius_sq;

    // Three-finger gestures
    bool three_fingers_detected;
    int64_t three_finger_start_time;
    uint16_t three_finger_start_x, three_finger_start_y;
    int16_t three_finger_delta_x, three_finger_delta_y;
    bool three_finger_swipe_triggered;

    // Two-finger scroll
    int16_t last_scroll_x;

    // Inertial cursor
    struct k_work_delayable inertial_work;
    float velocity_x, velocity_y;

    // Drag and drop
    bool is_holding;

    // Settings
    uint8_t mouse_sensitivity;
};

static struct enhanced_gesture_data gesture_data = {
    .mouse_sensitivity = 128,
    .center_x = TRACKPAD_WIDTH / 2,
    .center_y = TRACKPAD_HEIGHT / 2
};

static const struct device *trackpad;
static const struct device *trackpad_device = NULL;

// Helper functions
static float calculate_angle(uint16_t x, uint16_t y, uint16_t center_x, uint16_t center_y) {
    return atan2f((float)(x - center_x), (float)(y - center_y)) * 180.0f / M_PI;
}

static bool is_in_scroll_zone(uint16_t x, uint16_t y) {
    uint32_t dist_sq = (x - gesture_data.center_x) * (x - gesture_data.center_x) +
                       (y - gesture_data.center_y) * (y - gesture_data.center_y);
    return (dist_sq >= gesture_data.inner_radius_sq && dist_sq <= gesture_data.outer_radius_sq);
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

// Send keyboard events for Mission Control
static void send_mission_control_key(void) {
    LOG_INF("Sending Mission Control gesture (F3)");
    send_input_event(INPUT_EV_KEY, INPUT_KEY_F3, 1, true);
    send_input_event(INPUT_EV_KEY, INPUT_KEY_F3, 0, true);
}

static void send_desktop_switch_key(bool next) {
    LOG_INF("Sending desktop switch: %s", next ? "next" : "previous");
    // Send Ctrl+Arrow for desktop switching
    send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 1, false);
    send_input_event(INPUT_EV_KEY, next ? INPUT_KEY_RIGHT : INPUT_KEY_LEFT, 1, false);
    send_input_event(INPUT_EV_KEY, next ? INPUT_KEY_RIGHT : INPUT_KEY_LEFT, 0, false);
    send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 0, true);
}

// Work handlers
static void tap_timeout_handler(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);

    if (gesture_data.tap_pending && !gesture_data.touching) {
        LOG_DBG("Tap gesture detected - sending left click");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        gesture_data.tap_pending = false;
    }
    gesture_data.current_gesture = GESTURE_NONE;
}

static void inertial_cursor_handler(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);

    // Apply decay
    gesture_data.velocity_x *= (100.0f - INERTIAL_DECAY_PERCENT) / 100.0f;
    gesture_data.velocity_y *= (100.0f - INERTIAL_DECAY_PERCENT) / 100.0f;

    // Continue movement if velocity is significant
    if (fabsf(gesture_data.velocity_x) > 0.5f || fabsf(gesture_data.velocity_y) > 0.5f) {
        send_input_event(INPUT_EV_REL, INPUT_REL_X, (int)gesture_data.velocity_x, false);
        send_input_event(INPUT_EV_REL, INPUT_REL_Y, (int)gesture_data.velocity_y, true);
        k_work_reschedule(&gesture_data.inertial_work, K_MSEC(50));
    } else {
        gesture_data.current_gesture = GESTURE_NONE;
    }
}

// Enhanced gesture detection based on finger count and position
static uint8_t detect_enhanced_finger_count(const struct iqs5xx_rawdata *data) {
    // Use the actual finger count from IQS5XX when available
    if (data->finger_count > 0) {
        return data->finger_count;
    }

    // Fallback: analyze movement patterns for better detection
    uint32_t movement_magnitude = abs(data->rx) + abs(data->ry);

    if (movement_magnitude < 10) {
        return 1;  // Small movement, likely single finger
    } else if (movement_magnitude < 50) {
        return 2;  // Medium movement, could be 2 fingers
    } else {
        return 3;  // Large movement, likely 3+ fingers
    }
}

// Main enhanced gesture handler
static void enhanced_trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    int64_t now = k_uptime_get();
    bool has_gesture = false;

    // Update finger count with enhanced detection
    uint8_t detected_fingers = detect_enhanced_finger_count(data);
    gesture_data.finger_count = detected_fingers;

    // Extract absolute position from first finger
    gesture_data.current_x = data->fingers[0].ax;
    gesture_data.current_y = data->fingers[0].ay;

    // Calculate movement deltas
    gesture_data.delta_x = data->rx;
    gesture_data.delta_y = data->ry;

    // Touch start detection
    if (!gesture_data.touching && gesture_data.finger_count > 0) {
        LOG_DBG("Touch started with %d fingers at (%d, %d)",
                gesture_data.finger_count, gesture_data.current_x, gesture_data.current_y);

        gesture_data.touching = true;
        gesture_data.touch_start_time = now;
        gesture_data.start_x = gesture_data.current_x;
        gesture_data.start_y = gesture_data.current_y;
        gesture_data.current_gesture = GESTURE_NONE;
        gesture_data.accum_pos.x = 0;
        gesture_data.accum_pos.y = 0;

        // Three-finger detection
        if (gesture_data.finger_count >= 3) {
            gesture_data.three_fingers_detected = true;
            gesture_data.three_finger_start_time = now;
            gesture_data.three_finger_start_x = gesture_data.current_x;
            gesture_data.three_finger_start_y = gesture_data.current_y;
            gesture_data.three_finger_swipe_triggered = false;
            gesture_data.current_gesture = GESTURE_THREE_FINGER_SWIPE;
            LOG_DBG("Started 3-finger gesture detection");
        }
        // Check for circular scroll zone (single finger in rim area)
        else if (gesture_data.finger_count == 1 &&
                 is_in_scroll_zone(gesture_data.current_x, gesture_data.current_y)) {
            gesture_data.current_gesture = GESTURE_CIRCULAR_SCROLL;
            gesture_data.in_scroll_zone = true;
            gesture_data.last_angle = calculate_angle(gesture_data.current_x, gesture_data.current_y,
                                                     gesture_data.center_x, gesture_data.center_y);
            LOG_DBG("Started circular scroll");
        }
        // Two-finger gestures
        else if (gesture_data.finger_count == 2) {
            gesture_data.current_gesture = GESTURE_TWO_FINGER_SCROLL;
            gesture_data.last_scroll_x = 0;
            LOG_DBG("Started 2-finger scroll");
        }
        // Single finger tap detection
        else if (gesture_data.finger_count == 1) {
            gesture_data.tap_pending = true;
            gesture_data.current_gesture = GESTURE_TAP_PENDING;
            k_work_reschedule(&gesture_data.tap_timeout_work, K_MSEC(TAP_TIMEOUT_MS));
        }
    }

    // Touch end detection
    if (gesture_data.touching && gesture_data.finger_count == 0) {
        LOG_DBG("Touch ended");
        gesture_data.touching = false;

        // Handle three-finger click (quick tap)
        if (gesture_data.three_fingers_detected &&
            !gesture_data.three_finger_swipe_triggered &&
            (now - gesture_data.three_finger_start_time) < THREE_FINGER_CLICK_TIME) {

            LOG_INF("3-finger tap - sending middle click");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
            has_gesture = true;
        }

        // Handle drag and drop release
        if (gesture_data.is_holding) {
            send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
            gesture_data.is_holding = false;
        }

        // Check for inertial cursor
        if (gesture_data.current_gesture == GESTURE_CURSOR_MOVE &&
            (abs(gesture_data.delta_x) + abs(gesture_data.delta_y)) > INERTIAL_THRESHOLD) {

            float sens_multiplier = (float)gesture_data.mouse_sensitivity / 128.0f;
            gesture_data.velocity_x = -gesture_data.delta_y * sens_multiplier * 0.5f;
            gesture_data.velocity_y = gesture_data.delta_x * sens_multiplier * 0.5f;
            gesture_data.current_gesture = GESTURE_INERTIAL_CURSOR;
            k_work_reschedule(&gesture_data.inertial_work, K_MSEC(50));
            LOG_DBG("Starting inertial cursor");
        }

        // Reset state
        gesture_data.three_fingers_detected = false;
        gesture_data.in_scroll_zone = false;
        gesture_data.current_gesture = GESTURE_NONE;
    }

    // Process ongoing gestures
    if (gesture_data.touching && !has_gesture) {
        switch (gesture_data.current_gesture) {
            case GESTURE_THREE_FINGER_SWIPE:
                if (gesture_data.three_fingers_detected) {
                    // Calculate total movement from start
                    gesture_data.three_finger_delta_x = gesture_data.current_x - gesture_data.three_finger_start_x;
                    gesture_data.three_finger_delta_y = gesture_data.current_y - gesture_data.three_finger_start_y;

                    uint32_t total_movement = abs(gesture_data.three_finger_delta_x) +
                                            abs(gesture_data.three_finger_delta_y);

                    if (total_movement > THREE_FINGER_SWIPE_THRESHOLD && !gesture_data.three_finger_swipe_triggered) {
                        gesture_data.three_finger_swipe_triggered = true;

                        // Determine swipe direction
                        if (abs(gesture_data.three_finger_delta_y) > abs(gesture_data.three_finger_delta_x)) {
                            // Vertical swipe - Mission Control
                            LOG_INF("3-finger vertical swipe - Mission Control");
                            send_mission_control_key();
                        } else {
                            // Horizontal swipe - Desktop switching
                            bool swipe_right = gesture_data.three_finger_delta_x > 0;
                            LOG_INF("3-finger horizontal swipe %s", swipe_right ? "right" : "left");
                            send_desktop_switch_key(swipe_right);
                        }

                        // Reset for potential additional gestures
                        gesture_data.three_finger_start_x = gesture_data.current_x;
                        gesture_data.three_finger_start_y = gesture_data.current_y;
                    }
                }
                break;

            case GESTURE_CIRCULAR_SCROLL:
                if (gesture_data.in_scroll_zone &&
                    is_in_scroll_zone(gesture_data.current_x, gesture_data.current_y)) {

                    float current_angle = calculate_angle(gesture_data.current_x, gesture_data.current_y,
                                                         gesture_data.center_x, gesture_data.center_y);
                    float angle_diff = normalize_angle_diff(gesture_data.last_angle, current_angle);

                    if (fabsf(angle_diff) > 5.0f) {  // Minimum angle change
                        int scroll_value = (int)(angle_diff / 15.0f);  // Scale factor
                        if (scroll_value != 0) {
                            send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, scroll_value, true);
                            gesture_data.last_angle = current_angle;
                            LOG_DBG("Circular scroll: angle=%.1f, scroll=%d", (double)angle_diff, scroll_value);
                        }
                    }
                } else {
                    // Moved out of scroll zone - switch to cursor
                    gesture_data.current_gesture = GESTURE_CURSOR_MOVE;
                    gesture_data.in_scroll_zone = false;
                }
                break;

            case GESTURE_TAP_PENDING:
                // Check if movement is too large for tap
                if (abs(gesture_data.current_x - gesture_data.start_x) > 50 ||
                    abs(gesture_data.current_y - gesture_data.start_y) > 50) {
                    gesture_data.tap_pending = false;
                    gesture_data.current_gesture = GESTURE_CURSOR_MOVE;
                    k_work_cancel_delayable(&gesture_data.tap_timeout_work);
                    LOG_DBG("Tap cancelled, switching to cursor move");
                }
                // Suppress movement during tap detection
                break;

            case GESTURE_TWO_FINGER_SCROLL:
                // Handle built-in IQS5XX gestures for two-finger scroll
                if (data->gestures1 & GESTURE_SCROLLG) {
                    gesture_data.last_scroll_x += data->rx;
                    int8_t pan = -data->ry;
                    int8_t scroll = 0;

                    if (abs(gesture_data.last_scroll_x) > SCROLL_REPORT_DISTANCE) {
                        scroll = gesture_data.last_scroll_x >= 0 ? 1 : -1;
                        gesture_data.last_scroll_x = 0;
                    }

                    if (pan != 0) {
                        send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, pan, false);
                    }
                    if (scroll != 0) {
                        send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, scroll, true);
                    }
                    has_gesture = true;
                }
                break;

            case GESTURE_CURSOR_MOVE:
            default:
                // Normal cursor movement for single finger
                if (gesture_data.finger_count == 1 && (gesture_data.delta_x != 0 || gesture_data.delta_y != 0)) {
                    float sens_multiplier = (float)gesture_data.mouse_sensitivity / 128.0f;
                    gesture_data.accum_pos.x += -gesture_data.delta_y * sens_multiplier;
                    gesture_data.accum_pos.y += gesture_data.delta_x * sens_multiplier;

                    int16_t xp = (int16_t)gesture_data.accum_pos.x;
                    int16_t yp = (int16_t)gesture_data.accum_pos.y;

                    if (abs(xp) >= 1 || abs(yp) >= 1) {
                        send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
                        send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);
                        gesture_data.accum_pos.x -= xp;
                        gesture_data.accum_pos.y -= yp;
                    }
                }
                gesture_data.current_gesture = GESTURE_CURSOR_MOVE;
                break;
        }
    }

    // Handle built-in IQS5XX gestures (when not overridden by enhanced gestures)
    if (!has_gesture && (data->gestures0 || data->gestures1)) {
        // Single finger gestures
        if (data->gestures0 & GESTURE_SINGLE_TAP && gesture_data.finger_count <= 1) {
            send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
            has_gesture = true;
        }

        if (data->gestures0 & GESTURE_TAP_AND_HOLD) {
            send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
            gesture_data.is_holding = true;
            has_gesture = true;
        }

        // Multi-finger gestures (if not already handled)
        if (data->gestures1 & GESTURE_TWO_FINGER_TAP && gesture_data.current_gesture != GESTURE_TWO_FINGER_SCROLL) {
            send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 0, true);
            has_gesture = true;
        }
    }

    // Update state
    gesture_data.last_finger_count = gesture_data.finger_count;
    gesture_data.last_x = gesture_data.current_x;
    gesture_data.last_y = gesture_data.current_y;
    gesture_data.last_touch_time = now;
}

static int enhanced_trackpad_init(void) {
    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }

    // Store reference for input events
    trackpad_device = trackpad;

    // Initialize work items
    k_work_init_delayable(&gesture_data.tap_timeout_work, tap_timeout_handler);
    k_work_init_delayable(&gesture_data.inertial_work, inertial_cursor_handler);

    // Calculate circular scroll parameters
    uint16_t max_radius = MIN(gesture_data.center_x, gesture_data.center_y);
    uint16_t rim_width = (max_radius * CIRCULAR_SCROLL_RIM_PERCENT) / 100;
    uint16_t inner_radius = max_radius - rim_width;

    gesture_data.inner_radius_sq = inner_radius * inner_radius;
    gesture_data.outer_radius_sq = max_radius * max_radius;

    // Initialize state
    gesture_data.current_gesture = GESTURE_NONE;
    gesture_data.touching = false;
    gesture_data.accum_pos.x = 0;
    gesture_data.accum_pos.y = 0;

    // Set the enhanced trigger handler
    int err = iqs5xx_trigger_set(trackpad, enhanced_trackpad_trigger_handler);
    if (err) {
        return -EINVAL;
    }

    LOG_INF("Enhanced trackpad gesture handler initialized");
    LOG_INF("Circular scroll zone: inner_r=%d, outer_r=%d, rim_percent=%d",
            inner_radius, max_radius, CIRCULAR_SCROLL_RIM_PERCENT);

    return 0;
}

SYS_INIT(enhanced_trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
