#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    bool hasGesture = false;

    // OPTIMIZED: Handle hardware gestures FIRST (immediate response)
    if (data->gestures0 && data->finger_count == 1) {
        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // Only handle single tap if we're not already dragging
                if (!state->isDragging) {
                    hasGesture = true;
                    LOG_INF("*** SINGLE TAP -> LEFT CLICK ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                }
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
