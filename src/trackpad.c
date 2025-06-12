/**
 * @file trackpad.c
 * @brief Improved trackpad gestures with better finger detection and reduced lag
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

// ===== TUNED CONSTANTS =====
#define THREE_FINGER_SWIPE_THRESHOLD        100     // Reduced for easier triggering
#define THREE_FINGER_TAP_TIMEOUT            200     // Reduced timeout
#define CIRCULAR_SCROLL_SENSITIVITY         8.0f    // More sensitive scrolling
#define MOVEMENT_THRESHOLD                  5       // Minimum movement to register
#define FINGER_STRENGTH_THRESHOLD           50      // Lower threshold for finger detection
#define TRACKPAD_WIDTH                      1024
#define TRACKPAD_HEIGHT                     1024

// ===== GESTURE STATE =====
typedef enum {
    GESTURE_NONE,
    GESTURE_CURSOR_MOVE,
    GESTURE_THREE_FINGER_TAP,
    GESTURE_THREE_FINGER_SWIPE,
    GESTURE_CIRCULAR_SCROLL,
    GESTURE_TWO_FINGER_SCROLL
} gesture_state_t;

static gesture_state_t current_gesture = GESTURE_NONE;
static const struct device *trackpad_device = NULL;

// Movement tracking
static struct {
    float x, y;
} accumPos = {0, 0};

// Three-finger gesture tracking
static struct {
    bool active;
    uint16_t start_x, start_y;
    int64_t start_time;
    bool swipe_detected;
} three_finger = {0};

// Circular scroll tracking
static struct {
    bool active;
    float last_angle;
    uint16_t center_x, center_y;
    uint32_t inner_radius_sq, outer_radius_sq;
} circular_scroll = {0};

// Two-finger scroll tracking
static struct {
    int16_t last_x_report;
} two_finger_scroll = {0};

// General state
static uint8_t last_finger_count = 0;
static uint8_t mouse_sensitivity = 128;

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ===== HELPER FUNCTIONS =====

static void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    if (trackpad_device) {
        input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
    }
}

static float calculate_angle(uint16_t x, uint16_t y) {
    float dx = (float)(x - circular_scroll.center_x);
    float dy = (float)(y - circular_scroll.center_y);
    return atan2f(dy, dx) * 180.0f / M_PI;
}

static bool is_in_scroll_rim(uint16_t x, uint16_t y) {
    uint32_t dist_sq = (x - circular_scroll.center_x) * (x - circular_scroll.center_x) +
                       (y - circular_scroll.center_y) * (y - circular_scroll.center_y);
    return (dist_sq >= circular_scroll.inner_radius_sq && dist_sq <= circular_scroll.outer_radius_sq);
}

static float normalize_angle_diff(float angle1, float angle2) {
    float diff = angle2 - angle1;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    return diff;
}

// ===== IMPROVED FINGER DETECTION =====
static uint8_t detect_finger_count(const struct iqs5xx_rawdata *data) {
    // Primary detection: use hardware finger count if reliable
    if (data->finger_count > 0 && data->finger_count <= 5) {
        return data->finger_count;
    }

    // Secondary detection: count fingers with strength above threshold
    uint8_t active_fingers = 0;
    for (int i = 0; i < 5; i++) {
        if (data->fingers[i].strength > FINGER_STRENGTH_THRESHOLD) {
            active_fingers++;
        }
    }

    if (active_fingers > 0) {
        return active_fingers;
    }

    // Fallback: movement-based detection
    uint32_t movement = abs(data->rx) + abs(data->ry);
    if (movement > 100) return 3;      // Large movement = likely multi-finger
    if (movement > 30) return 2;       // Medium movement = likely two fingers
    if (movement > 5) return 1;        // Small movement = one finger

    return 0;
}

// ===== GESTURE HANDLERS =====

static void handle_three_finger_start(const struct iqs5xx_rawdata *data) {
    three_finger.active = true;
    three_finger.start_time = k_uptime_get();
    three_finger.swipe_detected = false;

    // Use centroid of all active fingers for more stable tracking
    uint16_t sum_x = 0, sum_y = 0;
    uint8_t count = 0;

    for (int i = 0; i < 3; i++) {
        if (data->fingers[i].strength > FINGER_STRENGTH_THRESHOLD) {
            sum_x += data->fingers[i].ax;
            sum_y += data->fingers[i].ay;
            count++;
        }
    }

    if (count > 0) {
        three_finger.start_x = sum_x / count;
        three_finger.start_y = sum_y / count;
    } else {
        // Fallback to first finger
        three_finger.start_x = data->fingers[0].ax;
        three_finger.start_y = data->fingers[0].ay;
    }

    current_gesture = GESTURE_THREE_FINGER_TAP;
    LOG_INF("=== 3-FINGER GESTURE STARTED at (%d, %d) ===",
            three_finger.start_x, three_finger.start_y);
}

static void handle_three_finger_movement(const struct iqs5xx_rawdata *data) {
    if (!three_finger.active || three_finger.swipe_detected) {
        return;
    }

    // Calculate current centroid
    uint16_t sum_x = 0, sum_y = 0;
    uint8_t count = 0;

    for (int i = 0; i < 3; i++) {
        if (data->fingers[i].strength > FINGER_STRENGTH_THRESHOLD) {
            sum_x += data->fingers[i].ax;
            sum_y += data->fingers[i].ay;
            count++;
        }
    }

    if (count == 0) return;

    uint16_t current_x = sum_x / count;
    uint16_t current_y = sum_y / count;

    int16_t delta_x = current_x - three_finger.start_x;
    int16_t delta_y = current_y - three_finger.start_y;
    uint32_t total_movement = abs(delta_x) + abs(delta_y);

    LOG_DBG("3-finger movement: dx=%d, dy=%d, total=%d", delta_x, delta_y, total_movement);

    if (total_movement > THREE_FINGER_SWIPE_THRESHOLD) {
        three_finger.swipe_detected = true;
        current_gesture = GESTURE_THREE_FINGER_SWIPE;

        // Determine primary direction
        if (abs(delta_y) > abs(delta_x)) {
            // Vertical swipe
            if (delta_y < 0) {
                LOG_INF("=== 3-FINGER SWIPE UP - MISSION CONTROL ===");
                send_input_event(INPUT_EV_KEY, INPUT_KEY_F3, 1, true);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_F3, 0, true);
            } else {
                LOG_INF("=== 3-FINGER SWIPE DOWN - MISSION CONTROL ===");
                send_input_event(INPUT_EV_KEY, INPUT_KEY_F3, 1, true);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_F3, 0, true);
            }
        } else {
            // Horizontal swipe
            if (delta_x > 0) {
                LOG_INF("=== 3-FINGER SWIPE RIGHT - DESKTOP SWITCH ===");
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 1, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_RIGHT, 1, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_RIGHT, 0, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 0, true);
            } else {
                LOG_INF("=== 3-FINGER SWIPE LEFT - DESKTOP SWITCH ===");
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 1, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFT, 1, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFT, 0, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 0, true);
            }
        }
    }
}

static void handle_three_finger_end(void) {
    if (!three_finger.active) return;

    int64_t duration = k_uptime_get() - three_finger.start_time;

    if (!three_finger.swipe_detected && duration < THREE_FINGER_TAP_TIMEOUT) {
        LOG_INF("=== 3-FINGER TAP - MIDDLE CLICK ===");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, true);
        send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
    }

    three_finger.active = false;
    three_finger.swipe_detected = false;
    current_gesture = GESTURE_NONE;
}

static void handle_circular_scroll(const struct iqs5xx_rawdata *data) {
    uint16_t touch_x = data->fingers[0].ax;
    uint16_t touch_y = data->fingers[0].ay;

    if (is_in_scroll_rim(touch_x, touch_y)) {
        if (!circular_scroll.active) {
            circular_scroll.active = true;
            circular_scroll.last_angle = calculate_angle(touch_x, touch_y);
            current_gesture = GESTURE_CIRCULAR_SCROLL;
            LOG_INF("=== CIRCULAR SCROLL STARTED ===");
        } else {
            float current_angle = calculate_angle(touch_x, touch_y);
            float angle_diff = normalize_angle_diff(circular_scroll.last_angle, current_angle);

            if (fabsf(angle_diff) > CIRCULAR_SCROLL_SENSITIVITY) {
                int scroll_value = (int)(angle_diff / CIRCULAR_SCROLL_SENSITIVITY);
                if (scroll_value != 0) {
                    send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, -scroll_value, true);
                    circular_scroll.last_angle = current_angle;
                    LOG_DBG("Circular scroll: angle=%.1f, scroll=%d", (double)angle_diff, scroll_value);
                }
            }
        }
    } else {
        if (circular_scroll.active) {
            circular_scroll.active = false;
            current_gesture = GESTURE_NONE;
        }
    }
}

static void handle_cursor_movement(const struct iqs5xx_rawdata *data) {
    // Only move cursor if no other gesture is active
    if (current_gesture != GESTURE_NONE && current_gesture != GESTURE_CURSOR_MOVE) {
        return;
    }

    // Check if movement is significant enough
    if (abs(data->rx) < MOVEMENT_THRESHOLD && abs(data->ry) < MOVEMENT_THRESHOLD) {
        return;
    }

    current_gesture = GESTURE_CURSOR_MOVE;

    float sens_multiplier = (float)mouse_sensitivity / 128.0f;
    accumPos.x += -data->ry * sens_multiplier;
    accumPos.y += data->rx * sens_multiplier;

    int16_t move_x = (int16_t)accumPos.x;
    int16_t move_y = (int16_t)accumPos.y;

    if (abs(move_x) >= 1 || abs(move_y) >= 1) {
        send_input_event(INPUT_EV_REL, INPUT_REL_X, move_x, false);
        send_input_event(INPUT_EV_REL, INPUT_REL_Y, move_y, true);

        accumPos.x -= move_x;
        accumPos.y -= move_y;
    }
}

static void handle_two_finger_scroll(const struct iqs5xx_rawdata *data) {
    current_gesture = GESTURE_TWO_FINGER_SCROLL;

    // Accumulate horizontal scroll
    two_finger_scroll.last_x_report += data->rx;

    int8_t vertical_scroll = -data->ry;
    int8_t horizontal_scroll = 0;

    // Check if we need to report horizontal scroll
    if (abs(two_finger_scroll.last_x_report) >= 35) {
        horizontal_scroll = two_finger_scroll.last_x_report >= 0 ? 1 : -1;
        two_finger_scroll.last_x_report = 0;
    }

    // Send scroll events
    if (vertical_scroll != 0) {
        send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, vertical_scroll, true);
    }
    if (horizontal_scroll != 0) {
        send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, horizontal_scroll, true);
    }
}

// ===== MAIN TRIGGER HANDLER =====
static void enhanced_trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    uint8_t finger_count = detect_finger_count(data);
    bool finger_count_changed = (finger_count != last_finger_count);

    // Log finger count changes
    if (finger_count_changed) {
        LOG_INF("*** FINGER COUNT CHANGED: %d -> %d ***", last_finger_count, finger_count);
        if (finger_count >= 3) {
            LOG_INF("*** THREE+ FINGERS DETECTED! ***");
        }
    }

    // ===== GESTURE PRIORITY HANDLING =====

    // Handle three-finger gestures (highest priority)
    if (finger_count >= 3) {
        if (!three_finger.active) {
            handle_three_finger_start(data);
        } else {
            handle_three_finger_movement(data);
        }
    } else if (three_finger.active) {
        handle_three_finger_end();
    }

    // Handle single-finger gestures (circular scroll or cursor movement)
    else if (finger_count == 1) {
        // Reset two-finger scroll state
        two_finger_scroll.last_x_report = 0;

        // Try circular scroll first
        handle_circular_scroll(data);

        // If not circular scrolling, handle cursor movement
        if (current_gesture != GESTURE_CIRCULAR_SCROLL) {
            handle_cursor_movement(data);
        }
    }

    // Handle two-finger scroll
    else if (finger_count == 2) {
        // End any circular scroll
        if (circular_scroll.active) {
            circular_scroll.active = false;
        }

        // Handle built-in two-finger scroll gesture
        if (data->gestures1 & GESTURE_SCROLLG) {
            handle_two_finger_scroll(data);
        }
    }

    // Handle finger lift (reset all gestures)
    else if (finger_count == 0) {
        accumPos.x = 0;
        accumPos.y = 0;

        if (three_finger.active) {
            handle_three_finger_end();
        }

        if (circular_scroll.active) {
            circular_scroll.active = false;
        }

        two_finger_scroll.last_x_report = 0;
        current_gesture = GESTURE_NONE;
    }

    // ===== BUILT-IN GESTURE HANDLING =====
    // Only process if no enhanced gesture is active
    if (current_gesture == GESTURE_NONE && (data->gestures0 || data->gestures1)) {
        if (data->gestures0 & GESTURE_SINGLE_TAP) {
            LOG_DBG("Single tap detected");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        }

        if (data->gestures1 & GESTURE_TWO_FINGER_TAP) {
            LOG_DBG("Two-finger tap detected");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 0, true);
        }
    }

    last_finger_count = finger_count;
}

// ===== INITIALIZATION =====
static int enhanced_trackpad_init(void) {
    const struct device *trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }

    trackpad_device = trackpad;

    // Initialize circular scroll parameters
    circular_scroll.center_x = TRACKPAD_WIDTH / 2;
    circular_scroll.center_y = TRACKPAD_HEIGHT / 2;

    uint16_t max_radius = MIN(circular_scroll.center_x, circular_scroll.center_y);
    uint16_t rim_width = max_radius / 5;  // 20% rim
    uint16_t inner_radius = max_radius - rim_width;

    circular_scroll.inner_radius_sq = inner_radius * inner_radius;
    circular_scroll.outer_radius_sq = max_radius * max_radius;

    LOG_INF("Enhanced trackpad gestures initialized");
    LOG_INF("Circular scroll zone: inner_r=%d, outer_r=%d", inner_radius, max_radius);
    LOG_INF("3-finger swipe threshold: %d pixels", THREE_FINGER_SWIPE_THRESHOLD);

    int err = iqs5xx_trigger_set(trackpad, enhanced_trackpad_trigger_handler);
    if (err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return -EINVAL;
    }

    return 0;
}

SYS_INIT(enhanced_trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
