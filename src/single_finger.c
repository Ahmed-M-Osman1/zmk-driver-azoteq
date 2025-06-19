#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Double-tap detection
static int64_t last_tap_time = 0;
static bool waiting_for_double_tap = false;
static struct k_work_delayable double_tap_work;

// Send delayed single click if no double-tap occurs
static void double_tap_timeout_handler(struct k_work *work) {
    if (waiting_for_double_tap) {
        LOG_INF("*** DELAYED SINGLE CLICK ***");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        waiting_for_double_tap = false;
    }
}

void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    // ============================================================================
    // PRIORITY 1: CLICK PROCESSING - Handle ALL click events IMMEDIATELY FIRST
    // ============================================================================

    if (data->gestures0) {
        LOG_INF("*** PRIORITY CLICK PROCESSING: Hardware gesture 0x%02x ***", data->gestures0);

        // Process clicks with ABSOLUTE PRIORITY - no other logic interferes
        if (data->gestures0 & GESTURE_SINGLE_TAP) {
            if (!state->isDragging) {
                int64_t current_time = k_uptime_get();

                // Double-tap detection with IMMEDIATE processing
                if (waiting_for_double_tap && (current_time - last_tap_time) < 400) {
                    // IMMEDIATE double-click
                    LOG_INF("*** IMMEDIATE DOUBLE TAP -> DOUBLE CLICK ***");
                    k_work_cancel_delayable(&double_tap_work);
                    waiting_for_double_tap = false;

                    // Send double-click immediately
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, false);
                    k_msleep(50); // Small delay between clicks
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);

                    last_tap_time = 0; // Reset to prevent triple-tap
                } else {
                    // First tap - IMMEDIATE processing setup
                    LOG_INF("*** IMMEDIATE FIRST TAP - SETTING UP DOUBLE-TAP DETECTION ***");
                    last_tap_time = current_time;
                    waiting_for_double_tap = true;

                    // Schedule single-click if no double-tap comes
                    k_work_schedule(&double_tap_work, K_MSEC(400));
                }
            }

            // RETURN IMMEDIATELY after processing click - don't process movement
            return;
        }

        if (data->gestures0 & GESTURE_TAP_AND_HOLD) {
            // IMMEDIATE drag start with ABSOLUTE PRIORITY
            if (!state->isDragging) {
                LOG_INF("*** IMMEDIATE TAP AND HOLD -> DRAG START ***");
                send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                state->isDragging = true;
                state->dragStartSent = true;
            }

            // RETURN IMMEDIATELY after processing click
            return;
        }

        // Handle other single finger gestures
        if (data->gestures0 & (GESTURE_SWIPE_X_NEG | GESTURE_SWIPE_X_POS |
                               GESTURE_SWIPE_Y_NEG | GESTURE_SWIPE_Y_POS)) {
            LOG_INF("*** IMMEDIATE SWIPE GESTURE: 0x%02x ***", data->gestures0);
            // Could add swipe handling here if needed
            return;
        }
    }

    // ============================================================================
    // PRIORITY 2: MOVEMENT PROCESSING - Only if no clicks were processed above
    // ============================================================================

    // Only process movement for single finger when no clicks are active
    if (data->finger_count == 1) {
        float sensMp = (float)state->mouseSensitivity / 128.0F;

        // Process movement if we have any
        if (data->rx != 0 || data->ry != 0) {
            // Direct accumulation with correct axis mapping
            state->accumPos.x += -data->rx * sensMp;
            state->accumPos.y += -data->ry * sensMp;

            int16_t xp = (int16_t)state->accumPos.x;
            int16_t yp = (int16_t)state->accumPos.y;

            // OPTIMIZED: Lower threshold for immediate response (0.3 instead of 0.5)
            if (fabsf(state->accumPos.x) >= 0.3f || fabsf(state->accumPos.y) >= 0.3f) {
                // Send movement events (works for both normal movement and drag)
                send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
                send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);

                // Reset accumulation, keeping fractional part
                state->accumPos.x -= xp;
                state->accumPos.y -= yp;
            }
        }
    }
}

void reset_single_finger_state(struct gesture_state *state) {
    // Initialize work queue on first call
    static bool work_initialized = false;
    if (!work_initialized) {
        k_work_init_delayable(&double_tap_work, double_tap_timeout_handler);
        work_initialized = true;
    }

    // Cancel any pending double-tap work with IMMEDIATE effect
    if (waiting_for_double_tap) {
        k_work_cancel_delayable(&double_tap_work);
        waiting_for_double_tap = false;
        LOG_DBG("Cancelled pending double-tap detection");
    }

    // IMMEDIATE drag release - this fixes the "stuck drag" issue
    if (state->isDragging && state->dragStartSent) {
        LOG_INF("*** IMMEDIATE DRAG END - RELEASING BUTTON ***");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        state->isDragging = false;
        state->dragStartSent = false;
    }

    // Reset accumulated position only if needed
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }
}
