#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

// Helper function to calculate distance between two points
static float calculate_distance_2d(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    return sqrtf(dx * dx + dy * dy);
}

void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    int64_t current_time = k_uptime_get();
    
    // Handle hardware gesture events first
    if (data->gestures0) {
        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // Always process single tap normally
                // Clean up any stale drag state
                if (state->isDragging && !state->dragStartSent) {
                    state->isDragging = false;
                }
                
                // Only process tap if not already dragging
                if (!state->isDragging && !state->dragLockActive) {
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                }
                break;

            case GESTURE_TAP_AND_HOLD:
                // Always start with normal drag - drag lock only activates with two fingers
                if (!state->isDragging) {
                    // Start normal drag immediately
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    state->isDragging = true;
                    state->dragStartSent = true;
                }
                break;

            default:
                break;
        }
    }

    // Handle single finger movement (cursor movement and normal dragging)
    if (data->finger_count == 1) {
        float sensMp = (float)state->mouseSensitivity / 128.0F;
        
        // Handle movement - always process single finger movement when there's only one finger
        if (data->rx != 0 || data->ry != 0) {
            // Normal cursor movement or dragging
            state->accumPos.x += data->rx * sensMp;
            state->accumPos.y += data->ry * sensMp;

            int16_t xp = (int16_t)state->accumPos.x;
            int16_t yp = (int16_t)state->accumPos.y;

            if (fabsf(state->accumPos.x) >= MOVEMENT_THRESHOLD || fabsf(state->accumPos.y) >= MOVEMENT_THRESHOLD) {
                send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
                send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);

                state->accumPos.x -= xp;
                state->accumPos.y -= yp;
            }
        }
    }
}

// Handle drag lock with two fingers
void handle_drag_lock_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    // Only process if drag lock is active with button sent and we have 2 fingers
    if (!state->dragLockActive || !state->dragLockButtonSent || data->finger_count != 2) {
        return;
    }
    
    float sensMp = (float)state->mouseSensitivity / 128.0F;
    
    // Find the second finger (not the drag lock finger)
    int second_finger_idx = -1;
    for (int i = 0; i < 2; i++) {
        if (i != state->dragLockFingerID) {
            second_finger_idx = i;
            break;
        }
    }
    
    if (second_finger_idx == -1) {
        return;
    }
    
    // Mark that second finger is now providing movement
    if (!state->secondFingerMoving) {
        state->secondFingerMoving = true;
        // Reset drag lock accumulator when second finger starts moving
        state->dragLockAccumPos.x = 0;
        state->dragLockAccumPos.y = 0;
    }
    
    // Use relative movement from the trackpad for second finger movement
    // Since we can't easily separate individual finger movement, use the relative data
    if (data->rx != 0 || data->ry != 0) {
        state->dragLockAccumPos.x += data->rx * sensMp;
        state->dragLockAccumPos.y += data->ry * sensMp;

        int16_t xp = (int16_t)state->dragLockAccumPos.x;
        int16_t yp = (int16_t)state->dragLockAccumPos.y;

        if (fabsf(state->dragLockAccumPos.x) >= DRAG_LOCK_MOVEMENT_THRESHOLD || 
            fabsf(state->dragLockAccumPos.y) >= DRAG_LOCK_MOVEMENT_THRESHOLD) {
            
            send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
            send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);

            state->dragLockAccumPos.x -= xp;
            state->dragLockAccumPos.y -= yp;
        }
    }
}

void reset_drag_lock_state(struct gesture_state *state) {
    // Clear all drag lock state (button release handled by caller)
    state->dragLockActive = false;
    state->dragLockButtonSent = false;
    state->secondFingerMoving = false;
    state->dragLockStartTime = 0;
    state->dragLockStartX = 0;
    state->dragLockStartY = 0;
    state->dragLockFingerID = 0;
    state->dragLockAccumPos.x = 0;
    state->dragLockAccumPos.y = 0;
}

void reset_single_finger_state(struct gesture_state *state) {
    // Handle drag release (both normal drag and drag lock use the same button)
    if ((state->isDragging && state->dragStartSent) || (state->dragLockActive && state->dragLockButtonSent)) {
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
    }
    
    // Reset all drag-related state
    state->isDragging = false;
    state->dragStartSent = false;
    
    // Reset drag lock state
    reset_drag_lock_state(state);

    // Reset accumulated position
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }
}
