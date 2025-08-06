#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(trackpad, CONFIG_ZMK_LOG_LEVEL);

void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    const struct iqs5xx_config *config = dev->config;

    LOG_DBG("Single finger handler: fingers=%d, g0=0x%02x, rel=(%d,%d)",
            data->finger_count, data->gestures0, data->rx, data->ry);

    // IMMEDIATE GESTURE HANDLING - following the working code pattern
    if (data->gestures0) {
        LOG_INF("Processing single finger gesture: 0x%02x", data->gestures0);

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // IMMEDIATE SINGLE CLICK - only if not already dragging (like working code)
                if (!state->isDragging) {
                    LOG_INF("Single tap detected - sending left click");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                }
                break;

            case GESTURE_TAP_AND_HOLD:
                // IMMEDIATE drag start - only send button press ONCE (like working code)
                if (!state->isDragging) {
                    LOG_INF("Tap and hold detected - starting drag");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    state->isDragging = true;
                    state->dragStartSent = true;
                } else {
                    LOG_DBG("Already dragging, ignoring tap and hold");
                }
                break;

            default:
                LOG_INF("Unknown single finger gesture: 0x%02x", data->gestures0);
                break;
        }
    }

    // Movement handling for single finger (following working code pattern)
    if (data->finger_count == 1) {
        // Use sensitivity from device configuration
        float sensMp = (float)config->sensitivity / 128.0F;
        LOG_DBG("Using sensitivity multiplier: %f", sensMp);

        // Process movement if we have any
        if (data->rx != 0 || data->ry != 0) {
            LOG_DBG("Raw movement: (%d,%d), sensitivity: %f", data->rx, data->ry, sensMp);

            // Note: rx and ry are already transformed by apply_coordinate_transform()
            state->accumPos.x += data->rx * sensMp;
            state->accumPos.y += data->ry * sensMp;

            int16_t xp = (int16_t)state->accumPos.x;
            int16_t yp = (int16_t)state->accumPos.y;

            LOG_DBG("Accumulated position: (%f,%f), integer: (%d,%d)",
                    state->accumPos.x, state->accumPos.y, xp, yp);

            // Use same threshold as working code
            if (fabsf(state->accumPos.x) >= MOVEMENT_THRESHOLD || fabsf(state->accumPos.y) >= MOVEMENT_THRESHOLD) {

                LOG_INF("Sending mouse movement: (%d,%d)", xp, yp);

                // Send movement events (works for both normal movement and drag)
                send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
                send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);

                // Reset accumulation, keeping fractional part
                state->accumPos.x -= xp;
                state->accumPos.y -= yp;

                LOG_DBG("Remaining accumulation: (%f,%f)", state->accumPos.x, state->accumPos.y);
            }
        }
    }
}

void reset_single_finger_state(struct gesture_state *state) {
    LOG_DBG("Resetting single finger state");

    // IMMEDIATE drag release when fingers are lifted (like working code)
    if (state->isDragging && state->dragStartSent) {
        LOG_INF("Ending drag - releasing left button");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        state->isDragging = false;
        state->dragStartSent = false;
    }

    // Reset accumulated position (like working code)
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        LOG_DBG("Clearing accumulated position: (%f,%f)", state->accumPos.x, state->accumPos.y);
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }
}
