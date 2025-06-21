#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Gesture type enumeration
typedef enum {
    TWO_FINGER_NONE = 0,
    TWO_FINGER_ZOOM,
    TWO_FINGER_VERTICAL_SCROLL,
    TWO_FINGER_HORIZONTAL_SCROLL
} two_finger_gesture_type_t;

// Enhanced two-finger state
struct enhanced_two_finger_state {
    // Basic tracking
    bool active;
    int64_t start_time;
    two_finger_gesture_type_t gesture_type;
    bool gesture_locked;

    // Position tracking
    struct {
        uint16_t x, y;
    } start_pos[2];
    struct {
        uint16_t x, y;
    } last_pos[2];

    // Zoom state
    float initial_distance;
    float last_distance;
    bool zoom_command_sent;
    int stable_readings;

    // Scroll state
    float scroll_accumulator_x;
    float scroll_accumulator_y;
    int64_t last_scroll_time;

    // Movement tracking for gesture detection
    float total_movement_x[2];  // Total X movement for each finger
    float total_movement_y[2];  // Total Y movement for each finger
} static two_finger_state = {0};

// Configuration constants
#define GESTURE_DETECTION_TIME_MS    100    // Reduced! Time to wait before deciding gesture type
#define ZOOM_THRESHOLD_PX           100     // Distance change needed for zoom
#define SCROLL_THRESHOLD_PX         25      // Reduced! Movement needed to start scrolling
#define SCROLL_SENSITIVITY          3.0f    // Scroll speed multiplier
#define ZOOM_STABILITY_THRESHOLD    15.0f   // Distance change considered stable
#define MIN_FINGER_STRENGTH         1000    // Minimum strength for valid gesture
#define TAP_MAX_TIME_MS             200     // Reduced! Maximum time for a tap

// Calculate distance between two points
static float calculate_distance(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    return sqrtf(dx * dx + dy * dy);
}

// Calculate average position of two fingers
static void calculate_center_point(const struct iqs5xx_rawdata *data, float *center_x, float *center_y) {
    *center_x = (float)(data->fingers[0].ax + data->fingers[1].ax) / 2.0f;
    *center_y = (float)(data->fingers[0].ay + data->fingers[1].ay) / 2.0f;
}

// IMMEDIATE two-finger tap detection from hardware gesture
static void handle_hardware_two_finger_tap(void) {
    LOG_INF("*** TWO FINGER TAP -> RIGHT CLICK ***");
    send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, true);
    send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 0, true);
}

