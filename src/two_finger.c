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

    // Manual pinch-to-zoom with simple flag-based approach (SIMPLE & RELIABLE)
    if (data->fingers[0].strength > 0 && data->fingers[1].strength > 0) {

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

        // Simple flag-based zoom tracking (static to persist between calls)
        static bool zoom_in_progress = false;
        static float last_zoom_distance = 0;
        static int64_t last_zoom_time = 0;

        // Reset when starting new two-finger gesture
        if (!state->twoFingerActive) {
            zoom_in_progress = false;
            last_zoom_distance = 0;
            last_zoom_time = 0;
            LOG_DBG("Reset zoom flags: new gesture started");
        }

        LOG_DBG("Zoom check: total_change=%.1f, in_progress=%d, last_distance=%.1f",
                (double)totalDistanceChange, zoom_in_progress, (double)last_zoom_distance);

        // Only send zoom if significant movement AND enough time has passed
        int64_t current_time = k_uptime_get();
        float movement_since_last = fabsf(totalDistanceChange - last_zoom_distance);

        if (movement_since_last > ZOOM_SENSITIVITY && // 40 pixels since last zoom
            (current_time - last_zoom_time > 150)) {   // 150ms since last zoom

            if (totalDistanceChange > last_zoom_distance + 10) {
                // Pinch OUT = Zoom IN
                LOG_INF("*** SIMPLE ZOOM: PINCH OUT -> ZOOM IN (change=%.1f) ***",
                        (double)totalDistanceChange);
                send_trackpad_zoom_in();
            } else if (totalDistanceChange < last_zoom_distance - 10) {
                // Pinch IN = Zoom OUT
                LOG_INF("*** SIMPLE ZOOM: PINCH IN -> ZOOM OUT (change=%.1f) ***",
                        (double)totalDistanceChange);
                send_trackpad_zoom_out();
            }

            // Update tracking
            last_zoom_distance = totalDistanceChange;
            last_zoom_time = current_time;
            zoom_in_progress = true;

            LOG_INF("Zoom sent: last_distance=%.1f", (double)last_zoom_distance);
        }
    }
}

void reset_two_finger_state(struct gesture_state *state) {
    if (state->twoFingerActive) {
        LOG_DBG("Resetting two finger state - zoom flags will reset on next gesture");
        state->twoFingerActive = false;
    }

    if (state->lastXScrollReport != 0) {
        LOG_DBG("Resetting scroll accumulator: was %d", state->lastXScrollReport);
        state->lastXScrollReport = 0;
    }
}
