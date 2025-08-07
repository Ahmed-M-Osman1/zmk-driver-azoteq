/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include "iqs5xx.h"
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

// FIXED: Proper state cleanup after Mission Control
static void send_control_up(void) {
    // Clear any existing HID state first
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(10);

    // Press Control
    int ret1 = zmk_hid_keyboard_press(LEFT_CONTROL);
    if (ret1 < 0) {
        LOG_ERR("Failed to press LCTRL: %d", ret1);
        return;
    }
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(10);

    // Press Up Arrow
    int ret2 = zmk_hid_keyboard_press(UP_ARROW);
    if (ret2 < 0) {
        LOG_ERR("Failed to press UP_ARROW: %d", ret2);
        zmk_hid_keyboard_release(LEFT_CONTROL);
        zmk_endpoints_send_report(HID_USAGE_KEY);
        return;
    }
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(30); // Reduced hold time

    // Release Up Arrow
    zmk_hid_keyboard_release(UP_ARROW);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(5);

    // Release Control
    zmk_hid_keyboard_release(LEFT_CONTROL);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(10);

    // CRITICAL FIX: Complete cleanup after Mission Control
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(30); // Reduced cleanup time

    LOG_DBG("Sent Control+Up (Mission Control)");
}

// FIXED: Proper state cleanup after Application Windows
static void send_control_down(void) {
    // Clear any existing HID state first
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(10);

    // Press Control
    int ret1 = zmk_hid_keyboard_press(LEFT_CONTROL);
    if (ret1 < 0) {
        LOG_ERR("Failed to press LCTRL: %d", ret1);
        return;
    }
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(10);

    // Press Down Arrow
    int ret2 = zmk_hid_keyboard_press(DOWN_ARROW);
    if (ret2 < 0) {
        LOG_ERR("Failed to press DOWN_ARROW: %d", ret2);
        zmk_hid_keyboard_release(LEFT_CONTROL);
        zmk_endpoints_send_report(HID_USAGE_KEY);
        return;
    }
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(30); // Reduced hold time

    // Release Down Arrow
    zmk_hid_keyboard_release(DOWN_ARROW);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(5);

    // Release Control
    zmk_hid_keyboard_release(LEFT_CONTROL);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(10);

    // CRITICAL FIX: Complete cleanup after Application Windows
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(30); // Reduced cleanup time

    LOG_DBG("Sent Control+Down (App Expose)");
}

void handle_three_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    // Early exit if not exactly three fingers
    if (data->finger_count != 3) {
        return;
    }

    int64_t current_time = k_uptime_get();

    // Check global cooldown - block all processing if too recent
    if (current_time - global_gesture_cooldown < 500) { // Reduced to 500ms cooldown
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
        LOG_DBG("Three finger gesture started");
        return;
    }

    // Skip if gesture already triggered
    if (state->gestureTriggered) {
        return;
    }

    // Check for three finger swipe gestures after 150ms
    int64_t time_since_start = current_time - state->threeFingerPressTime;
    if (time_since_start > 150 && // Wait 150ms before checking swipes
        data->fingers[0].strength > 0 && data->fingers[1].strength > 0 && data->fingers[2].strength > 0) {

        // Calculate average movement in Y direction
        float initialAvgY = (float)(state->threeFingerStartPos[0].y +
                           state->threeFingerStartPos[1].y +
                           state->threeFingerStartPos[2].y) / 3.0f;
        float currentAvgY = calculate_average_y(data, 3);
        float yMovement = currentAvgY - initialAvgY;

        LOG_DBG("Three finger Y movement: %.1f", yMovement);

        // Detect significant movement (reduced threshold for better responsiveness)
        if (fabsf(yMovement) > 30.0f) {
            if (yMovement > 0) {
                // SWIPE DOWN = Application Windows (App Exposé)
                LOG_INF("Three finger swipe down detected - App Expose");
                send_control_down();
            } else {
                // SWIPE UP = Mission Control
                LOG_INF("Three finger swipe up detected - Mission Control");
                send_control_up();
            }

            // CRITICAL FIX: Complete state cleanup after gesture
            state->gestureTriggered = true;
            global_gesture_cooldown = current_time;
            state->threeFingersPressed = false;

            // Only reset three finger state to avoid interfering with other gestures
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
            LOG_DBG("Three finger tap detected");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, false);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
        }
    }

    if (state->threeFingersPressed) {
        state->threeFingersPressed = false;
        state->gestureTriggered = false;
        LOG_DBG("Three finger gesture reset");
    }
}