// Detect gesture type based on finger movement patterns
static two_finger_gesture_type_t detect_gesture_type(const struct iqs5xx_rawdata *data) {
    // Calculate current positions and movements
    float dx0 = (float)(data->fingers[0].ax - two_finger_state.start_pos[0].x);
    float dy0 = (float)(data->fingers[0].ay - two_finger_state.start_pos[0].y);
    float dx1 = (float)(data->fingers[1].ax - two_finger_state.start_pos[1].x);
    float dy1 = (float)(data->fingers[1].ay - two_finger_state.start_pos[1].y);

    // Update total movement tracking
    two_finger_state.total_movement_x[0] = dx0;
    two_finger_state.total_movement_y[0] = dy0;
    two_finger_state.total_movement_x[1] = dx1;
    two_finger_state.total_movement_y[1] = dy1;

    // Calculate movement magnitudes
    float movement0 = sqrtf(dx0*dx0 + dy0*dy0);
    float movement1 = sqrtf(dx1*dx1 + dy1*dy1);

    // Check if both fingers moved enough
    if (movement0 < SCROLL_THRESHOLD_PX && movement1 < SCROLL_THRESHOLD_PX) {
        return TWO_FINGER_NONE;
    }

    // Calculate distance change for zoom detection
    float current_distance = calculate_distance(
        data->fingers[0].ax, data->fingers[0].ay,
        data->fingers[1].ax, data->fingers[1].ay
    );
    float distance_change = fabsf(current_distance - two_finger_state.initial_distance);

    // Check for zoom gesture (fingers moving apart/together)
    if (distance_change > ZOOM_THRESHOLD_PX) {
        // Additional check: fingers should move in opposite directions for zoom
        float dot_product = dx0*dx1 + dy0*dy1;
        if (dot_product < 0) {  // Opposite directions
            LOG_INF("Detected ZOOM gesture: distance_change=%.1f, dot_product=%.1f",
                    (double)distance_change, (double)dot_product);
            return TWO_FINGER_ZOOM;
        }
    }

    // Check for scroll gestures (fingers moving in same direction)
    float dot_product = dx0*dx1 + dy0*dy1;
    if (dot_product > 0) {  // Same direction
        // Calculate average movement
        float avg_dx = (dx0 + dx1) / 2.0f;
        float avg_dy = (dy0 + dy1) / 2.0f;

        // Determine if horizontal or vertical scroll
        if (fabsf(avg_dy) > fabsf(avg_dx) * 1.5f) {
            LOG_INF("Detected VERTICAL SCROLL: avg_dy=%.1f, avg_dx=%.1f",
                    (double)avg_dy, (double)avg_dx);
            return TWO_FINGER_VERTICAL_SCROLL;
        } else if (fabsf(avg_dx) > fabsf(avg_dy) * 1.5f) {
            LOG_INF("Detected HORIZONTAL SCROLL: avg_dx=%.1f, avg_dy=%.1f",
                    (double)avg_dx, (double)avg_dy);
            return TWO_FINGER_HORIZONTAL_SCROLL;
        }
    }

    LOG_DBG("No clear gesture detected: dot_product=%.1f, distance_change=%.1f",
            (double)dot_product, (double)distance_change);
    return TWO_FINGER_NONE;
}

// Handle zoom gesture
static void handle_zoom_gesture(const struct iqs5xx_rawdata *data) {
    if (two_finger_state.zoom_command_sent) {
        return;  // Already sent zoom command this session
    }

    float current_distance = calculate_distance(
        data->fingers[0].ax, data->fingers[0].ay,
        data->fingers[1].ax, data->fingers[1].ay
    );

    float distance_change = current_distance - two_finger_state.initial_distance;
    float distance_delta = current_distance - two_finger_state.last_distance;
    two_finger_state.last_distance = current_distance;

    // Check for stability
    if (fabsf(distance_delta) < ZOOM_STABILITY_THRESHOLD) {
        two_finger_state.stable_readings++;
    } else {
        two_finger_state.stable_readings = 0;
    }

    // Send zoom command if stable enough
    if (two_finger_state.stable_readings >= 1 || fabsf(distance_change) > ZOOM_THRESHOLD_PX * 2) {
        if (distance_change > 0) {
            LOG_INF("*** ZOOM IN: %d px ***", (int)distance_change);
            send_trackpad_zoom_in();
        } else {
            LOG_INF("*** ZOOM OUT: %d px ***", (int)distance_change);
            send_trackpad_zoom_out();
        }
        two_finger_state.zoom_command_sent = true;
    }
}

