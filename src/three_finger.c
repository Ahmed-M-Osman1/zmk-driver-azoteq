#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <math.h>
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Global cooldown to prevent gesture re-triggering
static int64_t global_gesture_cooldown = 0;

// Calculate average Y position of fingers
static float calculate_average_y(const struct iqs5xx_rawdata *data, int finger_count) {
    float sum = 0;
    for (int i = 0; i < finger_count && i < 3; i++) {
        sum += data->fingers[i].ay;
    }
    return sum / finger_count;
}

// Send Control+Up key combination
static void send_control_up(void) {
    LOG_INF("*** SENDING CONTROL+UP ***");
    send_keyboard_combo(INPUT_KEY_LEFTCTRL, INPUT_KEY_UP);
    LOG_INF("Control+Up sequence complete - Mission Control should appear!");
}

void handle_three_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    // Early exit if not exactly three fingers
    if (data->finger_count != 3) {
        LOG_DBG("Invalid finger count: %d", data->finger_count);
        return;
    }

    int64_t current_time = k_uptime_get();

    // Check global cooldown - block all processing if too recent
    if (current_time - global_gesture_cooldown < 1000) { // 1 second cooldown
        LOG_DBG("Three finger gesture blocked: in cooldown period (%lld ms remaining)",
                1000 - (current_time - global_gesture_cooldown));
        return;
    }

    // Initialize three finger tracking if just started
    if (!state->threeFingersPressed) {
        state->threeFingerPressTime = current_time;
        state->threeFingersPressed = true;
        state->gestureTriggered = false;

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

    // Skip if gesture already triggered
    if (state->gestureTriggered) {
        LOG_DBG("Gesture already triggered, ignoring event");
        return;
    }

    // Check for three finger swipe gestures after 100ms
    int64_t time_since_start = current_time - state->threeFingerPressTime;
    if (time_since_start > 100 && // Wait 100ms before checking swipes
        data->fingers[0].strength > 0 && data->fingers[1].strength > 0 && data->fingers[2].strength > 0) {

        // Calculate average movement in Y direction
        float initialAvgY = (float)(state->threeFingerStartPos[0].y +
                           state->threeFingerStartPos[1].y +
                           state->threeFingerStartPos[2].y) / 3.0f;
        float currentAvgY = calculate_average_y(data, 3);
        float yMovement = currentAvgY - initialAvgY;

        LOG_DBG("Three finger Y movement: initial_avg=%.1f, current_avg=%.1f, movement=%.1f",
                initialAvgY, currentAvgY, yMovement);

        // Detect significant downward movement (swipe down)
        if (yMovement > TRACKPAD_THREE_FINGER_SWIPE_MIN_DIST) {
            LOG_INF("*** THREE FINGER SWIPE DOWN -> CONTROL+UP ***");

            // Send Control+Up
            send_control_up();

            // Mark gesture as complete
            state->gestureTriggered = true;
            global_gesture_cooldown = current_time;
            state->threeFingersPressed = false;

            LOG_INF("Control+Up gesture complete - cooldown active for 1000ms");
            return;
        }
    }
}

void reset_three_finger_state(struct gesture_state *state) {
    // Handle three finger click (if fingers released quickly without swipe)
    if (state->threeFingersPressed && !state->gestureTriggered &&
        k_uptime_get() - state->threeFingerPressTime < TRACKPAD_THREE_FINGER_CLICK_TIME) {

        // Check if we're in gesture cooldown
        if (k_uptime_get() - global_gesture_cooldown > 500) {
            LOG_INF("*** THREE FINGER CLICK -> MIDDLE CLICK ***");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, false);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
        } else {
            LOG_DBG("Skipping three finger click - in cooldown");
        }
    }

    if (state->threeFingersPressed) {
        state->threeFingersPressed = false;
        state->gestureTriggered = false;
        LOG_DBG("Three fingers released");
    }
}
