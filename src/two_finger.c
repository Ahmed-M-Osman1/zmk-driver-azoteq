#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

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

        LOG_INF("Two finger gesture started at positions (%d,%d) and (%d,%d)",
                state->twoFingerStartPos[0].x, state->twoFingerStartPos[0].y,
                state->twoFingerStartPos[1].x, state->twoFingerStartPos[1].y);
        return;
    }

    // Handle hardware gestures first
    if (data->gestures1) {
        switch(data->gestures1) {
            case GESTURE_TWO_FINGER_TAP:
                LOG_INF("*** TWO FINGER TAP -> RIGHT CLICK ***");
                send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, true);
                send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 0, true);
                return; // Don't check zoom after tap

            case GESTURE_SCROLLG:
                // Handle scrolling - this takes priority over zoom
                state->lastXScrollReport += data->rx;
                int8_t pan = -data->ry;
                int8_t scroll = 0;

                if (abs(state->lastXScrollReport) - (int16_t)SCROLL_REPORT_DISTANCE > 0) {
                    scroll = state->lastXScrollReport >= 0 ? 1 : -1;
                    state->lastXScrollReport = 0;
                }

                LOG_INF("*** SCROLL: pan=%d, scroll=%d (rx=%d, ry=%d) ***",
                        pan, scroll, data->rx, data->ry);

                if (pan != 0) {
                    send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, pan, false);
                }
                if (scroll != 0) {
                    send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, scroll, true);
                }
                return; // Don't check zoom during scroll

            case GESTURE_ZOOM:
                LOG_INF("*** HARDWARE ZOOM DETECTED - IGNORING (using manual detection) ***");
                // Ignore hardware zoom, use manual detection for better control
                break;

            default:
                if (data->gestures1 != 0) {
                    LOG_WRN("Unknown two finger gesture1: 0x%02x", data->gestures1);
                }
                break;
        }
    }

    // Manual pinch-to-zoom with distance-based steps (FIXED - No blocking delays)
    if (data->fingers[0].strength > 0 && data->fingers[1].strength > 0) {

        // Only process zoom when fingers are relatively stable
        if (abs(data->rx) > 3 || abs(data->ry) > 3) {
            LOG_DBG("Zoom blocked: fingers moving too fast (rx=%d, ry=%d)", data->rx, data->ry);
            return;
        }

        // Calculate current distance between fingers
        float currentDistance = calculate_distance(
            data->fingers[0].ax, data->fingers[0].ay,
            data->fingers[1].ax, data->fingers[1].ay
        );

        // Calculate initial distance
        float initialDistance = calculate_distance(
            state->twoFingerStartPos[0].x, state->twoFingerStartPos[0].y,
            state->twoFingerStartPos[1].x, state->twoFingerStartPos[1].y
        );

        float totalDistanceChange = currentDistance - initialDistance;

        // Track zoom steps already sent (static to persist between calls)
        static int zoom_steps_sent = 0;
        static float last_tracked_distance = 0;
        static int64_t last_zoom_time = 0;

        // Reset tracking when starting new two-finger gesture
        if (!state->twoFingerActive || fabsf(last_tracked_distance - initialDistance) > 50) {
            zoom_steps_sent = 0;
            last_tracked_distance = initialDistance;
            last_zoom_time = 0;
            LOG_DBG("Reset zoom tracking: initial_distance=%.1f", (double)initialDistance);
        }

        // Calculate how many zoom steps we should have sent based on total movement
        int target_zoom_steps = (int)(totalDistanceChange / ZOOM_SENSITIVITY);
        int steps_to_send = target_zoom_steps - zoom_steps_sent;

        LOG_DBG("Zoom calc: total_change=%.1f, target_steps=%d, sent=%d, to_send=%d",
                (double)totalDistanceChange, target_zoom_steps, zoom_steps_sent, steps_to_send);

        // Rate limit: Only send 1 zoom step per call, and only every 100ms
        int64_t current_time = k_uptime_get();
        if (steps_to_send != 0 && (current_time - last_zoom_time > 100)) {
            // Send only 1 step per trigger to prevent overwhelming
            int actual_steps = (steps_to_send > 0) ? 1 : -1;

            if (actual_steps > 0) {
                LOG_INF("*** ZOOM STEP %d: PINCH OUT -> ZOOM IN ***", zoom_steps_sent + 1);
                send_trackpad_zoom_in();
                zoom_steps_sent++;
            } else {
                LOG_INF("*** ZOOM STEP %d: PINCH IN -> ZOOM OUT ***", zoom_steps_sent - 1);
                send_trackpad_zoom_out();
                zoom_steps_sent--;
            }

            last_zoom_time = current_time;
            LOG_INF("Zoom step sent: %d (total change: %.1f pixels)",
                    zoom_steps_sent, (double)totalDistanceChange);
        }
    }
}

void reset_two_finger_state(struct gesture_state *state) {
    if (state->twoFingerActive) {
        LOG_DBG("Resetting two finger state");
        state->twoFingerActive = false;

        // Reset zoom step tracking (external variables)
        // This will be reset in the main function when twoFingerActive becomes false
    }

    if (state->lastXScrollReport != 0) {
        LOG_DBG("Resetting scroll accumulator: was %d", state->lastXScrollReport);
        state->lastXScrollReport = 0;
    }
}
