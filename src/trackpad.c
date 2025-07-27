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

// SIMPLE FIX: Keep original gesture state but add basic thread safety
static struct gesture_state g_gesture_state = {0};
static const struct device *trackpad;
static const struct device *trackpad_device = NULL;
static int event_count = 0;
static int64_t last_event_time = 0; // For rate-limiting

// MINIMAL FIX: Add basic mutex for thread safety without breaking existing code
static struct k_mutex simple_state_mutex;

// MINIMAL IMPLEMENTATION: Basic thread-safe functions to satisfy the header
int init_gesture_state(struct gesture_state *state, uint8_t sensitivity) {
    if (!state) return -EINVAL;

    // Initialize the basic mutex for state protection
    k_mutex_init(&state->state_mutex);

    // Keep original initialization logic
    memset(state, 0, sizeof(struct gesture_state));
    k_mutex_init(&state->state_mutex);  // Reinit after memset

    state->mouseSensitivity = sensitivity;
    state->state_magic = GESTURE_STATE_MAGIC;
    state->last_update_time = k_uptime_get();
    state->state_initialized = true;

    LOG_INF("Basic gesture state initialized with sensitivity: %d", sensitivity);
    return 0;
}

bool validate_gesture_state(const struct gesture_state *state) {
    if (!state) return false;
    if (state->state_magic != GESTURE_STATE_MAGIC) return false;
    if (!state->state_initialized) return false;
    return true;
}

void cleanup_gesture_state(struct gesture_state *state) {
    if (state) {
        state->state_initialized = false;
        state->state_magic = 0;
    }
}

uint8_t get_current_finger_count(struct gesture_state *state) {
    if (!state) return 0;
    return state->lastFingerCount;
}

void set_current_finger_count(struct gesture_state *state, uint8_t count) {
    if (!state) return;
    state->lastFingerCount = count;
    state->last_update_time = k_uptime_get();
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

// MINIMAL FIX: Enhanced trigger handler with basic validation
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;
    int64_t current_time = k_uptime_get();

    trigger_count++;

    // BASIC thread safety - just use a simple mutex
    if (!k_mutex_lock(&simple_state_mutex, K_MSEC(10))) {
        return; // Skip if can't get mutex quickly
    }

    // PRESERVED ORIGINAL LOGIC: ALWAYS process gestures immediately, regardless of finger count
    bool has_gesture = (data->gestures0 != 0) || (data->gestures1 != 0);
    bool finger_count_changed = (g_gesture_state.lastFingerCount != data->finger_count);

    // Rate limit ONLY movement events, NEVER gesture events
    if (!has_gesture && !finger_count_changed && (current_time - last_event_time < 20)) {
        k_mutex_unlock(&simple_state_mutex);
        return; // Skip only movement-only events
    }
    last_event_time = current_time;

    // Log when finger count changes or gestures detected
    if (finger_count_changed || has_gesture) {
        LOG_INF("TRIGGER #%d: fingers=%d, g0=0x%02x, g1=0x%02x, rel=%d/%d",
                trigger_count, data->finger_count, data->gestures0, data->gestures1, data->rx, data->ry);
    }

    // Log coordinate transformation info on significant events
    if (has_gesture || finger_count_changed) {
        const struct iqs5xx_config *config = dev->config;
        if (config && (config->invert_x || config->invert_y || config->rotate_90 || config->rotate_270)) {
            LOG_DBG("Transform active: inv_x=%d, inv_y=%d, rot90=%d, rot270=%d",
                    config->invert_x, config->invert_y, config->rotate_90, config->rotate_270);
        }
    }

    // PRESERVED ORIGINAL LOGIC: Process gestures FIRST, before finger count logic
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

    // THEN handle finger count changes and movement
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

    // Update finger count when it changes
    if (g_gesture_state.lastFingerCount != data->finger_count) {
        g_gesture_state.lastFingerCount = data->finger_count;
    }

    k_mutex_unlock(&simple_state_mutex);
}

// MINIMAL FIX: Basic initialization that preserves all original functionality
static int trackpad_init(void) {
    LOG_INF("=== MINIMAL TRACKPAD INIT START ===");

    // Initialize simple mutex
    k_mutex_init(&simple_state_mutex);

    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }
    LOG_INF("Found IQS5XX device: %p", trackpad);

    // Get configuration to read coordinate transform settings
    const struct iqs5xx_config *config = trackpad->config;
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

    // MINIMAL FIX: Basic gesture state initialization
    ret = init_gesture_state(&g_gesture_state, config->sensitivity);
    if (ret < 0) {
        LOG_ERR("Failed to initialize gesture state: %d", ret);
        return ret;
    }

    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return -EINVAL;
    }

    LOG_INF("=== TRACKPAD INITIALIZATION COMPLETE ===");
    LOG_INF("Coordinate transformations: %s%s%s%s%s",
            config->invert_x ? "invert-x " : "",
            config->invert_y ? "invert-y " : "",
            config->rotate_90 ? "rotate-90 " : "",
            config->rotate_270 ? "rotate-270 " : "",
            (!config->invert_x && !config->invert_y && !config->rotate_90 && !config->rotate_270) ? "none" : "");

    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
