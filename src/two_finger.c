#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Zoom-only state
struct zoom_state {
    float initial_distance;
    float last_distance;
    bool zoom_session_active;
    bool zoom_command_sent;
    int64_t session_start_time;
    int stable_readings;
};

static struct zoom_state zoom = {0};

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

    // Ensure we have two valid finger readings
    if (data->fingers[0].strength == 0 || data->fingers[1].strength == 0) {
        LOG_DBG("Skipping: invalid finger data (strength: %d, %d)",
                data->fingers[0].strength, data->fingers[1].strength);
        return;
    }

    int64_t current_time = k_uptime_get();

    // Initialize zoom session if just started
    if (!state->twoFingerActive) {
        state->twoFingerActive = true;
        state->twoFingerStartTime = current_time;

        // Reset zoom state
        zoom.initial_distance = calculate_distance(
            data->fingers[0].ax, data->fingers[0].ay,
            data->fingers[1].ax, data->fingers[1].ay
        );
        zoom.last_distance = zoom.initial_distance;
        zoom.zoom_session_active = false;
        zoom.zoom_command_sent = false;
        zoom.session_start_time = current_time;
        zoom.stable_readings = 0;

        LOG_INF("=== TWO FINGER SESSION START ===");
        LOG_INF("Initial positions: F0(%d,%d) F1(%d,%d)",
                data->fingers[0].ax, data->fingers[0].ay,
                data->fingers[1].ax, data->fingers[1].ay);
        LOG_INF("Initial distance: %d px", (int)zoom.initial_distance);
        return;
    }

    // Calculate current distance
    float current_distance = calculate_distance(
        data->fingers[0].ax, data->fingers[0].ay,
        data->fingers[1].ax, data->fingers[1].ay
    );

    int64_t time_since_start = current_time - zoom.session_start_time;
    float distance_change = current_distance - zoom.initial_distance;
    float distance_delta = current_distance - zoom.last_distance;

    // Update last distance
    zoom.last_distance = current_distance;

    // Count stable readings (small changes)
    if (fabsf(distance_delta) < 5.0f) {
        zoom.stable_readings++;
    } else {
        zoom.stable_readings = 0;
    }

    // Detailed logging for debugging - FIXED FLOAT FORMATTING
    LOG_INF("Time: %lld ms | Dist: init=%d, curr=%d, change=%d, delta=%d | Stable: %d",
            time_since_start, (int)zoom.initial_distance, (int)current_distance,
            (int)distance_change, (int)distance_delta, zoom.stable_readings);

    // Wait for session to stabilize before allowing zoom
    if (time_since_start < 200) { // Reduced from 500ms to 200ms
        LOG_INF("Waiting for stabilization (%lld/200 ms)", time_since_start);
        return;
    }

    // Require strong finger contact for zoom - LOWERED THRESHOLD
    if (data->fingers[0].strength < 2000 || data->fingers[1].strength < 2000) {
        LOG_INF("Insufficient strength: F0=%d, F1=%d (need >2000)",
                data->fingers[0].strength, data->fingers[1].strength);
        return;
    }

    // Only allow one zoom command per session
    if (zoom.zoom_command_sent) {
        LOG_DBG("Zoom already sent this session - waiting for finger lift");
        return;
    }

    // Check for significant distance change (zoom threshold)
    if (fabsf(distance_change) > 100.0f) {
        LOG_INF("*** ZOOM THRESHOLD EXCEEDED: %d px ***", (int)distance_change);

        if (!zoom.zoom_session_active) {
            zoom.zoom_session_active = true;
            LOG_INF("*** ZOOM SESSION ACTIVATED ***");
            LOG_INF("Distance change: %d px (threshold: 100px)", (int)distance_change);
        }

        // NEW: Lower stability requirement and add more debug
        if (zoom.stable_readings >= 2) { // Reduced from 3 to 2
            LOG_INF("*** ZOOM COMMAND READY - SENDING NOW ***");

            if (distance_change > 0) {
                LOG_INF("*** ZOOM IN: Fingers moved apart %d px ***", (int)distance_change);
                LOG_INF("*** SENDING CTRL+PLUS (ZOOM IN) ***");
                send_trackpad_zoom_in();

                // Also try direct input events as backup
                LOG_INF("*** ALSO SENDING DIRECT SCROLL EVENTS ***");
                send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, 3, true);  // Scroll up for zoom in

            } else {
                LOG_INF("*** ZOOM OUT: Fingers moved together %d px ***", (int)distance_change);
                LOG_INF("*** SENDING CTRL+MINUS (ZOOM OUT) ***");
                send_trackpad_zoom_out();

                // Also try direct input events as backup
                LOG_INF("*** ALSO SENDING DIRECT SCROLL EVENTS ***");
                send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, -3, true); // Scroll down for zoom out
            }

            zoom.zoom_command_sent = true;
            LOG_INF("Zoom command sent - session locked until finger lift");
        } else {
            LOG_INF("Zoom ready but waiting for stability (%d/2 stable readings)", zoom.stable_readings);
        }
    } else {
        LOG_INF("Distance change %d px below threshold (100px)", (int)distance_change);
    }

    // Show finger positions for debugging
    LOG_INF("Current positions: F0(%d,%d) F1(%d,%d) | Strengths: %d/%d",
            data->fingers[0].ax, data->fingers[0].ay,
            data->fingers[1].ax, data->fingers[1].ay,
            data->fingers[0].strength, data->fingers[1].strength);
}

void reset_two_finger_state(struct gesture_state *state) {
    if (state->twoFingerActive) {
        LOG_INF("=== TWO FINGER SESSION END ===");

        if (zoom.zoom_session_active) {
            LOG_INF("Zoom session completed: command_sent=%d", zoom.zoom_command_sent);
        } else {
            LOG_INF("Session ended without zoom activation");
        }

        state->twoFingerActive = false;

        // Reset zoom state
        memset(&zoom, 0, sizeof(zoom));

        LOG_INF("Ready for next two-finger session");
    }

    // Reset any scroll state (unused in zoom-only mode)
    if (state->lastXScrollReport != 0) {
        state->lastXScrollReport = 0;
    }
}
