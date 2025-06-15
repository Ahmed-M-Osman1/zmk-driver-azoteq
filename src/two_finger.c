#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Ultra-simple zoom state - GLOBAL to persist across all calls
static bool zoom_already_triggered = false;
static float initial_gesture_distance = 0.0f;

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

        // Store initial positions for zoom detection
        state->twoFingerStartPos[0].x = data->fingers[0].ax;
        state->twoFingerStartPos[0].y = data->fingers[0].ay;
        state->twoFingerStartPos[1].x = data->fingers[1].ax;
        state->twoFingerStartPos[1].y = data->fingers[1].ay;

        // CRITICAL: Reset zoom state for new gesture
        zoom_already_triggered = false;
        initial_gesture_distance = calculate_distance(
            data->fingers[0].ax, data->fingers[0].ay,
            data->fingers[1].ax, data->fingers[1].ay
        );

        LOG_INF("Two finger gesture started at positions (%d,%d) and (%d,%d)",
                state->twoFingerStartPos[0].x, state->twoFingerStartPos[0].y,
                state->twoFingerStartPos[1].x, state->twoFingerStartPos[1].y);
        LOG_DBG("Initial distance: %.1f, zoom reset", (double)initial_gesture_distance);
        return;
    }

    // PRIORITY 1: Handle hardware tap gesture (right click) - IMMEDIATE response
    if (data->gestures1 & GESTURE_TWO_FINGER_TAP) {
        LOG_INF("*** TWO FINGER TAP -> RIGHT CLICK ***");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, true);
        send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 0, true);

        // Block zoom for this gesture since user just tapped
        zoom_already_triggered = true;
        LOG_DBG("Blocking zoom: two-finger tap detected");
        return;
    }

    // PRIORITY 2: Handle scrolling - takes priority over zoom
    if (data->gestures1 & GESTURE_SCROLLG) {
        state->lastXScrollReport += data->rx;
        int8_t pan = -data->ry;
        int8_t scroll = 0;

        if (abs(state->lastXScrollReport) - (int16_t)SCROLL_REPORT_DISTANCE > 0) {
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

        // Block zoom during scroll
        zoom_already_triggered = true;
        LOG_DBG("Blocking zoom: scrolling detected");
        return;
    }

    // PRIORITY 3: Handle hardware zoom (ignore but log)
    if (data->gestures1 & GESTURE_ZOOM) {
        LOG_DBG("Hardware zoom ignored - using manual detection");
        // Don't return here - still allow manual zoom detection
    }

    // PRIORITY 4: Manual zoom detection - only if no other gestures active
    // Wait at least 200ms after gesture start to avoid tap interference
    int64_t time_since_start = k_uptime_get() - state->twoFingerStartTime;
    if (time_since_start > 200 && // Wait longer to avoid tap conflicts
        !zoom_already_triggered &&
        data->fingers[0].strength > 1000 && // Higher threshold for zoom
        data->fingers[1].strength > 1000) {

        // Calculate current distance
        float current_distance = calculate_distance(
            data->fingers[0].ax, data->fingers[0].ay,
            data->fingers[1].ax, data->fingers[1].ay
        );

        float distance_change = current_distance - initial_gesture_distance;

        LOG_DBG("Zoom check: time=%lld, initial=%.1f, current=%.1f, change=%.1f, triggered=%d",
                time_since_start, (double)initial_gesture_distance, (double)current_distance,
                (double)distance_change, zoom_already_triggered);

        // TRIGGER: Significant deliberate movement (120px threshold)
        if (fabsf(distance_change) > 120.0f) {

            if (distance_change > 0) {
                // Fingers moving apart = ZOOM IN
                LOG_INF("*** ZOOM IN: Pinch apart (%.1f px) after %lld ms ***",
                        (double)distance_change, time_since_start);
                send_trackpad_zoom_in();
            } else {
                // Fingers moving together = ZOOM OUT
                LOG_INF("*** ZOOM OUT: Pinch together (%.1f px) after %lld ms ***",
                        (double)distance_change, time_since_start);
                send_trackpad_zoom_out();
            }

            // BLOCK all future zoom for this gesture
            zoom_already_triggered = true;
            LOG_INF("Zoom LOCKED - no more zoom until fingers lift");
        }
    } else if (!zoom_already_triggered) {
        LOG_DBG("Zoom waiting: time=%lld/200ms, strength=%d/%d",
                time_since_start, data->fingers[0].strength, data->fingers[1].strength);
    }
}

void reset_two_finger_state(struct gesture_state *state) {
    if (state->twoFingerActive) {
        LOG_DBG("Resetting two finger state");
        state->twoFingerActive = false;

        // Reset zoom state when fingers lift
        zoom_already_triggered = false;
        initial_gesture_distance = 0.0f;
        LOG_DBG("Zoom state reset - ready for next gesture");
    }

    if (state->lastXScrollReport != 0) {
        LOG_DBG("Resetting scroll accumulator: was %d", state->lastXScrollReport);
        state->lastXScrollReport = 0;
    }
}
