#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(trackpad, CONFIG_ZMK_LOG_LEVEL);

// FIXED: Enhanced movement validation to prevent direction reversals
static bool is_movement_valid(int16_t rx, int16_t ry, struct gesture_state *state) {
    // Calculate movement magnitude
    float magnitude = sqrtf((float)(rx * rx + ry * ry));

    // Reject obviously impossible movements (prevent jumps/reversals)
    if (magnitude > 100.0f) {
        LOG_WRN("Rejecting large movement: rx=%d, ry=%d, magnitude=%.1f", rx, ry, magnitude);
        return false;
    }

    // Filter out tiny movements that might be noise
    if (magnitude < 0.8f) {
        return false;
    }

    // Additional consistency check - prevent sudden direction reversals
    static int16_t last_rx = 0, last_ry = 0;
    static int consistent_direction_count = 0;

    // Check if movement is in roughly the same direction as previous movement
    if (last_rx != 0 || last_ry != 0) {
        float dot_product = (float)(rx * last_rx + ry * last_ry);
        float last_magnitude = sqrtf((float)(last_rx * last_rx + last_ry * last_ry));

        if (last_magnitude > 0) {
            float cosine_similarity = dot_product / (magnitude * last_magnitude);

            // If movement is in opposite direction (cosine < -0.5), be suspicious
            if (cosine_similarity < -0.5f && consistent_direction_count > 3) {
                LOG_WRN("Rejecting direction reversal: rx=%d, ry=%d, last=(%d,%d), cos=%.2f",
                        rx, ry, last_rx, last_ry, cosine_similarity);
                return false;
            }

            // Count consistent movements
            if (cosine_similarity > 0.5f) {
                consistent_direction_count++;
            } else {
                consistent_direction_count = 0;
            }
        }
    }

    // Update last movement
    last_rx = rx;
    last_ry = ry;

    return true;
}

void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    const struct iqs5xx_config *config = dev->config;

    LOG_DBG("Single finger handler: fingers=%d, g0=0x%02x, rel=(%d,%d)",
            data->finger_count, data->gestures0, data->rx, data->ry);

    // IMMEDIATE GESTURE HANDLING - following the working code pattern
    if (data->gestures0) {
        LOG_INF("Processing single finger gesture: 0x%02x", data->gestures0);

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // IMMEDIATE SINGLE CLICK - only if not already dragging
                if (!state->isDragging) {
                    LOG_INF("Single tap detected - sending left click");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                }
                break;

            case GESTURE_TAP_AND_HOLD:
                // IMMEDIATE drag start - only send button press ONCE
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

    // Movement handling for single finger
    if (data->finger_count == 1) {
        // Use sensitivity from device configuration (FIXED: proper sensitivity handling)
        float sensMp = (float)config->sensitivity / 128.0F;
        LOG_DBG("Using sensitivity multiplier: %f", sensMp);

        // Process movement if we have any AND it passes validation
        if ((data->rx != 0 || data->ry != 0) && is_movement_valid(data->rx, data->ry, state)) {
            LOG_DBG("Raw movement: (%d,%d), sensitivity: %f", data->rx, data->ry, sensMp);

            // Apply sensitivity scaling to already-transformed coordinates
            state->accumPos.x += data->rx * sensMp;
            state->accumPos.y += data->ry * sensMp;

            int16_t xp = (int16_t)state->accumPos.x;
            int16_t yp = (int16_t)state->accumPos.y;

            LOG_DBG("Accumulated position: (%f,%f), integer: (%d,%d)",
                    state->accumPos.x, state->accumPos.y, xp, yp);

            // Use threshold to prevent micro-movements
            if (fabsf(state->accumPos.x) >= MOVEMENT_THRESHOLD || fabsf(state->accumPos.y) >= MOVEMENT_THRESHOLD) {

                // Additional sanity check before sending
                if (abs(xp) <= 50 && abs(yp) <= 50) { // Reasonable movement bounds
                    LOG_INF("Sending mouse movement: (%d,%d) [drag=%s]", xp, yp,
                            state->isDragging ? "YES" : "NO");

                    // Send movement events (works for both normal movement and drag)
                    send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
                    send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);

                    // Reset accumulation, keeping fractional part
                    state->accumPos.x -= xp;
                    state->accumPos.y -= yp;

                    LOG_DBG("Remaining accumulation: (%f,%f)", state->accumPos.x, state->accumPos.y);
                } else {
                    LOG_WRN("Filtered out large accumulated movement: xp=%d, yp=%d", xp, yp);
                    // Reset accumulation to prevent buildup
                    state->accumPos.x = 0;
                    state->accumPos.y = 0;
                }
            }
        }
    }
}

void reset_single_finger_state(struct gesture_state *state) {
    LOG_DBG("Resetting single finger state");

    // IMMEDIATE drag release when fingers are lifted
    if (state->isDragging && state->dragStartSent) {
        LOG_INF("Ending drag - releasing left button");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        state->isDragging = false;
        state->dragStartSent = false;
    }

    // Reset accumulated position
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        LOG_DBG("Clearing accumulated position: (%f,%f)", state->accumPos.x, state->accumPos.y);
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }
}
