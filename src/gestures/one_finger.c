// =============================================================================
// FILE: src/gestures/one_finger.c
// Single finger gesture processing with performance optimizations
// =============================================================================
#include "../iqs5xx_enhanced.h"
#include "gesture_common.h"

LOG_MODULE_DECLARE(iqs5xx_driver);

// Static variables for gesture state tracking (performance optimization)
static struct gesture_event current_gesture = {0};
static uint32_t gesture_start_timestamp = 0;

/**
 * Process single finger gestures with enhanced detection
 * Optimized for minimal processing overhead
 */

void process_single_finger_gestures(struct azoteq_iqs5xx_data *data) {
    const struct iqs5xx_rawdata *current = &data->current_data;
    const struct iqs5xx_rawdata *previous = &data->previous_data;

    // Quick exit if no gesture bits are set (performance optimization)
    if (current->gestures0 == 0 && previous->gestures0 == 0) {
        return;
    }

    // Detect new gesture events (edge detection)
    uint8_t new_gestures = current->gestures0 & ~previous->gestures0;

    if (new_gestures & GESTURE_SINGLE_TAP) {
        handle_single_tap(data);
    }

    if (new_gestures & GESTURE_PRESS_HOLD) {
        handle_press_and_hold(data);
    }

    // Handle directional swipes
    if (new_gestures & (GESTURE_SWIPE_NEG_X | GESTURE_SWIPE_POS_X |
                       GESTURE_SWIPE_NEG_Y | GESTURE_SWIPE_POS_Y)) {
        handle_swipe_gesture(data, new_gestures);
    }
}

/**
 * Handle single tap gesture with debouncing
 */
static void handle_single_tap(struct azoteq_iqs5xx_data *data) {
    const struct iqs5xx_rawdata *current = &data->current_data;

    // Validate tap conditions (prevent false positives)
    if (current->finger_count != 1) {
        LOG_DBG("Invalid finger count for single tap: %d", current->finger_count);
        return;
    }

    const struct iqs5xx_finger *finger = &current->fingers[0];

    // Check if finger position is stable (not moving during tap)
    if (!validate_tap_stability(data)) {
        LOG_DBG("Tap rejected due to movement");
        return;
    }

    LOG_INF("Single tap detected at (%d, %d)", finger->abs_x, finger->abs_y);

    // Generate left mouse button click
    input_report_key(data->dev, INPUT_BTN_LEFT, 1, false, K_NO_WAIT);
    input_report_key(data->dev, INPUT_BTN_LEFT, 0, true, K_NO_WAIT);

    // Update performance statistics
    increment_gesture_counter(data, GESTURE_SINGLE_TAP);
}

/**
 * Handle press and hold gesture with timing validation
 */
static void handle_press_and_hold(struct azoteq_iqs5xx_data *data) {
    const struct iqs5xx_rawdata *current = &data->current_data;

    if (current->finger_count != 1) {
        return;
    }

    const struct iqs5xx_finger *finger = &current->fingers[0];

    // Start hold timer if not already started
    if (gesture_start_timestamp == 0) {
        gesture_start_timestamp = k_uptime_get_32();
        LOG_DBG("Hold gesture started");
        return;
    }

    uint32_t hold_duration = k_uptime_get_32() - gesture_start_timestamp;

    // Check if hold duration meets minimum requirement
    if (hold_duration >= HOLD_MIN_TIME_MS) {
        LOG_INF("Press and hold confirmed at (%d, %d), duration: %d ms",
                finger->abs_x, finger->abs_y, hold_duration);

        // Generate right mouse button click (context menu)
        input_report_key(data->dev, INPUT_BTN_RIGHT, 1, false, K_NO_WAIT);
        input_report_key(data->dev, INPUT_BTN_RIGHT, 0, true, K_NO_WAIT);

        // Reset gesture state
        gesture_start_timestamp = 0;
        increment_gesture_counter(data, GESTURE_PRESS_HOLD);
    }
}

/**
 * Handle swipe gestures with direction detection
 */
static void handle_swipe_gesture(struct azoteq_iqs5xx_data *data, uint8_t swipe_flags) {
    const struct iqs5xx_rawdata *current = &data->current_data;

    // Determine swipe direction and magnitude
    int16_t swipe_x = 0, swipe_y = 0;
    const char *direction = "unknown";

    if (swipe_flags & GESTURE_SWIPE_POS_X) {
        swipe_x = abs(current->rel_x);
        direction = "right";
    } else if (swipe_flags & GESTURE_SWIPE_NEG_X) {
        swipe_x = -abs(current->rel_x);
        direction = "left";
    }

    if (swipe_flags & GESTURE_SWIPE_POS_Y) {
        swipe_y = abs(current->rel_y);
        direction = "down";
    } else if (swipe_flags & GESTURE_SWIPE_NEG_Y) {
        swipe_y = -abs(current->rel_y);
        direction = "up";
    }

    // Validate swipe magnitude (prevent noise)
    uint32_t swipe_magnitude = swipe_x * swipe_x + swipe_y * swipe_y;
    if (swipe_magnitude < (SWIPE_MIN_DISTANCE * SWIPE_MIN_DISTANCE)) {
        LOG_DBG("Swipe magnitude too small: %d", swipe_magnitude);
        return;
    }

    LOG_INF("Swipe %s detected: dx=%d, dy=%d", direction, swipe_x, swipe_y);

    // Convert swipe to scroll events for better UX
    if (abs(swipe_y) > abs(swipe_x)) {
        // Vertical swipe -> vertical scroll
        int16_t scroll_delta = (swipe_y > 0) ? -3 : 3; // Invert for natural scrolling
        input_report_rel(data->dev, INPUT_REL_WHEEL, scroll_delta, true, K_NO_WAIT);
    } else {
        // Horizontal swipe -> horizontal scroll
        int16_t scroll_delta = (swipe_x > 0) ? 3 : -3;
        input_report_rel(data->dev, INPUT_REL_HWHEEL, scroll_delta, true, K_NO_WAIT);
    }

    increment_gesture_counter(data, swipe_flags);
}

/**
 * Validate tap stability by checking movement during tap
 */
static bool validate_tap_stability(struct azoteq_iqs5xx_data *data) {
    const struct iqs5xx_rawdata *current = &data->current_data;

    // Check relative movement (should be minimal for a tap)
    uint32_t movement = current->rel_x * current->rel_x + current->rel_y * current->rel_y;

    return movement <= (TAP_MAX_DISTANCE * TAP_MAX_DISTANCE);
}

/**
 * Update gesture performance counters
 */
static void increment_gesture_counter(struct azoteq_iqs5xx_data *data, uint8_t gesture_type) {
    // This could be expanded to track individual gesture statistics
    // For now, just log the gesture for debugging
    LOG_DBG("Gesture 0x%02x processed", gesture_type);
}
