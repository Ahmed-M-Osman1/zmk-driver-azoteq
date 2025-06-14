// =============================================================================
// FILE: src/gestures/two_finger.c
// Multi-finger gesture processing with zoom and scroll detection
// =============================================================================

#include "../iqs5xx_enhanced.h"
#include "gesture_common.h"

LOG_MODULE_DECLARE(iqs5xx_driver);

// Static state for multi-finger gesture tracking
static struct {
    bool zoom_active;
    bool scroll_active;
    uint32_t initial_distance;      // For zoom detection
    uint16_t last_center_x, last_center_y;  // For scroll tracking
    uint32_t gesture_start_time;
} multitouch_state = {0};

/**
 * Process multi-finger gestures with enhanced precision
 */
void process_multi_finger_gestures(struct azoteq_iqs5xx_data *data) {
    const struct iqs5xx_rawdata *current = &data->current_data;
    const struct iqs5xx_rawdata *previous = &data->previous_data;

    // Quick exit for single finger or no fingers
    if (current->finger_count < 2) {
        reset_multitouch_state();
        return;
    }

    // Process two-finger specific gestures
    if (current->finger_count == 2) {
        handle_two_finger_gestures(data);
    }

    // Handle gesture events from device
    uint8_t new_gestures = current->gestures1 & ~previous->gestures1;

    if (new_gestures & GESTURE_TWO_FINGER_TAP) {
        handle_two_finger_tap(data);
    }

    if (new_gestures & GESTURE_SCROLL) {
        handle_gesture_scroll(data);
    }

    if (new_gestures & GESTURE_ZOOM) {
        handle_gesture_zoom(data);
    }
}

/**
 * Handle two-finger gestures (custom detection)
 */
static void handle_two_finger_gestures(struct azoteq_iqs5xx_data *data) {
    const struct iqs5xx_rawdata *current = &data->current_data;
    const struct iqs5xx_finger *finger1 = &current->fingers[0];
    const struct iqs5xx_finger *finger2 = &current->fingers[1];

    // Validate both fingers are active
    if (!is_finger_active(finger1) || !is_finger_active(finger2)) {
        return;
    }

    // Calculate finger distance for zoom detection
    uint32_t current_distance = calculate_distance(finger1->abs_x, finger1->abs_y,
                                                  finger2->abs_x, finger2->abs_y);

    // Calculate center point for scroll detection
    uint16_t center_x = (finger1->abs_x + finger2->abs_x) / 2;
    uint16_t center_y = (finger1->abs_y + finger2->abs_y) / 2;

    // Initialize tracking on first detection
    if (multitouch_state.gesture_start_time == 0) {
        multitouch_state.gesture_start_time = k_uptime_get_32();
        multitouch_state.initial_distance = current_distance;
        multitouch_state.last_center_x = center_x;
        multitouch_state.last_center_y = center_y;
        return;
    }

    // Detect zoom gesture (distance change)
    detect_zoom_gesture(data, current_distance);

    // Detect scroll gesture (center point movement)
    detect_scroll_gesture(data, center_x, center_y);

    // Update tracking state
    multitouch_state.last_center_x = center_x;
    multitouch_state.last_center_y = center_y;
}

/**
 * Detect and handle zoom gestures
 */
static void detect_zoom_gesture(struct azoteq_iqs5xx_data *data, uint32_t current_distance) {
    if (multitouch_state.initial_distance == 0) return;

    // Calculate distance change (use integer math for performance)
    int32_t distance_change = current_distance - multitouch_state.initial_distance;

    // Check if distance change exceeds threshold
    if (abs(distance_change) > (ZOOM_GESTURE_THRESHOLD * ZOOM_GESTURE_THRESHOLD)) {
        bool zooming_in = distance_change > 0;

        LOG_INF("Zoom gesture: %s (distance change: %d)",
                zooming_in ? "in" : "out", distance_change);

        // Generate zoom events (using Ctrl+Scroll wheel simulation)
        input_report_key(data->dev, INPUT_KEY_LEFTCTRL, 1, false, K_NO_WAIT);

        int16_t zoom_delta = zooming_in ? 1 : -1;
        input_report_rel(data->dev, INPUT_REL_WHEEL, zoom_delta, false, K_NO_WAIT);

        input_report_key(data->dev, INPUT_KEY_LEFTCTRL, 0, true, K_NO_WAIT);

        // Update zoom state
        multitouch_state.zoom_active = true;
        multitouch_state.initial_distance = current_distance; // Reset baseline
    }
}

