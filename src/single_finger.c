#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    // IMMEDIATE GESTURE HANDLING - following the working code pattern
    if (data->gestures0) {

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // Handle single tap - be smarter about drag state
                if (data->finger_count == 0) {
                    // Finger lift - if we were dragging, this should have been handled by reset_single_finger_state
                    // If drag state is stale, clear it and process the tap
                    if (state->isDragging) {
                        LOG_DBG("Clearing stale drag state before processing tap");
                        state->isDragging = false;
                        state->dragStartSent = false;
                    }
                    LOG_DBG("Single tap detected (finger lift)");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                } else if (data->finger_count == 1 && !state->isDragging) {
                    // Finger down tap - only if not currently dragging
                    LOG_DBG("Single tap detected (finger down)");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                } else {
                    LOG_DBG("Single tap ignored (dragging=%d, fingers=%d)", state->isDragging, data->finger_count);
                }
                break;

            case GESTURE_TAP_AND_HOLD:
                // IMMEDIATE drag start - only send button press ONCE (like working code)
                if (!state->isDragging) {
                    LOG_DBG("Tap and hold detected - starting drag");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    state->isDragging = true;
                    state->dragStartSent = true;
                } else {
                    // Already dragging, don't send more button presses
                }
                break;

            default:
                break;
        }
    }

    // Movement handling for single finger (following working code pattern)
    if (data->finger_count == 1) {
        float sensMp = (float)state->mouseSensitivity / 128.0F;

        // Process movement if we have any
        if (data->rx != 0 || data->ry != 0) {
            // FIXED: Use same axis mapping as working code
            state->accumPos.x += data->rx * sensMp;      // rx maps to X movement
            state->accumPos.y += data->ry * sensMp;      // ry maps to Y movement

            int16_t xp = (int16_t)state->accumPos.x;
            int16_t yp = (int16_t)state->accumPos.y;

            // Use same threshold as working code
            if (fabsf(state->accumPos.x) >= MOVEMENT_THRESHOLD || fabsf(state->accumPos.y) >= MOVEMENT_THRESHOLD) {

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
    // IMMEDIATE drag release when fingers are lifted (like working code)
    if (state->isDragging && state->dragStartSent) {
        LOG_DBG("Ending drag");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        state->isDragging = false;
        state->dragStartSent = false;
    }

    // Reset accumulated position (like working code)
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }
}
