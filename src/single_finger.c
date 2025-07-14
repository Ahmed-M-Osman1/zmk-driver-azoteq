#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Enhanced movement filtering to prevent jumps
static bool is_movement_valid(int16_t rx, int16_t ry, struct gesture_state *state) {
    // Calculate movement magnitude
    float magnitude = sqrtf((float)(rx * rx + ry * ry));

    // Reject obviously impossible movements (jumps)
    if (magnitude > 200.0f) {
        LOG_WRN("Rejecting large movement: rx=%d, ry=%d, magnitude=%.1f", rx, ry, magnitude);
        return false;
    }

    // Additional filtering for very small movements that might be noise
    if (magnitude < 0.5f) {
        return false;
    }

    return true;
}

void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    // IMMEDIATE GESTURE HANDLING - following the working code pattern
    if (data->gestures0) {
        LOG_DBG("Single finger gesture detected: 0x%02x", data->gestures0);

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // IMMEDIATE SINGLE CLICK - only if not already dragging
                if (!state->isDragging) {
                    LOG_DBG("Single tap - sending click");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                }
                break;

            case GESTURE_TAP_AND_HOLD:
                // IMMEDIATE drag start - only send button press ONCE
                if (!state->isDragging) {
                    LOG_DBG("Tap and hold - starting drag");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    state->isDragging = true;
                    state->dragStartSent = true;
                } else {
                    LOG_DBG("Already dragging, ignoring tap and hold");
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

        // Process movement if we have any AND it passes validation
        if ((data->rx != 0 || data->ry != 0) && is_movement_valid(data->rx, data->ry, state)) {
            // Apply sensitivity scaling
            state->accumPos.x += data->rx * sensMp;
            state->accumPos.y += data->ry * sensMp;

            int16_t xp = (int16_t)state->accumPos.x;
            int16_t yp = (int16_t)state->accumPos.y;

            // Use threshold to prevent micro-movements
            if (fabsf(state->accumPos.x) >= MOVEMENT_THRESHOLD || fabsf(state->accumPos.y) >= MOVEMENT_THRESHOLD) {

                // Additional validation before sending movement
                if (abs(xp) <= 100 && abs(yp) <= 100) { // Sanity check
                    // Send movement events (works for both normal movement and drag)
                    send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
                    send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);

                    LOG_DBG("Movement: rx=%d, ry=%d -> xp=%d, yp=%d (sens=%.2f, drag=%d)",
                            data->rx, data->ry, xp, yp, sensMp, state->isDragging);

                    // Reset accumulation, keeping fractional part
                    state->accumPos.x -= xp;
                    state->accumPos.y -= yp;
                } else {
                    LOG_WRN("Filtered out large movement: xp=%d, yp=%d", xp, yp);
                    // Reset accumulation to prevent buildup
                    state->accumPos.x = 0;
                    state->accumPos.y = 0;
                }
            }
        }
    }
}

void reset_single_finger_state(struct gesture_state *state) {
    // IMMEDIATE drag release when fingers are lifted
    if (state->isDragging && state->dragStartSent) {
        LOG_DBG("Ending drag");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        state->isDragging = false;
        state->dragStartSent = false;
    }

    // Reset accumulated position
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        LOG_DBG("Resetting accumulated position: x=%.2f, y=%.2f", state->accumPos.x, state->accumPos.y);
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }
}
