// src/three_finger.c - Simple test version with better logging
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <math.h>
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Calculate average Y position of fingers
static float calculate_average_y(const struct iqs5xx_rawdata *data, int finger_count) {
    float sum = 0;
    for (int i = 0; i < finger_count && i < 3; i++) {
        sum += data->fingers[i].ay;
    }
    return sum / finger_count;
}

// Send F3 key using ZMK's HID system directly - Test version with more info
static void send_f3_key_direct(void) {
    LOG_INF("*** SENDING F3 KEY DIRECTLY ***");
    LOG_INF("Before F3 press - about to call zmk_hid_keyboard_press");

    // Use ZMK's HID keyboard system directly
    int ret1 = zmk_hid_keyboard_press(HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_F3));
    LOG_INF("zmk_hid_keyboard_press returned: %d", ret1);

    int ret2 = zmk_endpoints_send_report(0x07);
    LOG_INF("zmk_endpoints_send_report(0x07) returned: %d", ret2);

    // Short delay then release
    k_msleep(100); // Increased delay for testing

    LOG_INF("About to release F3 key");
    int ret3 = zmk_hid_keyboard_release(HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_F3));
    LOG_INF("zmk_hid_keyboard_release returned: %d", ret3);

    int ret4 = zmk_endpoints_send_report(0x07);
    LOG_INF("zmk_endpoints_send_report(0x07) release returned: %d", ret4);

    LOG_INF("F3 key sequence completed - check Mac for Mission Control!");
}

// Send F4 key using ZMK's HID system directly - Test version with more info
static void send_f4_key_direct(void) {
    LOG_INF("*** SENDING F4 KEY DIRECTLY ***");
    LOG_INF("Before F4 press - about to call zmk_hid_keyboard_press");

    // Use ZMK's HID keyboard system directly
    int ret1 = zmk_hid_keyboard_press(HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_F4));
    LOG_INF("zmk_hid_keyboard_press returned: %d", ret1);

    int ret2 = zmk_endpoints_send_report(0x07);
    LOG_INF("zmk_endpoints_send_report(0x07) returned: %d", ret2);

    // Short delay then release
    k_msleep(100); // Increased delay for testing

    LOG_INF("About to release F4 key");
    int ret3 = zmk_hid_keyboard_release(HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_F4));
    LOG_INF("zmk_hid_keyboard_release returned: %d", ret3);

    int ret4 = zmk_endpoints_send_report(0x07);
    LOG_INF("zmk_endpoints_send_report(0x07) release returned: %d", ret4);

    LOG_INF("F4 key sequence completed - check Mac for Launchpad!");
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
            LOG_INF("*** THREE FINGER SWIPE UP -> F3 KEY (MISSION CONTROL) ***");
            LOG_INF("=== LOOK FOR MISSION CONTROL TO APPEAR ON MAC ===");

            // Use the direct HID method
            send_f3_key_direct();

            // Reset tracking to prevent repeated triggers
            state->threeFingersPressed = false;
            last_swipe_time = current_time;
            return;
        }

        // Detect significant downward movement (swipe down)
        if (yMovement > TRACKPAD_THREE_FINGER_SWIPE_MIN_DIST) {
            LOG_INF("*** THREE FINGER SWIPE DOWN -> F4 KEY (LAUNCHPAD) ***");
            LOG_INF("=== LOOK FOR LAUNCHPAD TO APPEAR ON MAC ===");

            // Use the direct HID method
            send_f4_key_direct();

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
        // Middle click via input event (mouse events still use input system)
        send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, false);
        send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
    }

    if (state->threeFingersPressed) {
        state->threeFingersPressed = false;
        LOG_INF("*** THREE FINGERS RELEASED ***");
    }
}
