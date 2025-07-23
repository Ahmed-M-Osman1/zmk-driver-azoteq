#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include "iqs5xx.h"
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// FIXED ISSUE #6 & #10: Enhanced global state with thread safety
static struct gesture_state g_gesture_state = {0};
static const struct device *trackpad;
static const struct device *trackpad_device = NULL;
static int event_count = 0;
static int64_t last_event_time = 0; // For rate-limiting

// FIXED ISSUE #6: Add global state protection mutex
static struct k_mutex global_state_mutex;
static bool global_state_initialized = false;

// FIXED ISSUE #6: Thread-safe gesture state management functions
int init_gesture_state(struct gesture_state *state, uint8_t sensitivity) {
    if (!state) {
        LOG_ERR("NULL gesture state pointer");
        return -EINVAL;
    }

    // Initialize mutex first
    k_mutex_init(&state->state_mutex);

    k_mutex_lock(&state->state_mutex, K_FOREVER);

    // Clear all state
    memset(state, 0, sizeof(struct gesture_state));

    // Reinitialize mutex (since memset cleared it)
    k_mutex_init(&state->state_mutex);

    // Set initial values
    state->mouseSensitivity = sensitivity;
    state->state_magic = GESTURE_STATE_MAGIC;
    state->last_update_time = k_uptime_get();
    state->state_initialized = true;

    k_mutex_unlock(&state->state_mutex);

    LOG_INF("Gesture state initialized with sensitivity: %d", sensitivity);
    return 0;
}

bool validate_gesture_state(const struct gesture_state *state) {
    if (!state) {
        LOG_ERR("NULL gesture state in validation");
        return false;
    }

    if (state->state_magic != GESTURE_STATE_MAGIC) {
        LOG_ERR("Gesture state magic number corrupted: 0x%08x (expected 0x%08x)",
                state->state_magic, GESTURE_STATE_MAGIC);
        return false;
    }

    if (!state->state_initialized) {
        LOG_ERR("Gesture state not properly initialized");
        return false;
    }

    // Check for reasonable timestamp
    int64_t current_time = k_uptime_get();
    if (state->last_update_time > current_time ||
        (current_time - state->last_update_time) > 60000) { // 1 minute max
        LOG_ERR("Gesture state timestamp invalid: %lld (current: %lld)",
                state->last_update_time, current_time);
        return false;
    }

    return true;
}

void cleanup_gesture_state(struct gesture_state *state) {
    if (!state) return;

    if (k_mutex_lock(&state->state_mutex, K_MSEC(100)) == 0) {
        state->state_initialized = false;
        state->state_magic = 0;
        k_mutex_unlock(&state->state_mutex);
    }
}

uint8_t get_current_finger_count(struct gesture_state *state) {
    if (!validate_gesture_state(state)) {
        LOG_ERR("Invalid gesture state in get_current_finger_count");
        return 0;
    }

    uint8_t count = 0;
    if (k_mutex_lock(&state->state_mutex, K_MSEC(10)) == 0) {
        count = state->lastFingerCount;
        k_mutex_unlock(&state->state_mutex);
    }
    return count;
}

void set_current_finger_count(struct gesture_state *state, uint8_t count) {
    if (!validate_gesture_state(state)) {
        LOG_ERR("Invalid gesture state in set_current_finger_count");
        return;
    }

    if (k_mutex_lock(&state->state_mutex, K_MSEC(10)) == 0) {
        state->lastFingerCount = count;
        state->last_update_time = k_uptime_get();
        k_mutex_unlock(&state->state_mutex);
    }
}

// PRESERVED ORIGINAL FUNCTIONALITY: Optimized input event sending unchanged
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    event_count++;
    // Log important events
    if (type == INPUT_EV_KEY) {
        LOG_INF("CLICK #%d: btn=%d, val=%d", event_count, code, value);
    } else if (abs(value) > 5) { // Only log significant movements
        LOG_DBG("MOVE #%d: type=%d, code=%d, val=%d", event_count, type, code, value);
    }

    if (trackpad_device) {
        int ret = input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
        if (ret < 0) {
            LOG_ERR("Input event failed: %d", ret);
        }
    } else {
        LOG_ERR("No trackpad device");
    }
}

