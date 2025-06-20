#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    // IMMEDIATE GESTURE HANDLING - No double-tap complexity
    if (data->gestures0) {
        LOG_INF("Hardware gesture detected: 0x%02x", data->gestures0);

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // IMMEDIATE SINGLE CLICK
                if (!state->isDragging) {
                    LOG_INF("*** SINGLE TAP -> IMMEDIATE CLICK ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_LEFT, 1, false);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_LEFT, 0, true);
                }
                break;

            case GESTURE_TAP_AND_HOLD:
                // IMMEDIATE drag start
                if (!state->isDragging) {
                    LOG_INF("*** TAP AND HOLD -> DRAG START ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_LEFT, 1, true);
                    state->isDragging = true;
                    state->dragStartSent = true;
                }
                break;

            default:
                LOG_DBG("Unknown single finger gesture: 0x%02x", data->gestures0);
                break;
        }
    }

    // Movement handling for single finger
    if (data->finger_count == 1) {
        float sensMp = (float)state->mouseSensitivity / 128.0F;

        // Process movement if we have any
        if (data->rx != 0 || data->ry != 0) {
            // Direct accumulation with correct axis mapping
            state->accumPos.x += -data->rx * sensMp;
            state->accumPos.y += -data->ry * sensMp;

            int16_t xp = (int16_t)state->accumPos.x;
            int16_t yp = (int16_t)state->accumPos.y;

            // Lower threshold for immediate response
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
    // IMMEDIATE drag release when fingers are lifted
    if (state->isDragging && state->dragStartSent) {
        LOG_INF("*** DRAG END - IMMEDIATE RELEASE ***");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_LEFT, 0, true);
        state->isDragging = false;
        state->dragStartSent = false;
    }

    // Reset accumulated position
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }
}
