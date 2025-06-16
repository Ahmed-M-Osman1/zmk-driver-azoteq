#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    bool hasGesture = false;

    // Handle single finger hardware gestures FIRST (prioritize hardware detection)
    if (data->gestures0 && data->finger_count == 1) {
        LOG_DBG("Single finger gesture detected: 0x%02x", data->gestures0);

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // Only handle single tap if we're not already dragging
                if (!state->isDragging) {
                    hasGesture = true;
                    LOG_INF("*** SINGLE TAP -> LEFT CLICK ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                } else {
                    LOG_DBG("Ignoring single tap - already dragging");
                }
                break;

            case GESTURE_TAP_AND_HOLD:
                // IMMEDIATE drag start - no delays!
                if (!state->isDragging) {
                    LOG_INF("*** TAP AND HOLD -> IMMEDIATE DRAG START ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    state->isDragging = true;
                    state->dragStartSent = true;
                    hasGesture = true;
                } else {
                    // We're already dragging, don't send more button presses
                    LOG_DBG("Already dragging, continuing...");
                    hasGesture = true; // Still a gesture, just continuing
                }
                break;

            default:
                LOG_DBG("Unknown single finger gesture0: 0x%02x", data->gestures0);
                break;
        }
    }

    // Movement handling - ALWAYS process movement for single finger
    if (data->finger_count == 1) {
        float sensMp = (float)state->mouseSensitivity / 128.0F;

        // Process movement if we have any relative movement data
        if (data->rx != 0 || data->ry != 0) {
            // Accumulate movement - CORRECT axis mapping
            state->accumPos.x += -data->rx * sensMp;  // Invert X for natural feel
            state->accumPos.y += -data->ry * sensMp;  // Invert Y for natural feel

            int16_t xp = (int16_t)state->accumPos.x;
            int16_t yp = (int16_t)state->accumPos.y;

            // LOWER threshold for immediate response
            if (fabsf(state->accumPos.x) >= 0.3f || fabsf(state->accumPos.y) >= 0.3f) {
                LOG_DBG("Mouse movement: rx=%d,ry=%d -> move=%d,%d (dragging=%d)",
                        data->rx, data->ry, xp, yp, state->isDragging);

                // Send movement events (works both for normal movement and drag)
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
    // Handle end of drag operation - IMMEDIATE release
    if (state->isDragging && state->dragStartSent) {
        LOG_INF("*** DRAG END - IMMEDIATE BUTTON RELEASE ***");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        state->isDragging = false;
        state->dragStartSent = false;
    }

    // Reset accumulated position
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        LOG_DBG("Resetting accumulated position: was %.2f,%.2f",
                (double)state->accumPos.x, (double)state->accumPos.y);
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }
}