/**
 * Detect and handle scroll gestures
 */
static void detect_scroll_gesture(struct azoteq_iqs5xx_data *data, uint16_t center_x, uint16_t center_y) {
    // Calculate center point movement
    int16_t center_dx = center_x - multitouch_state.last_center_x;
    int16_t center_dy = center_y - multitouch_state.last_center_y;

    // Check if movement exceeds threshold
    uint32_t movement = center_dx * center_dx + center_dy * center_dy;
    if (movement > (SCROLL_GESTURE_THRESHOLD * SCROLL_GESTURE_THRESHOLD)) {

        LOG_INF("Two-finger scroll: dx=%d, dy=%d", center_dx, center_dy);

        // Generate scroll events with sensitivity scaling
        int16_t scroll_x = center_dx / 3; // Scale down for smooth scrolling
        int16_t scroll_y = center_dy / 3;

        if (abs(scroll_y) > abs(scroll_x)) {
            // Vertical scrolling is more common
            input_report_rel(data->dev, INPUT_REL_WHEEL, -scroll_y, true, K_NO_WAIT);
        } else {
            // Horizontal scrolling
            input_report_rel(data->dev, INPUT_REL_HWHEEL, scroll_x, true, K_NO_WAIT);
        }

        multitouch_state.scroll_active = true;
    }
}

/**
 * Handle two-finger tap gesture (right click alternative)
 */
static void handle_two_finger_tap(struct azoteq_iqs5xx_data *data) {
    const struct iqs5xx_rawdata *current = &data->current_data;

    if (current->finger_count != 2) {
        return;
    }

    LOG_INF("Two-finger tap detected");

    // Generate right mouse button click
    input_report_key(data->dev, INPUT_BTN_RIGHT, 1, false, K_NO_WAIT);
    input_report_key(data->dev, INPUT_BTN_RIGHT, 0, true, K_NO_WAIT);
}

/**
 * Handle device-detected scroll gesture
 */
static void handle_gesture_scroll(struct azoteq_iqs5xx_data *data) {
    const struct iqs5xx_rawdata *current = &data->current_data;

    LOG_INF("Device scroll gesture detected: rel_x=%d, rel_y=%d",
            current->rel_x, current->rel_y);

    // Use relative movement for scroll
    if (abs(current->rel_y) > abs(current->rel_x)) {
        input_report_rel(data->dev, INPUT_REL_WHEEL, -current->rel_y / 10, true, K_NO_WAIT);
    } else {
        input_report_rel(data->dev, INPUT_REL_HWHEEL, current->rel_x / 10, true, K_NO_WAIT);
    }
}

/**
 * Handle device-detected zoom gesture
 */
static void handle_gesture_zoom(struct azoteq_iqs5xx_data *data) {
    const struct iqs5xx_rawdata *current = &data->current_data;

    LOG_INF("Device zoom gesture detected");

    // Use relative Y movement to determine zoom direction
    input_report_key(data->dev, INPUT_KEY_LEFTCTRL, 1, false, K_NO_WAIT);

    int16_t zoom_delta = (current->rel_y > 0) ? 1 : -1;
    input_report_rel(data->dev, INPUT_REL_WHEEL, zoom_delta, false, K_NO_WAIT);

    input_report_key(data->dev, INPUT_KEY_LEFTCTRL, 0, true, K_NO_WAIT);
}

/**
 * Reset multi-touch state when gesture ends
 */

static void reset_multitouch_state(void) {
    if (multitouch_state.gesture_start_time != 0) {
        LOG_DBG("Resetting multitouch state");
        memset(&multitouch_state, 0, sizeof(multitouch_state));
    }
}