// Handle scroll gesture
static void handle_scroll_gesture(const struct iqs5xx_rawdata *data) {
    int64_t current_time = k_uptime_get();

    // Rate limit scrolling to prevent too many events
    if (current_time - two_finger_state.last_scroll_time < 50) {
        return;
    }

    // Calculate average movement since last position
    float dx = ((float)(data->fingers[0].ax - two_finger_state.last_pos[0].x) +
                (float)(data->fingers[1].ax - two_finger_state.last_pos[1].x)) / 2.0f;
    float dy = ((float)(data->fingers[0].ay - two_finger_state.last_pos[0].y) +
                (float)(data->fingers[1].ay - two_finger_state.last_pos[1].y)) / 2.0f;

    // Accumulate scroll movement
    two_finger_state.scroll_accumulator_x += dx * SCROLL_SENSITIVITY;
    two_finger_state.scroll_accumulator_y += dy * SCROLL_SENSITIVITY;

    // Send scroll events when accumulator exceeds threshold
    int scroll_x = 0, scroll_y = 0;

    if (two_finger_state.gesture_type == TWO_FINGER_HORIZONTAL_SCROLL) {
        if (fabsf(two_finger_state.scroll_accumulator_x) >= SCROLL_REPORT_DISTANCE) {
            scroll_x = (int)(two_finger_state.scroll_accumulator_x / SCROLL_REPORT_DISTANCE);
            two_finger_state.scroll_accumulator_x -= scroll_x * SCROLL_REPORT_DISTANCE;

            LOG_INF("Horizontal scroll: %d units", scroll_x);
            send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, -scroll_x, true);
            two_finger_state.last_scroll_time = current_time;
        }
    } else if (two_finger_state.gesture_type == TWO_FINGER_VERTICAL_SCROLL) {
        if (fabsf(two_finger_state.scroll_accumulator_y) >= SCROLL_REPORT_DISTANCE) {
            scroll_y = (int)(two_finger_state.scroll_accumulator_y / SCROLL_REPORT_DISTANCE);
            two_finger_state.scroll_accumulator_y -= scroll_y * SCROLL_REPORT_DISTANCE;

            LOG_INF("Vertical scroll: %d units", scroll_y);
            send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, -scroll_y, true);
            two_finger_state.last_scroll_time = current_time;
        }
    }

    // Update last positions
    two_finger_state.last_pos[0].x = data->fingers[0].ax;
    two_finger_state.last_pos[0].y = data->fingers[0].ay;
    two_finger_state.last_pos[1].x = data->fingers[1].ax;
    two_finger_state.last_pos[1].y = data->fingers[1].ay;
}

