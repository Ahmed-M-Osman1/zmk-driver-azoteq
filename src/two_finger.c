// Fixed two_finger.c - Simpler tap detection
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Simplified two-finger state
struct simple_two_finger_state {
    bool active;
    int64_t start_time;
    struct {
        uint16_t x, y;
    } start_pos[2];

    // Simplified movement tracking
    float total_movement[2];  // Total movement for each finger
} static two_finger_state = {0};

// More lenient thresholds
#define TAP_MOVEMENT_THRESHOLD      50.0f   // Increased from 35px
#define GESTURE_DETECTION_TIME_MS   200     // Longer time window

// Calculate movement since start
static float calculate_total_movement(const struct iqs5xx_rawdata *data, int finger_idx) {
    float dx = (float)(data->fingers[finger_idx].ax - two_finger_state.start_pos[finger_idx].x);
    float dy = (float)(data->fingers[finger_idx].ay - two_finger_state.start_pos[finger_idx].y);
    return sqrtf(dx*dx + dy*dy);
}

void handle_two_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    if (data->finger_count != 2) {
        return;
    }

    // PRIORITY 1: Handle hardware gestures IMMEDIATELY (like the simple version)
    if (data->gestures1 == GESTURE_TWO_FINGER_TAP) {
        LOG_INF("*** HARDWARE TWO FINGER TAP -> RIGHT CLICK ***");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_RIGHT, 1, false);
        send_input_event(INPUT_EV_KEY, INPUT_BTN_RIGHT, 0, true);
        return; // DONE - don't process further
    }

    if (data->gestures1 == GESTURE_SCROLLG) {
        LOG_INF("*** HARDWARE SCROLL: rx=%d, ry=%d ***", data->rx, data->ry);

        // IMPROVED: Better scroll sensitivity (like the simple version)
        static int16_t scroll_accumulator = 0;

        scroll_accumulator += data->rx;
        int8_t vertical_scroll = -data->ry / 10;  // Faster vertical scrolling
        int8_t horizontal_scroll = 0;

        // Horizontal scroll every 15 pixels
        if (abs(scroll_accumulator) >= 15) {
            horizontal_scroll = scroll_accumulator >= 0 ? 1 : -1;
            scroll_accumulator = 0;
        }

        if (vertical_scroll != 0) {
            send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, vertical_scroll, false);
        }
        if (horizontal_scroll != 0) {
            send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, horizontal_scroll, true);
        }
        return; // DONE - don't process further
    }

    // PRIORITY 2: Initialize tracking if needed
    if (!two_finger_state.active) {
        two_finger_state.active = true;
        two_finger_state.start_time = k_uptime_get();

        // Store initial positions
        two_finger_state.start_pos[0].x = data->fingers[0].ax;
        two_finger_state.start_pos[0].y = data->fingers[0].ay;
        two_finger_state.start_pos[1].x = data->fingers[1].ax;
        two_finger_state.start_pos[1].y = data->fingers[1].ay;

        LOG_DBG("Two finger session start");

        // Update legacy state for compatibility
        state->twoFingerActive = true;
        state->twoFingerStartTime = k_uptime_get();
        state->twoFingerStartPos[0].x = data->fingers[0].ax;
        state->twoFingerStartPos[0].y = data->fingers[0].ay;
        state->twoFingerStartPos[1].x = data->fingers[1].ax;
        state->twoFingerStartPos[1].y = data->fingers[1].ay;

        return;
    }

    // PRIORITY 3: Update movement tracking for potential tap detection
    two_finger_state.total_movement[0] = calculate_total_movement(data, 0);
    two_finger_state.total_movement[1] = calculate_total_movement(data, 1);
}
}

void reset_two_finger_state(struct gesture_state *state) {
    if (two_finger_state.active) {
        LOG_INF("=== TWO FINGER SESSION END ===");

        int64_t session_time = k_uptime_get() - two_finger_state.start_time;
        bool quick_release = (session_time < 300);  // Released within 300ms
        bool small_movement = (two_finger_state.total_movement[0] < TAP_MOVEMENT_THRESHOLD &&
                              two_finger_state.total_movement[1] < TAP_MOVEMENT_THRESHOLD);

void reset_two_finger_state(struct gesture_state *state) {
    if (two_finger_state.active) {
        LOG_DBG("Two finger session end");

        // SIMPLIFIED: Check for tap only if no significant movement AND quick release
        int64_t session_time = k_uptime_get() - two_finger_state.start_time;
        bool quick_release = (session_time < 300);  // Released within 300ms
        bool small_movement = (two_finger_state.total_movement[0] < TAP_MOVEMENT_THRESHOLD &&
                              two_finger_state.total_movement[1] < TAP_MOVEMENT_THRESHOLD);

        if (quick_release && small_movement) {
            LOG_INF("*** TWO FINGER TAP -> RIGHT CLICK ***");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_RIGHT, 1, false);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_RIGHT, 0, true);
        } else {
            LOG_DBG("No tap: time=%lldms, movement=%.1f/%.1f px",
                    session_time,
                    (double)two_finger_state.total_movement[0],
                    (double)two_finger_state.total_movement[1]);
        }

        // Clear state
        memset(&two_finger_state, 0, sizeof(two_finger_state));

        // Clear legacy state
        state->twoFingerActive = false;
        state->lastXScrollReport = 0;
    }
}
