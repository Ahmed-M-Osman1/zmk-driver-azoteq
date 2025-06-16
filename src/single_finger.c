#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Enhanced single finger state tracking
struct single_finger_state {
    bool finger_down;
    bool tap_pending;
    bool drag_active;
    bool button_pressed;
    int64_t finger_down_time;
    int64_t last_tap_time;
    uint16_t start_x, start_y;
    uint16_t last_x, last_y;
    float movement_since_down;
} static sf_state = {0};

// Configuration constants
#define TAP_TIMEOUT_MS                      300   // Maximum time for a tap
#define DRAG_START_THRESHOLD                10    // Minimum movement to start drag
#define MIN_FINGER_STRENGTH                 1000  // Minimum strength for valid touch
#define DOUBLE_TAP_WINDOW_MS               500   // Time window for double tap

// Calculate distance between two points
static float calculate_distance(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    return sqrtf(dx * dx + dy * dy);
}

void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    bool hasGesture = false;
    int64_t current_time = k_uptime_get();

    // Check if we have a valid single finger touch
    if (data->finger_count != 1 || data->fingers[0].strength < MIN_FINGER_STRENGTH) {
        return;
    }

    // Initialize finger tracking if just started
    if (!sf_state.finger_down) {
        sf_state.finger_down = true;
        sf_state.finger_down_time = current_time;
        sf_state.start_x = sf_state.last_x = data->fingers[0].ax;
        sf_state.start_y = sf_state.last_y = data->fingers[0].ay;
        sf_state.movement_since_down = 0;
        sf_state.tap_pending = true;
        sf_state.drag_active = false;
        sf_state.button_pressed = false;

        LOG_DBG("Single finger down at (%d,%d), strength=%d",
                sf_state.start_x, sf_state.start_y, data->fingers[0].strength);
        return;
    }

    // Calculate movement since finger went down
    sf_state.movement_since_down = calculate_distance(
        sf_state.start_x, sf_state.start_y,
        data->fingers[0].ax, data->fingers[0].ay
    );

    // Handle hardware gestures FIRST (higher priority)
    if (data->gestures0 && !hasGesture) {
        LOG_DBG("Hardware gesture detected: 0x%02x, movement=%.1f",
                data->gestures0, (double)sf_state.movement_since_down);

        switch(data->gestures0) {
            case GESTURE_SINGLE_TAP:
                // Only handle single tap if we haven't moved much and aren't dragging
                if (!sf_state.drag_active && sf_state.movement_since_down < DRAG_START_THRESHOLD) {
                    hasGesture = true;
                    sf_state.tap_pending = false;

                    // Check for double-tap
                    if (current_time - sf_state.last_tap_time < DOUBLE_TAP_WINDOW_MS) {
                        LOG_INF("*** DOUBLE TAP -> DOUBLE CLICK ***");
                        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, false);
                        k_msleep(50); // Small delay between clicks
                        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                        sf_state.last_tap_time = 0; // Reset to prevent triple-tap
                    } else {
                        LOG_INF("*** SINGLE TAP -> LEFT CLICK ***");
                        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                        sf_state.last_tap_time = current_time;
                    }
                } else {
                    LOG_DBG("Ignoring single tap - already dragging or moved too much (%.1f px)",
                            (double)sf_state.movement_since_down);
                }
                break;

            case GESTURE_TAP_AND_HOLD:
                // Start drag operation
                if (!sf_state.drag_active && !sf_state.button_pressed) {
                    LOG_INF("*** TAP AND HOLD -> DRAG START ***");
                    send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
                    sf_state.drag_active = true;
                    sf_state.button_pressed = true;
                    sf_state.tap_pending = false;
                    hasGesture = true;
                } else {
                    LOG_DBG("Already dragging, ignoring repeated tap-and-hold");
                }
                break;

            default:
                LOG_DBG("Unknown single finger gesture: 0x%02x", data->gestures0);
                break;
        }
    }

    // Movement handling - works during normal movement AND during drag
    if (!hasGesture) {
        float sensMp = (float)state->mouseSensitivity / 128.0f;

        // Check if we should start drag based on movement (without hardware gesture)
        if (!sf_state.drag_active && sf_state.tap_pending &&
            sf_state.movement_since_down > DRAG_START_THRESHOLD &&
            current_time - sf_state.finger_down_time > 200) { // 200ms minimum hold time

            LOG_INF("*** MOVEMENT DRAG START (%.1f px movement) ***", (double)sf_state.movement_since_down);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
            sf_state.drag_active = true;
            sf_state.button_pressed = true;
            sf_state.tap_pending = false;
        }

        // Process relative movement if we have any
        if (data->rx != 0 || data->ry != 0) {
            // Accumulate movement with correct axis mapping
            state->accumPos.x += data->rx * sensMp;  // rx maps to X movement
            state->accumPos.y += data->ry * sensMp;  // ry maps to Y movement

            int16_t xp = (int16_t)state->accumPos.x;
            int16_t yp = (int16_t)state->accumPos.y;

            // Send movement if threshold exceeded
            if (fabsf(state->accumPos.x) >= MOVEMENT_THRESHOLD || fabsf(state->accumPos.y) >= MOVEMENT_THRESHOLD) {
                LOG_DBG("Mouse movement: rx=%d,ry=%d -> accum=%.2f,%.2f -> move=%d,%d (drag=%d)",
                        data->rx, data->ry, (double)state->accumPos.x, (double)state->accumPos.y,
                        xp, yp, sf_state.drag_active);

                // Send movement events (works both for normal movement and drag)
                send_input_event(INPUT_EV_REL, INPUT_REL_X, xp, false);
                send_input_event(INPUT_EV_REL, INPUT_REL_Y, yp, true);

                // Reset accumulation, keeping fractional part
                state->accumPos.x -= xp;
                state->accumPos.y -= yp;
            }
        }

        // Update last position for next iteration
        sf_state.last_x = data->fingers[0].ax;
        sf_state.last_y = data->fingers[0].ay;
    }

    // Handle tap timeout (if finger held too long without movement)
    if (sf_state.tap_pending && !sf_state.drag_active &&
        current_time - sf_state.finger_down_time > TAP_TIMEOUT_MS) {
        LOG_DBG("Tap timeout - canceling pending tap");
        sf_state.tap_pending = false;
    }

    // Update legacy state for compatibility
    state->isDragging = sf_state.drag_active;
    state->dragStartSent = sf_state.button_pressed;
}