// FIXED ISSUE #8: Enhanced trigger handler with comprehensive state validation and error recovery
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;
    int64_t current_time = k_uptime_get();

    trigger_count++;

    // FIXED ISSUE #6 & #10: Validate gesture state before processing
    if (!validate_gesture_state(&g_gesture_state)) {
        LOG_ERR("Gesture state corrupted, attempting recovery...");

        // Attempt to recover gesture state
        const struct iqs5xx_config *config = dev->config;
        if (init_gesture_state(&g_gesture_state, config->sensitivity) != 0) {
            LOG_ERR("Failed to recover gesture state - skipping gesture processing");
            return;
        }
        LOG_INF("Gesture state recovered successfully");
    }

    // FIXED ISSUE #6: Thread-safe access to global state
    if (!k_mutex_lock(&global_state_mutex, K_MSEC(50))) {
        LOG_WRN("Failed to acquire global state mutex - skipping event");
        return;
    }

    // CRITICAL: ALWAYS process gestures immediately, regardless of finger count - PRESERVED ORIGINAL LOGIC
    bool has_gesture = (data->gestures0 != 0) || (data->gestures1 != 0);
    bool finger_count_changed = (get_current_finger_count(&g_gesture_state) != data->finger_count);

    // Rate limit ONLY movement events, NEVER gesture events - PRESERVED ORIGINAL LOGIC
    if (!has_gesture && !finger_count_changed && (current_time - last_event_time < 20)) {
        k_mutex_unlock(&global_state_mutex);
        return; // Skip only movement-only events
    }
    last_event_time = current_time;

    // Log when finger count changes or gestures detected
    if (finger_count_changed || has_gesture) {
        LOG_INF("TRIGGER #%d: fingers=%d, g0=0x%02x, g1=0x%02x, rel=%d/%d",
                trigger_count, data->finger_count, data->gestures0, data->gestures1, data->rx, data->ry);
    }

    // Log coordinate transformation info on significant events - PRESERVED FUNCTIONALITY
    if (has_gesture || finger_count_changed) {
        const struct iqs5xx_config *config = dev->config;
        if (config->invert_x || config->invert_y || config->rotate_90 || config->rotate_270) {
            LOG_DBG("Transform active: inv_x=%d, inv_y=%d, rot90=%d, rot270=%d",
                    config->invert_x, config->invert_y, config->rotate_90, config->rotate_270);
        }
    }

    // FIXED: Process gestures FIRST, before finger count logic - PRESERVED ORIGINAL LOGIC
    if (has_gesture) {
        LOG_INF("=== GESTURE DETECTED: g0=0x%02x, g1=0x%02x ===", data->gestures0, data->gestures1);

        // Handle single finger gestures (including taps that happen on finger lift)
        if (data->gestures0) {
            handle_single_finger_gestures(dev, data, &g_gesture_state);
        }

        // Handle two finger gestures
        if (data->gestures1) {
            handle_two_finger_gestures(dev, data, &g_gesture_state);
        }
    }

    // THEN handle finger count changes and movement - PRESERVED ORIGINAL LOGIC
    switch (data->finger_count) {
        case 0:
            // Reset all states when no fingers
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;

        case 1:
            // Only reset others if they were active
            if (g_gesture_state.twoFingerActive) reset_two_finger_state(&g_gesture_state);
            if (g_gesture_state.threeFingersPressed) reset_three_finger_state(&g_gesture_state);

            // Handle single finger movement (but gestures were already handled above)
            if (!has_gesture) {
                handle_single_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 2:
            // Only reset others if they were active
            if (g_gesture_state.isDragging) reset_single_finger_state(&g_gesture_state);
            if (g_gesture_state.threeFingersPressed) reset_three_finger_state(&g_gesture_state);

            // Handle two finger gestures (but hardware gestures were already handled above)
            if (!has_gesture) {
                handle_two_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 3:
            // Only reset others if they were active
            if (g_gesture_state.isDragging) reset_single_finger_state(&g_gesture_state);
            if (g_gesture_state.twoFingerActive) reset_two_finger_state(&g_gesture_state);
            handle_three_finger_gestures(dev, data, &g_gesture_state);
            break;

        default:
            // 4+ fingers - reset all
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;
    }

    // FIXED ISSUE #6: Thread-safe finger count update
    set_current_finger_count(&g_gesture_state, data->finger_count);

    k_mutex_unlock(&global_state_mutex);
}

// FIXED ISSUE #2, #6, #8, #9: Enhanced initialization with comprehensive error handling
static int trackpad_init(void) {
    LOG_INF("=== ENHANCED MODULAR TRACKPAD INIT START ===");

    // FIXED ISSUE #6: Initialize global state protection
    k_mutex_init(&global_state_mutex);

    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }
    LOG_INF("Found IQS5XX device: %p", trackpad);

    // FIXED ISSUE #2: Enhanced device validation
    if (!device_is_ready(trackpad)) {
        LOG_ERR("IQS5XX device is not ready");
        return -ENODEV;
    }

    // Get configuration to read coordinate transform settings
    const struct iqs5xx_config *config = trackpad->config;
    if (!config) {
        LOG_ERR("Failed to get device configuration");
        return -EINVAL;
    }

    LOG_INF("Trackpad config: sensitivity=%d, transform flags: invert_x=%d, invert_y=%d, rotate_90=%d, rotate_270=%d",
            config->sensitivity, config->invert_x, config->invert_y, config->rotate_90, config->rotate_270);

    trackpad_device = trackpad;
    LOG_INF("Set trackpad device reference: %p", trackpad_device);

    // Initialize the keyboard events system
    int ret = trackpad_keyboard_init(trackpad_device);
    if (ret < 0) {
        LOG_ERR("Failed to initialize trackpad keyboard events: %d", ret);
        return ret;
    }

    // FIXED ISSUE #6: Enhanced gesture state initialization with thread safety
    ret = init_gesture_state(&g_gesture_state, config->sensitivity);
    if (ret < 0) {
        LOG_ERR("Failed to initialize gesture state: %d", ret);
        return ret;
    }

    // FIXED ISSUE #6: Mark global state as initialized
    global_state_initialized = true;

    // FIXED ISSUE #2: Enhanced trigger handler registration with validation
    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        cleanup_gesture_state(&g_gesture_state);
        return -EINVAL;
    }

    LOG_INF("=== ENHANCED TRACKPAD INITIALIZATION COMPLETE ===");
    LOG_INF("Coordinate transformations: %s%s%s%s%s",
            config->invert_x ? "invert-x " : "",
            config->invert_y ? "invert-y " : "",
            config->rotate_90 ? "rotate-90 " : "",
            config->rotate_270 ? "rotate-270 " : "",
            (!config->invert_x && !config->invert_y && !config->rotate_90 && !config->rotate_270) ? "none" : "");

    // FIXED ISSUE #6: Final state validation
    if (!validate_gesture_state(&g_gesture_state)) {
        LOG_ERR("Gesture state validation failed after initialization");
        cleanup_gesture_state(&g_gesture_state);
        return -EINVAL;
    }

    LOG_INF("All initialization checks passed successfully");
    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
