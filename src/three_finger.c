/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include "iqs5xx.h"
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/usb_hid.h>

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

// FIXED: Proper state cleanup after Mission Control - CORRECTED HID CONSTANTS
static void send_control_up(void) {
    // Clear any existing HID state first
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(10);

    // Press Control (CORRECTED CONSTANT NAME - NO UNDERSCORES)
    int ret1 = zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
    if (ret1 < 0) {
        return;
    }
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(10);

    // Press Up Arrow (CORRECTED CONSTANT NAME)
    int ret2 = zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_UPARROW);
    if (ret2 < 0) {
        zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
        zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
        return;
    }
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(50); // Hold the combination

    // Release Up Arrow
    zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_UPARROW);
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(10);

    // Release Control
    zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(20);

    // CRITICAL FIX: Complete cleanup after Mission Control
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(50); // Give extra time for cleanup
}

// FIXED: Proper state cleanup after Application Windows - CORRECTED HID CONSTANTS
static void send_control_down(void) {
    // Clear any existing HID state first
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(10);

    // Press Control (CORRECTED CONSTANT NAME - NO UNDERSCORES)
    int ret1 = zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
    if (ret1 < 0) {
        return;
    }
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(10);

    // Press Down Arrow (CORRECTED CONSTANT NAME)
    int ret2 = zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_DOWNARROW);
    if (ret2 < 0) {
        zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
        zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
        return;
    }
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(50); // Hold the combination

    // Release Down Arrow
    zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_DOWNARROW);
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(10);

    // Release Control
    zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(20);

    // CRITICAL FIX: Complete cleanup after Application Windows
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(50); // Give extra time for cleanup
}

void handle_three_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    // Early exit if not exactly three fingers
    if (data->finger_count != 3) {
        return;
    }

    int64_t current_time = k_uptime_get();

    // Check global cooldown - block all processing if too recent
    if (current_time - global_gesture_cooldown < 1000) { // 1 second cooldown
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

        // Detect significant movement (50px threshold for better reliability)
        if (fabsf(yMovement) > 50.0f) {
            if (yMovement > 0) {
                // SWIPE DOWN = Application Windows (App Exposé)
                send_control_down();
            } else {
                // SWIPE UP = Mission Control - THIS IS THE PROBLEMATIC ONE
                send_control_up();
            }

            // CRITICAL FIX: Complete state cleanup after gesture
            state->gestureTriggered = true;
            global_gesture_cooldown = current_time;
            state->threeFingersPressed = false;

            // ADDITIONAL CLEANUP: Reset ALL gesture states to prevent contamination
            state->isDragging = false;
            state->dragStartSent = false;
            state->twoFingerActive = false;
            state->lastXScrollReport = 0;
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
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, false);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
        }
    }

    if (state->threeFingersPressed) {
        state->threeFingersPressed = false;
        state->gestureTriggered = false;
    }
}