void handle_two_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    if (data->finger_count != 2) {
        return;
    }

    // IMMEDIATE HARDWARE GESTURE HANDLING - Check first for instant response
    if (data->gestures1 & GESTURE_TWO_FINGER_TAP) {
        handle_hardware_two_finger_tap();
        return; // Handle tap immediately, don't process other gestures
    }

    // ALSO check for immediate tap if fingers are very stable
    if (!two_finger_state.active) {
        // If this is the very first reading and fingers are close together,
        // it's likely a tap intention - be more aggressive
        float initial_distance = calculate_distance(
            data->fingers[0].ax, data->fingers[0].ay,
            data->fingers[1].ax, data->fingers[1].ay
        );

        // If fingers are very close (likely a tap), reduce detection time
        if (initial_distance < 150.0f) {
            LOG_DBG("Fingers close together (%.1f px) - likely tap intention", (double)initial_distance);
        }
    }

    // Ensure we have two valid finger readings
    if (data->fingers[0].strength == 0 || data->fingers[1].strength == 0) {
        LOG_DBG("Skipping: invalid finger data (strength: %d, %d)",
                data->fingers[0].strength, data->fingers[1].strength);
        return;
    }

    // Check minimum strength requirement
    if (data->fingers[0].strength < MIN_FINGER_STRENGTH ||
        data->fingers[1].strength < MIN_FINGER_STRENGTH) {
        LOG_DBG("Insufficient strength: F0=%d, F1=%d (need >%d)",
                data->fingers[0].strength, data->fingers[1].strength, MIN_FINGER_STRENGTH);
        return;
    }

    int64_t current_time = k_uptime_get();

    // Initialize two-finger session if just started
    if (!two_finger_state.active) {
        two_finger_state.active = true;
        two_finger_state.start_time = current_time;
        two_finger_state.gesture_type = TWO_FINGER_NONE;
        two_finger_state.gesture_locked = false;
        two_finger_state.zoom_command_sent = false;
        two_finger_state.stable_readings = 0;
        two_finger_state.last_scroll_time = current_time;

        // Store initial positions
        two_finger_state.start_pos[0].x = two_finger_state.last_pos[0].x = data->fingers[0].ax;
        two_finger_state.start_pos[0].y = two_finger_state.last_pos[0].y = data->fingers[0].ay;
        two_finger_state.start_pos[1].x = two_finger_state.last_pos[1].x = data->fingers[1].ax;
        two_finger_state.start_pos[1].y = two_finger_state.last_pos[1].y = data->fingers[1].ay;

        // Calculate initial distance for zoom detection
        two_finger_state.initial_distance = calculate_distance(
            data->fingers[0].ax, data->fingers[0].ay,
            data->fingers[1].ax, data->fingers[1].ay
        );
        two_finger_state.last_distance = two_finger_state.initial_distance;

        // Reset scroll accumulators
        two_finger_state.scroll_accumulator_x = 0;
        two_finger_state.scroll_accumulator_y = 0;

        LOG_INF("=== TWO FINGER SESSION START ===");
        LOG_INF("Initial: F0(%d,%d) F1(%d,%d), distance=%.1f",
                data->fingers[0].ax, data->fingers[0].ay,
                data->fingers[1].ax, data->fingers[1].ay,
                (double)two_finger_state.initial_distance);

        // Update legacy state for compatibility
        state->twoFingerActive = true;
        state->twoFingerStartTime = current_time;
        state->twoFingerStartPos[0].x = data->fingers[0].ax;
        state->twoFingerStartPos[0].y = data->fingers[0].ay;
        state->twoFingerStartPos[1].x = data->fingers[1].ax;
        state->twoFingerStartPos[1].y = data->fingers[1].ay;

        return;
    }

    int64_t time_since_start = current_time - two_finger_state.start_time;

    // Wait for stabilization before detecting gesture type
    if (time_since_start < GESTURE_DETECTION_TIME_MS) {
        LOG_DBG("Waiting for stabilization (%lld/%d ms)", time_since_start, GESTURE_DETECTION_TIME_MS);
        return;
    }

    // Detect gesture type if not already locked
    if (!two_finger_state.gesture_locked && two_finger_state.gesture_type == TWO_FINGER_NONE) {
        two_finger_state.gesture_type = detect_gesture_type(data);
        if (two_finger_state.gesture_type != TWO_FINGER_NONE) {
            two_finger_state.gesture_locked = true;
            const char* gesture_names[] = {"NONE", "ZOOM", "VERTICAL_SCROLL", "HORIZONTAL_SCROLL"};
            LOG_INF("*** GESTURE LOCKED: %s ***", gesture_names[two_finger_state.gesture_type]);
        }
    }

    // Handle the specific gesture
    switch (two_finger_state.gesture_type) {
        case TWO_FINGER_ZOOM:
            handle_zoom_gesture(data);
            break;

        case TWO_FINGER_VERTICAL_SCROLL:
        case TWO_FINGER_HORIZONTAL_SCROLL:
            handle_scroll_gesture(data);
            break;

        default:
            // No gesture detected yet, continue waiting
            break;
    }
}

void reset_two_finger_state(struct gesture_state *state) {
    if (two_finger_state.active) {
        LOG_INF("=== TWO FINGER SESSION END ===");

        // SIMPLIFIED: Only handle fallback tap if no other gesture was performed AND it was quick
        if (!two_finger_state.gesture_locked &&
            k_uptime_get() - two_finger_state.start_time < TAP_MAX_TIME_MS) {
            LOG_INF("*** FALLBACK TWO FINGER TAP -> RIGHT CLICK ***");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 0, true);
        }

        const char* gesture_names[] = {"NONE", "ZOOM", "VERTICAL_SCROLL", "HORIZONTAL_SCROLL"};
        LOG_INF("Session completed: gesture=%s, zoom_sent=%d",
                gesture_names[two_finger_state.gesture_type],
                two_finger_state.zoom_command_sent);

        // Clear enhanced state
        memset(&two_finger_state, 0, sizeof(two_finger_state));

        // Clear legacy state
        state->twoFingerActive = false;
        state->lastXScrollReport = 0;
    }
}
