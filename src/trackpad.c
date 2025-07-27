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

// WORKING: Keep your exact original gesture state structure
static struct gesture_state g_gesture_state = {0};
static const struct device *trackpad;
static const struct device *trackpad_device = NULL;
static int event_count = 0;
static int64_t last_event_time = 0; // For rate-limiting

// WORKING: Optimized input event sending - this was working before
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    event_count++;
    // Log ALL events to see what's happening
    LOG_INF("SENDING EVENT #%d: type=%d, code=%d, val=%d, sync=%d", event_count, type, code, value, sync);

    if (trackpad_device) {
        int ret = input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
        if (ret < 0) {
            LOG_ERR("Input event failed: %d", ret);
        } else {
            LOG_INF("Input event sent successfully");
        }
    } else {
        LOG_ERR("No trackpad device available for input events");
    }
}

// WORKING: Your exact original trigger handler logic
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;
    int64_t current_time = k_uptime_get();

    trigger_count++;

    // ALWAYS process gestures immediately, regardless of finger count
    bool has_gesture = (data->gestures0 != 0) || (data->gestures1 != 0);
    bool finger_count_changed = (g_gesture_state.lastFingerCount != data->finger_count);

    // Rate limit ONLY movement events, NEVER gesture events
    if (!has_gesture && !finger_count_changed && (current_time - last_event_time < 20)) {
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

    // Process gestures FIRST, before finger count logic
    if (has_gesture) {
        LOG_INF("=== GESTURE DETECTED: g0=0x%02x, g1=0x%02x ===", data->gestures0, data->gestures1);

        // Handle single finger gestures (including taps that happen on finger lift)
        if (data->gestures0) {
            LOG_INF("Calling handle_single_finger_gestures for gesture 0x%02x", data->gestures0);
            handle_single_finger_gestures(dev, data, &g_gesture_state);
        }

        // Handle two finger gestures
        if (data->gestures1) {
            LOG_INF("Calling handle_two_finger_gestures for gesture 0x%02x", data->gestures1);
            handle_two_finger_gestures(dev, data, &g_gesture_state);
        }
    }

    // THEN handle finger count changes and movement
    switch (data->finger_count) {
        case 0:
            // Reset all states when no fingers
            LOG_DBG("No fingers - resetting all states");
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;

        case 1:
            // Only reset others if they were active
            if (g_gesture_state.twoFingerActive) {
                LOG_DBG("Switching from two finger to single finger");
                reset_two_finger_state(&g_gesture_state);
            }
            if (g_gesture_state.threeFingersPressed) {
                LOG_DBG("Switching from three finger to single finger");
                reset_three_finger_state(&g_gesture_state);
            }

            // Handle single finger movement (but gestures were already handled above)
            if (!has_gesture) {
                LOG_DBG("Processing single finger movement: rel=%d/%d", data->rx, data->ry);
                handle_single_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 2:
            // Only reset others if they were active
            if (g_gesture_state.isDragging) {
                LOG_DBG("Switching from single finger drag to two finger");
                reset_single_finger_state(&g_gesture_state);
            }
            if (g_gesture_state.threeFingersPressed) {
                LOG_DBG("Switching from three finger to two finger");
                reset_three_finger_state(&g_gesture_state);
            }

            // Handle two finger gestures (but hardware gestures were already handled above)
            if (!has_gesture) {
                LOG_DBG("Processing two finger movement");
                handle_two_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 3:
            // Only reset others if they were active
            if (g_gesture_state.isDragging) {
                LOG_DBG("Switching from single finger drag to three finger");
                reset_single_finger_state(&g_gesture_state);
            }
            if (g_gesture_state.twoFingerActive) {
                LOG_DBG("Switching from two finger to three finger");
                reset_two_finger_state(&g_gesture_state);
            }
            LOG_DBG("Processing three finger gestures");
            handle_three_finger_gestures(dev, data, &g_gesture_state);
            break;

        default:
            // 4+ fingers - reset all
            LOG_DBG("4+ fingers detected - resetting all states");
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;
    }

    // Update finger count when it changes
    if (g_gesture_state.lastFingerCount != data->finger_count) {
        LOG_DBG("Finger count changed from %d to %d", g_gesture_state.lastFingerCount, data->finger_count);
        g_gesture_state.lastFingerCount = data->finger_count;
    }
}

// WORKING: Your exact original initialization
static int trackpad_init(void) {
    LOG_INF("=== OPTIMIZED MODULAR TRACKPAD INIT START ===");

    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }
    LOG_INF("Found IQS5XX device: %p", trackpad);

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

    // Initialize gesture state with sensitivity from device tree
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
    g_gesture_state.mouseSensitivity = config->sensitivity;
    LOG_INF("Initialized gesture state with sensitivity: %d", g_gesture_state.mouseSensitivity);

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

    // CRITICAL: Test input event system
    LOG_INF("Testing input event system...");
    send_input_event(INPUT_EV_REL, INPUT_REL_X, 1, false);
    send_input_event(INPUT_EV_REL, INPUT_REL_Y, 1, true);
    LOG_INF("Input event test completed");

    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
