#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Gesture type tracking
typedef enum {
    TWO_FINGER_NONE = 0,
    TWO_FINGER_TAP,
    TWO_FINGER_SCROLL,
    TWO_FINGER_ZOOM
} two_finger_gesture_type_t;

// Global state for zoom
static bool zoom_already_triggered = false;
static float initial_gesture_distance = 0.0f;
static two_finger_gesture_type_t current_gesture_type = TWO_FINGER_NONE;

// Calculate distance between two points
static float calculate_distance(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    return sqrtf(dx * dx + dy * dy);
}

void handle_two_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    if (data->finger_count != 2) {
        return;
    }

    // Initialize two finger tracking if just started
    if (!state->twoFingerActive) {
        state->twoFingerActive = true;
        state->twoFingerStartTime = k_uptime_get();

        // Store initial positions
        state->twoFingerStartPos[0].x = data->fingers[0].ax;
        state->twoFingerStartPos[0].y = data->fingers[0].ay;
        state->twoFingerStartPos[1].x = data->fingers[1].ax;
        state->twoFingerStartPos[1].y = data->fingers[1].ay;

        // Reset all state
        zoom_already_triggered = false;
        current_gesture_type = TWO_FINGER_NONE;
        initial_gesture_distance = calculate_distance(
            data->fingers[0].ax, data->fingers[0].ay,
            data->fingers[1].ax, data->fingers[1].ay
        );

        LOG_INF("Two finger gesture started at positions (%d,%d) and (%d,%d)",
                state->twoFingerStartPos[0].x, state->twoFingerStartPos[0].y,
                state->twoFingerStartPos[1].x, state->twoFingerStartPos[1].y);
        LOG_DBG("Initial distance: %.1f, all gestures reset", (double)initial_gesture_distance);
        return;
    }

    int64_t time_since_start = k_uptime_get() - state->twoFingerStartTime;

    // STEP 1: Check for immediate tap gesture (highest priority)
    if (data->gestures1 & GESTURE_TWO_FINGER_TAP && current_gesture_type == TWO_FINGER_NONE) {
        LOG_INF("*** TWO FINGER TAP -> RIGHT CLICK ***");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, true);
        send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 0, true);
        current_gesture_type = TWO_FINGER_TAP;
        return;
    }

    // STEP 2: Check for scroll gesture
    if (data->gestures1 & GESTURE_SCROLLG) {
        // Only switch to scroll if we haven't committed to zoom yet
        if (current_gesture_type == TWO_FINGER_NONE || current_gesture_type == TWO_FINGER_SCROLL) {
            current_gesture_type = TWO_FINGER_SCROLL;

            state->lastXScrollReport += data->rx;
            int8_t pan = -data->ry;
            int8_t scroll = 0;

            if (abs(state->lastXScrollReport) >= SCROLL_REPORT_DISTANCE) {
                scroll = state->lastXScrollReport >= 0 ? 1 : -1;
                state->lastXScrollReport = 0;
            }

            LOG_DBG("Scroll: pan=%d, scroll=%d (rx=%d, ry=%d)",
                    pan, scroll, data->rx, data->ry);

            if (pan != 0) {
                send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, pan, false);
            }
            if (scroll != 0) {
                send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, scroll, true);
            }
            return;
        } else {
            LOG_DBG("Ignoring scroll - already committed to %s",
                    current_gesture_type == TWO_FINGER_ZOOM ? "zoom" : "other");
        }
    }

    // STEP 3: Check for zoom gesture (only if no other gesture active)
    if (current_gesture_type == TWO_FINGER_NONE || current_gesture_type == TWO_FINGER_ZOOM) {

        // Wait for deliberate movement and strong contact
        if (time_since_start > 300 && // Wait longer to distinguish from tap/scroll
            !zoom_already_triggered &&
            data->fingers[0].strength > 2500 && // Higher thresholds
            data->fingers[1].strength > 2500) {

            float current_distance = calculate_distance(
                data->fingers[0].ax, data->fingers[0].ay,
                data->fingers[1].ax, data->fingers[1].ay
            );

            float distance_change = current_distance - initial_gesture_distance;

            LOG_DBG("Zoom check: time=%lld, type=%d, initial=%.1f, current=%.1f, change=%.1f",
                    time_since_start, current_gesture_type, (double)initial_gesture_distance,
                    (double)current_distance, (double)distance_change);

            // Very high threshold for deliberate zoom
            if (fabsf(distance_change) > 150.0f) {
                current_gesture_type = TWO_FINGER_ZOOM;

                if (distance_change > 0) {
                    LOG_INF("*** ZOOM IN: Pinch apart (%.1f px) after %lld ms ***",
                            (double)distance_change, time_since_start);
                    send_trackpad_zoom_in();
                } else {
                    LOG_INF("*** ZOOM OUT: Pinch together (%.1f px) after %lld ms ***",
                            (double)distance_change, time_since_start);
                    send_trackpad_zoom_out();
                }

                zoom_already_triggered = true;
                LOG_INF("Zoom gesture committed - no more gestures until fingers lift");
            } else {
                LOG_DBG("Zoom pending: change=%.1f/150px threshold", (double)distance_change);
            }
        } else {
            LOG_DBG("Zoom waiting: time=%lld/300ms, strength=%d/%d, triggered=%d",
                    time_since_start, data->fingers[0].strength, data->fingers[1].strength, zoom_already_triggered);
        }
    }

    // Log hardware zoom detection but don't act on it
    if (data->gestures1 & GESTURE_ZOOM) {
        LOG_DBG("Hardware zoom detected but ignored (gesture_type=%d)", current_gesture_type);
    }
}

void reset_two_finger_state(struct gesture_state *state) {
    if (state->twoFingerActive) {
        LOG_DBG("Resetting two finger state (was type %d)", current_gesture_type);
        state->twoFingerActive = false;

        // Reset all gesture state
        zoom_already_triggered = false;
        initial_gesture_distance = 0.0f;
        current_gesture_type = TWO_FINGER_NONE;
        LOG_DBG("All two finger gesture state reset - ready for next gesture");
    }

    if (state->lastXScrollReport != 0) {
        LOG_DBG("Resetting scroll accumulator: was %d", state->lastXScrollReport);
        state->lastXScrollReport = 0;
    }
}