void reset_single_finger_state(struct gesture_state *state) {
    // Handle end of drag operation
    if (sf_state.drag_active && sf_state.button_pressed) {
        LOG_INF("*** DRAG END - RELEASING BUTTON ***");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
    }

    // Handle pending tap that was never completed
    if (sf_state.tap_pending && !sf_state.drag_active &&
        sf_state.movement_since_down < DRAG_START_THRESHOLD) {
        int64_t hold_time = k_uptime_get() - sf_state.finger_down_time;
        if (hold_time < TAP_TIMEOUT_MS) {
            // Check for double-tap
            if (k_uptime_get() - sf_state.last_tap_time < DOUBLE_TAP_WINDOW_MS) {
                LOG_INF("*** DELAYED DOUBLE TAP -> DOUBLE CLICK ***");
                send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, false);
                k_msleep(50);
                send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                sf_state.last_tap_time = 0;
            } else {
                LOG_INF("*** DELAYED SINGLE TAP -> LEFT CLICK ***");
                send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, false);
                send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
                sf_state.last_tap_time = k_uptime_get();
            }
        }
    }

    // Reset accumulated position
    if (state->accumPos.x != 0 || state->accumPos.y != 0) {
        LOG_DBG("Resetting single finger accumulated position: was %.2f,%.2f",
                (double)state->accumPos.x, (double)state->accumPos.y);
        state->accumPos.x = 0;
        state->accumPos.y = 0;
    }

    // Clear single finger state
    if (sf_state.finger_down) {
        LOG_DBG("Single finger session end: drag=%d, tap_pending=%d, movement=%.1f",
                sf_state.drag_active, sf_state.tap_pending, (double)sf_state.movement_since_down);
        memset(&sf_state, 0, sizeof(sf_state));
    }

    // Clear legacy state
    state->isDragging = false;
    state->dragStartSent = false;
}
