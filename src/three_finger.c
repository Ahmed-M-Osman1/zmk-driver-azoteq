#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Calculate average Y position of fingers
static float calculate_average_y(const struct iqs5xx_rawdata *data, int finger_count) {
    float sum = 0;
    for (int i = 0; i < finger_count && i < 3; i++) {
        sum += data->fingers[i].ay;
    }
    return sum / finger_count;
}

void handle_three_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    if (data->finger_count != 3) {
        return;
    }

    // Initialize three finger tracking if just started
    if (!state->threeFingersPressed) {
        state->threeFingerPressTime = k_uptime_get();
        state->threeFingersPressed = true;

        // Store initial positions for swipe detection
        for (int i = 0; i < 3; i++) {
            state->threeFingerStartPos[i].x = data->fingers[i].ax;
            state->threeFingerStartPos[i].y = data->fingers[i].ay;
        }

        LOG_INF("*** THREE FINGERS DETECTED - START ***");
        LOG_DBG("Initial positions: (%d,%d), (%d,%d), (%d,%d)",
                state->threeFingerStartPos[0].x, state->threeFingerStartPos[0].y,
                state->threeFingerStartPos[1].x, state->threeFingerStartPos[1].y,
                state->threeFingerStartPos[2].x, state->threeFingerStartPos[2].y);
        return;
    }

    // Check for three finger swipe gestures - only check after some time has passed
    int64_t time_since_start = k_uptime_get() - state->threeFingerPressTime;
    if (state->threeFingersPressed && time_since_start > 100 && // Wait 100ms before checking swipes
        data->fingers[0].strength > 0 && data->fingers[1].strength > 0 && data->fingers[2].strength > 0) {

        // Calculate average movement in Y direction
        float initialAvgY = (float)(state->threeFingerStartPos[0].y +
                           state->threeFingerStartPos[1].y +
                           state->threeFingerStartPos[2].y) / 3.0f;
        float currentAvgY = calculate_average_y(data, 3);

        float yMovement = currentAvgY - initialAvgY;

        LOG_DBG("Three finger Y movement: initial_avg=%d, current_avg=%d, movement=%d",
                (int)initialAvgY, (int)currentAvgY, (int)yMovement);

        // Only trigger if movement is significant AND we haven't triggered recently
        static int64_t last_swipe_time = 0;
        int64_t current_time = k_uptime_get();

        // Prevent rapid re-triggering (500ms cooldown)
        if (current_time - last_swipe_time < 500) {
            return;
        }

        // Detect significant upward movement (swipe up)
        if (yMovement < -TRACKPAD_THREE_FINGER_SWIPE_MIN_DIST) {
            LOG_INF("*** THREE FINGER SWIPE UP -> A KEY TEST ***");

            // Send A key as test (code 30)
            send_input_event(INPUT_EV_KEY, 30, 1, false);
            send_input_event(INPUT_EV_KEY, 30, 0, true);

            // Reset tracking to prevent repeated triggers
            state->threeFingersPressed = false;
            last_swipe_time = current_time;
            return;
        }

        // Detect significant downward movement (swipe down)
        if (yMovement > TRACKPAD_THREE_FINGER_SWIPE_MIN_DIST) {
            LOG_INF("*** THREE FINGER SWIPE DOWN -> B KEY TEST ***");

            // Send B key as test (code 48)
            send_input_event(INPUT_EV_KEY, 48, 1, false);
            send_input_event(INPUT_EV_KEY, 48, 0, true);

            // Reset tracking to prevent repeated triggers
            state->threeFingersPressed = false;
            last_swipe_time = current_time;
            return;
        }
    }
}

void reset_three_finger_state(struct gesture_state *state) {
    // Handle three finger click (if fingers released quickly without swipe)
    if (state->threeFingersPressed &&
        k_uptime_get() - state->threeFingerPressTime < TRACKPAD_THREE_FINGER_CLICK_TIME) {
        LOG_INF("*** THREE FINGER CLICK -> MIDDLE CLICK ***");
        // Middle click via input event
        send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, false);
        send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
    }

    if (state->threeFingersPressed) {
        state->threeFingersPressed = false;
        LOG_INF("*** THREE FINGERS RELEASED ***");
    }
}
