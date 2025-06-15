// src/three_finger.c - Enhanced to prevent multiple F4 presses and ensure proper release
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

// Send F4 key using ZMK's HID system directly - Robust version
static void send_f4_key_direct(void) {
    LOG_INF("*** SENDING F4 KEY DIRECTLY ***");

    // Ensure no keys are stuck by clearing all keyboard states
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);

    // Press F4
    int ret1 = zmk_hid_keyboard_press(F4);
    LOG_INF("F4 press: %d", ret1);
    if (ret1 < 0) {
        LOG_ERR("Failed to press F4");
        return;
    }

    // Send report for press
    int ret2 = zmk_endpoints_send_report(0x07);
    LOG_INF("F4 press report: %d", ret2);
    if (ret2 < 0) {
        LOG_ERR("Failed to send F4 press report");
        zmk_hid_keyboard_release(F4); // Clean up on failure
        return;
    }

    // Short delay to ensure the press is registered
    k_msleep(50);

    // Release F4
    int ret3 = zmk_hid_keyboard_release(F4);
    LOG_INF("F4 release: %d", ret3);
    if (ret3 < 0) {
        LOG_ERR("Failed to release F4");
    }

    // Send report for release
    int ret4 = zmk_endpoints_send_report(0x07);
    LOG_INF("F4 release report: %d", ret4);
    if (ret4 < 0) {
        LOG_ERR("Failed to send F4 release report");
    }

    LOG_INF("F4 sequence complete - Launchpad should appear!");
}

void handle_three_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    if (data->finger_count != 3) {
        return;
    }

    int64_t current_time = k_uptime_get();

    // Check global cooldown - prevent any gesture processing if too recent
    if (current_time - global_gesture_cooldown < 1000) { // 1 second cooldown
        LOG_DBG("Three finger gesture in cooldown period");
        return;
    }

    // Initialize three finger tracking if just started
    if (!state->threeFingersPressed) {
        state->threeFingerPressTime = current_time;
        state->threeFingersPressed = true;
        state->gestureTriggered = false; // Track if a gesture was triggered in this session

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

    // Skip further processing if a gesture was already triggered in this session
    if (state->gestureTriggered) {
        LOG_DBG("ahmed :: Gesture already triggered in this session, ignoring");
        return;
    }

    // Check for three finger swipe gestures - only check after some time has passed
    int64_t time_since_start = current_time - state->threeFingerPressTime;
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

        // Detect significant downward movement (swipe down)
        if (yMovement > TRACKPAD_THREE_FINGER_SWIPE_MIN_DIST) {
            LOG_INF("*** THREE FINGER SWIPE DOWN -> F4 KEY (LAUNCHPAD) ***");

            // Use the direct HID method
            send_f4_key_direct();

            // Mark gesture as triggered to prevent re-processing
            state->gestureTriggered = true;
            global_gesture_cooldown = current_time;
            state->threeFingersPressed = false;

            LOG_INF("F4 gesture complete - cooldown active");
            return;
        }
    }
}

void reset_three_finger_state(struct gesture_state *state) {
    // Handle three finger click (if fingers released quickly without swipe)
    if (state->threeFingersPressed && !state->gestureTriggered &&
        k_uptime_get() - state->threeFingerPressTime < TRACKPAD_THREE_FINGER_CLICK_TIME) {

        // Check if we're in gesture cooldown (don't do click if gesture just happened)
        if (k_uptime_get() - global_gesture_cooldown > 500) {
            LOG_INF("*** THREE FINGER CLICK -> MIDDLE CLICK ***");
            // Middle click via input event (mouse events still use input system)
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, false);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
        } else {
            LOG_DBG("Skipping three finger click - in gesture cooldown");
        }
    }

    if (state->threeFingersPressed) {
        state->threeFingersPressed = false;
        state->gestureTriggered = false;
        LOG_DBG("Three fingers released");
    }
}
