#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

// External work queue for edge continuation - defined in trackpad.c
extern struct k_work_delayable edge_continuation_work;

// Edge continuation update function
void update_edge_continuation(struct gesture_state *state) {
    if (!state->edgeContinuationActive) {
        return;
    }
    
    int64_t current_time = k_uptime_get();
    int64_t elapsed = current_time - state->edgeContinuationStartTime;
    
    // Stop continuation after duration expires or velocity gets too low
    if (elapsed > CONTINUATION_DURATION_MS || state->continuationVelocity < 0.1f) {
        state->edgeContinuationActive = false;
        state->continuationVelocity = 0;
        return;
    }
    
    // Calculate synthetic movement based on last direction and current velocity
    float move_x = state->lastMovementDirection.x * state->continuationVelocity;
    float move_y = state->lastMovementDirection.y * state->continuationVelocity;
    
    // Apply movement to accumulator
    state->accumPos.x += move_x;
    state->accumPos.y += move_y;
    
    // Send movement if threshold is reached
    int16_t xp = (int16_t)state->accumPos.x;
    int16_t yp = (int16_t)state->accumPos.y;
    
    if (fabsf(state->accumPos.x) >= MOVEMENT_THRESHOLD || fabsf(state->accumPos.y) >= MOVEMENT_THRESHOLD) {
        send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
        send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);
        
        // Reset accumulation, keeping fractional part
        state->accumPos.x -= xp;
        state->accumPos.y -= yp;
    }
    
    // Decay velocity
    state->continuationVelocity *= CONTINUATION_DECAY_FACTOR;
}


void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    // IMMEDIATE GESTURE HANDLING - following the working code pattern
    if (data->gestures0) {

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // IMMEDIATE SINGLE CLICK - check for stale drag state first
                if (state->isDragging && !state->dragStartSent) {
                    // Stale drag state, clear it
                    state->isDragging = false;
                }

                if (!state->isDragging) {
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                }
                break;

            case GESTURE_TAP_AND_HOLD:
                // IMMEDIATE drag start - only send button press ONCE (like working code)
                if (!state->isDragging) {
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    state->isDragging = true;
                    state->dragStartSent = true;
                } else if (state->isDragging && !state->dragStartSent) {
                    // Drag state exists but button wasn't sent - fix it
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    state->dragStartSent = true;
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
            // Store movement direction for edge continuation
            state->lastMovementDirection.x = data->rx * sensMp;
            state->lastMovementDirection.y = data->ry * sensMp;
            
            // Calculate movement magnitude for velocity tracking
            float movement_magnitude = sqrtf(state->lastMovementDirection.x * state->lastMovementDirection.x + 
                                             state->lastMovementDirection.y * state->lastMovementDirection.y);
            
            // Check if finger is at trackpad edge and we have significant movement
            bool at_left_edge = data->fingers[0].ax <= EDGE_THRESHOLD;
            bool at_right_edge = data->fingers[0].ax >= (TRACKPAD_MAX_X - EDGE_THRESHOLD);
            bool at_top_edge = data->fingers[0].ay <= EDGE_THRESHOLD;
            bool at_bottom_edge = data->fingers[0].ay >= (TRACKPAD_MAX_Y - EDGE_THRESHOLD);
            
            bool at_edge = at_left_edge || at_right_edge || at_top_edge || at_bottom_edge;
            
            // Start edge continuation if at edge with movement in same direction
            if (at_edge && movement_magnitude > 1.0f) {
                // Check if movement direction aligns with edge (moving towards the edge)
                bool should_continue = false;
                if (at_left_edge && state->lastMovementDirection.x < 0) should_continue = true;
                if (at_right_edge && state->lastMovementDirection.x > 0) should_continue = true;
                if (at_top_edge && state->lastMovementDirection.y < 0) should_continue = true;
                if (at_bottom_edge && state->lastMovementDirection.y > 0) should_continue = true;
                
                if (should_continue) {
                    state->edgeContinuationActive = true;
                    state->edgeContinuationStartTime = k_uptime_get();
                    state->continuationVelocity = movement_magnitude * 0.5f; // Start with reduced velocity
                    
                    // Start the edge continuation work queue
                    k_work_reschedule(&edge_continuation_work, K_MSEC(CONTINUATION_UPDATE_INTERVAL_MS));
                }
            } else {
                // Not at edge, stop continuation
                state->edgeContinuationActive = false;
                k_work_cancel_delayable(&edge_continuation_work);
            }

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
    } else {
        // No finger detected, stop continuation
        state->edgeContinuationActive = false;
        k_work_cancel_delayable(&edge_continuation_work);
    }
    
    // Update edge continuation if active
    update_edge_continuation(state);
}

void reset_single_finger_state(struct gesture_state *state) {
    // IMMEDIATE drag release when fingers are lifted (like working code)
    if (state->isDragging && state->dragStartSent) {
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        state->isDragging = false;
        state->dragStartSent = false;
    }

    // Stop edge continuation when fingers are lifted
    state->edgeContinuationActive = false;
    state->continuationVelocity = 0;
    k_work_cancel_delayable(&edge_continuation_work);

    // Reset accumulated position (like working code)
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }
}
