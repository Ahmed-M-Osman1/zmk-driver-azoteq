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
    bool hasGesture = false;

    // CRITICAL: Handle hardware gestures IMMEDIATELY when detected
    if (data->gestures0) {
        LOG_INF("Hardware gesture detected: 0x%02x", data->gestures0);

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // FIXED: Handle single tap REGARDLESS of drag state - tap is detected AFTER finger lift
                LOG_INF("Processing single tap (drag state: %s)", state->isDragging ? "active" : "inactive");

                int64_t current_time = k_uptime_get();

                // Check for double-tap (within 400ms)
                if (waiting_for_double_tap && (current_time - last_tap_time) < 400) {
                    // Double-tap detected!
                    LOG_INF("*** DOUBLE TAP -> DOUBLE CLICK ***");
                    k_work_cancel_delayable(&double_tap_work);
                    waiting_for_double_tap = false;

                    // Send double-click
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, false);
                    k_msleep(50); // Small delay between clicks
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);

                    last_tap_time = 0; // Reset to prevent triple-tap
                } else {
                    // First tap - wait for potential double-tap
                    LOG_INF("*** FIRST TAP - WAITING FOR DOUBLE ***");
                    last_tap_time = current_time;
                    waiting_for_double_tap = true;

                    // Schedule single-click if no double-tap comes
                    k_work_schedule(&double_tap_work, K_MSEC(400));
                }
                hasGesture = true;
                break;

            case GESTURE_TAP_AND_HOLD:
                // IMMEDIATE drag start - no delays!
                if (!state->isDragging) {
                    LOG_INF("*** TAP AND HOLD -> DRAG START ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    state->isDragging = true;
                    state->dragStartSent = true;
                }
                hasGesture = true; // Mark as handled even if already dragging
                break;

            default:
                LOG_DBG("Unknown single finger gesture: 0x%02x", data->gestures0);
                break;
        }
    }

    // OPTIMIZED: Movement handling - always process for single finger
    if (data->finger_count == 1) {
        float sensMp = (float)state->mouseSensitivity / 128.0F;

        // Process movement if we have any
        if (data->rx != 0 || data->ry != 0) {
            // OPTIMIZED: Direct accumulation with correct axis mapping
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

    // Cancel any pending double-tap work
    if (waiting_for_double_tap) {
        k_work_cancel_delayable(&double_tap_work);
        waiting_for_double_tap = false;
    }

    // OPTIMIZED: IMMEDIATE drag release - this fixes the "stuck drag" issue
    if (state->isDragging && state->dragStartSent) {
        LOG_INF("*** DRAG END - IMMEDIATE RELEASE ***");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        state->isDragging = false;
        state->dragStartSent = false;
    }

    // OPTIMIZED: Reset accumulated position only if needed
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }
}
