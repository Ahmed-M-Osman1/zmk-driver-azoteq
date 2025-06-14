#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    bool hasGesture = false;

    // Handle single finger hardware gestures
    if (data->gestures0 && data->finger_count == 1) {
        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // Only handle single tap if we're not already dragging
                if (!state->isDragging) {
                    hasGesture = true;
                    LOG_INF("*** SINGLE TAP -> LEFT CLICK ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                }
                break;

            case GESTURE_TAP_AND_HOLD:
                // Only send the button press ONCE when drag starts
                if (!state->isDragging) {
                    LOG_INF("*** TAP AND HOLD -> DRAG START ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    state->isDragging = true;
                    state->dragStartSent = true;
                } else {
                    // We're already dragging, don't send more button presses
                    LOG_DBG("Already dragging, ignoring repeated tap-and-hold gesture");
                }
                hasGesture = true;
                break;

            default:
                if (data->gestures0 != 0) {
                    LOG_WRN("Unknown single finger gesture0: 0x%02x", data->gestures0);
                }
                break;
        }
    }

    // Movement handling - works during normal movement AND during drag
    if (!hasGesture && data->finger_count == 1) {
        float sensMp = (float)state->mouseSensitivity / 128.0F;

        // Accumulate movement
        state->accumPos.x += -data->rx * sensMp;
        state->accumPos.y += -data->ry * sensMp;

        int16_t xp = (int16_t)state->accumPos.x;
        int16_t yp = (int16_t)state->accumPos.y;

        // Send movement if threshold exceeded
        if (fabsf(state->accumPos.x) >= MOVEMENT_THRESHOLD || fabsf(state->accumPos.y) >= MOVEMENT_THRESHOLD) {
            LOG_DBG("Mouse movement: rx=%d,ry=%d -> accum=%.2f,%.2f -> move=%d,%d",
                    data->rx, data->ry, state->accumPos.x, state->accumPos.y, xp, yp);

            // Send movement events (works both for normal movement and drag)
            send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
            send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);

            // Reset accumulation, keeping fractional part
            state->accumPos.x -= xp;
            state->accumPos.y -= yp;
        }
    }
}

void reset_single_finger_state(struct gesture_state *state) {
    // Handle end of drag operation
    if (state->isDragging && state->dragStartSent) {
        LOG_INF("*** DRAG END - RELEASING BUTTON ***");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        state->isDragging = false;
        state->dragStartSent = false;
    }

    // Reset accumulated position
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        LOG_DBG("Resetting single finger accumulated position: was %.2f,%.2f",
                state->accumPos.x, state->accumPos.y);
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }
}
