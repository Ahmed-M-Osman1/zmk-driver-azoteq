// src/trackpad.c - FIXED version to handle gestures on finger lift
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include "iqs5xx.h"
#include "gesture_handlers.h"
#include "trackpad_keyboard_events.h"


static struct gesture_state g_gesture_state = {0};
static const struct device *trackpad;
static const struct device *trackpad_device = NULL;
static int event_count = 0;
static int64_t last_event_time = 0; // For rate-limiting
static int64_t last_activity_time = 0; // For idle detection
static bool is_idle_mode = false;

// Edge continuation work queue
static void edge_continuation_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(edge_continuation_work, edge_continuation_work_handler);

static void edge_continuation_work_handler(struct k_work *work) {
    if (g_gesture_state.edgeContinuationActive) {
        update_edge_continuation(&g_gesture_state);
        // Schedule next update
        k_work_reschedule(&edge_continuation_work, K_MSEC(CONTINUATION_UPDATE_INTERVAL_MS));
    }
}

// Optimized input event sending
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    event_count++;
    
    // Reset event counter periodically to prevent overflow
    if (event_count > 1000000) {
        event_count = 0;
    }
    
    // Update activity time for any significant event
    if (type == INPUT_EV_KEY || abs(value) > 2) {
        last_activity_time = k_uptime_get();
        is_idle_mode = false;
    }
    
    // Log important events
    if (type == INPUT_EV_KEY) {
        // Button press/release
    } else if (abs(value) > 5) { // Only log significant movements
        // Mouse movement
    }

    if (trackpad_device) {
        int ret = input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
        if (ret < 0) {
            return;
        }
    } else {
        return;
    }
}

// FIXED: Handle gestures even when finger_count == 0
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;
    int64_t current_time = k_uptime_get();

    trigger_count++;

    // CRITICAL: ALWAYS process gestures immediately, regardless of finger count
    bool has_gesture = (data->gestures0 != 0) || (data->gestures1 != 0);
    bool finger_count_changed = (g_gesture_state.lastFingerCount != data->finger_count);
    bool has_activity = has_gesture || finger_count_changed || (data->finger_count > 0);

    // Check for idle state transition (5 seconds of inactivity)
    if (!has_activity && !is_idle_mode && (current_time - last_activity_time > 5000)) {
        is_idle_mode = true;
        // Could add device power state change here if supported
    }
    
    // If we have activity and were idle, wake up
    if (has_activity && is_idle_mode) {
        is_idle_mode = false;
        last_activity_time = current_time;
    }
    
    // In idle mode, reduce processing frequency significantly
    if (is_idle_mode && (current_time - last_event_time < 100)) {
        return; // Skip processing in idle mode unless 100ms passed
    }

    // Rate limit ONLY movement events, NEVER gesture events
    if (!has_gesture && !finger_count_changed && (current_time - last_event_time < 20)) {
        return; // Skip only movement-only events
    }
    // Only update last_event_time for non-gesture events to avoid blocking subsequent gestures
    if (!has_gesture) {
        last_event_time = current_time;
    }


    // FIXED: Process gestures FIRST, before finger count logic
    if (has_gesture) {

        // Handle single finger gestures - but avoid conflicts with multi-finger operations
        if (data->gestures0) {
            // Only process single finger gestures if:
            // 1. No multi-finger operations are active, OR
            // 2. This is a finger-lift gesture (finger_count == 0) from a single-finger session
            bool can_process_single = !g_gesture_state.twoFingerActive && !g_gesture_state.threeFingersPressed;
            if (can_process_single) {
                handle_single_finger_gestures(dev, data, &g_gesture_state);
            }
        }

        // Handle two finger gestures
        if (data->gestures1) {
            handle_two_finger_gestures(dev, data, &g_gesture_state);
        }
        
        // After processing gestures, don't immediately reset states to avoid conflicts
        // State resets will happen in the finger count logic below if needed
    }

    // THEN handle finger count changes and movement
    switch (data->finger_count) {
        case 0:
            // Reset all states when no fingers detected
            // This should happen AFTER gestures are processed to avoid conflicts
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;

        case 1:
            // Only reset others if they were active
            if (g_gesture_state.twoFingerActive) reset_two_finger_state(&g_gesture_state);
            if (g_gesture_state.threeFingersPressed) reset_three_finger_state(&g_gesture_state);

            // Handle single finger movement (but skip if gesture was already handled above)
            // This prevents double-processing of gestures
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
}

static int trackpad_init(void) {
    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        return -EINVAL;
    }
    trackpad_device = trackpad;

    // Get configuration for sensitivity
    const struct iqs5xx_config *config = trackpad->config;

    // Initialize the keyboard events system
    int ret = trackpad_keyboard_init(trackpad_device);
    if (ret < 0) {
        return ret;
    }

    // Initialize gesture state with devicetree sensitivity
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
    g_gesture_state.mouseSensitivity = config->sensitivity;
    
    // Initialize activity tracking
    last_activity_time = k_uptime_get();
    is_idle_mode = false;

    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        return -EINVAL;
    }

    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
