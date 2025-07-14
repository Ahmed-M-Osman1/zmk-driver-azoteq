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

LOG_MODULE_REGISTER(trackpad_main, CONFIG_ZMK_LOG_LEVEL);

static struct gesture_state g_gesture_state = {0};
static const struct device *trackpad;
static const struct device *trackpad_device = NULL;
static int event_count = 0;
static int64_t last_event_time = 0;
static int64_t last_config_check = 0;

// Enhanced input event sending with validation
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    event_count++;

    // Validate input values to prevent issues
    if (type == INPUT_EV_REL) {
        // Clamp relative movements to prevent jumps
        if (abs(value) > 200) {
            LOG_WRN("Clamping large movement: %d -> %d", value, (value > 0) ? 200 : -200);
            value = (value > 0) ? 200 : -200;
        }
    }

    // Log significant events for debugging
    if (type == INPUT_EV_KEY) {
        LOG_DBG("Button event: code=%d, value=%d", code, value);
    } else if (type == INPUT_EV_REL && abs(value) > 3) {
        LOG_DBG("Movement event: code=%d, value=%d", code, value);
    }

    if (trackpad_device) {
        int ret = input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
        if (ret < 0) {
            LOG_WRN("Failed to send input event: %d", ret);
        }
    } else {
        LOG_ERR("Trackpad device not available");
    }
}

// Enhanced configuration validation and recovery
static void validate_and_recover_config(const struct device *dev) {
    int64_t current_time = k_uptime_get();

    // Check configuration every 5 seconds
    if (current_time - last_config_check < 5000) {
        return;
    }
    last_config_check = current_time;

    const struct iqs5xx_config *config = dev->config;
    LOG_DBG("Config check: rot90=%d, rot270=%d, inv_x=%d, inv_y=%d, sens=%d",
            config->rotate_90, config->rotate_270, config->invert_x, config->invert_y, config->sensitivity);

    // If we suspect configuration issues (e.g., after wake from sleep), reinitialize
    if (g_gesture_state.mouseSensitivity != config->sensitivity) {
        LOG_INF("Sensitivity mismatch detected, updating: %d -> %d",
                g_gesture_state.mouseSensitivity, config->sensitivity);
        g_gesture_state.mouseSensitivity = config->sensitivity;

        // Force re-initialization of device registers
        struct iqs5xx_reg_config iqs5xx_registers = iqs5xx_reg_config_default();
        int ret = iqs5xx_registers_init(dev, &iqs5xx_registers);
        if (ret != 0) {
            LOG_ERR("Failed to re-initialize registers during config recovery: %d", ret);
        } else {
            LOG_INF("Successfully recovered device configuration");
        }
    }
}

// Data quality validation to filter out bad readings
static bool is_data_quality_good(const struct iqs5xx_rawdata *data) {
    // Check for system errors
    if (data->system_info0 & (ATI_ERROR | ALP_ATI_ERROR)) {
        LOG_WRN("ATI error detected in system_info0: 0x%02x", data->system_info0);
        return false;
    }

    if (data->system_info1 & TOO_MANY_FINGERS) {
        LOG_WRN("Too many fingers detected");
        return false;
    }

    // Validate finger data consistency
    if (data->finger_count > 0) {
        int valid_fingers = 0;
        for (int i = 0; i < data->finger_count && i < 5; i++) {
            if (data->fingers[i].strength > 0) {
                valid_fingers++;

                // Check for reasonable coordinate values
                if (data->fingers[i].ax > 32000 || data->fingers[i].ay > 32000) {
                    LOG_WRN("Finger %d has unreasonable coordinates: %d,%d",
                            i, data->fingers[i].ax, data->fingers[i].ay);
                    return false;
                }
            }
        }

        // Finger count should match valid fingers
        if (valid_fingers != data->finger_count) {
            LOG_WRN("Finger count mismatch: reported=%d, valid=%d", data->finger_count, valid_fingers);
            return false;
        }
    }

    return true;
}

// Enhanced trigger handler with improved error handling and recovery
static void trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    static int trigger_count = 0;
    static int bad_data_count = 0;
    int64_t current_time = k_uptime_get();

    trigger_count++;

    // Validate data quality first
    if (!is_data_quality_good(data)) {
        bad_data_count++;
        LOG_WRN("Bad data quality detected (count: %d)", bad_data_count);

        // If we have too much bad data, try recovery
        if (bad_data_count > 10) {
            LOG_ERR("Too much bad data, attempting device recovery");
            bad_data_count = 0;
            validate_and_recover_config(dev);
        }
        return;
    }

    // Reset bad data counter on good data
    bad_data_count = 0;

    // Periodic configuration validation
    validate_and_recover_config(dev);

    // Check for important events that should always be processed
    bool has_gesture = (data->gestures0 != 0) || (data->gestures1 != 0);
    bool finger_count_changed = (g_gesture_state.lastFingerCount != data->finger_count);

    // Rate limit ONLY movement events, NEVER gesture events
    if (!has_gesture && !finger_count_changed && (current_time - last_event_time < 15)) {
        return; // Skip only pure movement events
    }
    last_event_time = current_time;

    // Log important events
    if (finger_count_changed || has_gesture) {
        LOG_DBG("TRIGGER #%d: fingers=%d, g0=0x%02x, g1=0x%02x, rel=%d/%d",
                trigger_count, data->finger_count, data->gestures0, data->gestures1,
                data->rx, data->ry);
    }

    // Process gestures FIRST, before finger count logic
    if (has_gesture) {
        LOG_DBG("=== GESTURE DETECTED: g0=0x%02x, g1=0x%02x ===", data->gestures0, data->gestures1);

        // Handle single finger gestures (including taps that happen on finger lift)
        if (data->gestures0) {
            handle_single_finger_gestures(dev, data, &g_gesture_state);
        }

        // Handle two finger gestures
        if (data->gestures1) {
            handle_two_finger_gestures(dev, data, &g_gesture_state);
        }
    }

    // Handle finger count changes and movement
    switch (data->finger_count) {
        case 0:
            // === FINGER LIFT SESSION END ===
            if (finger_count_changed) {
                LOG_DBG("=== ALL FINGERS LIFTED ===");
            }

            // Reset all states when no fingers
            reset_single_finger_state(&g_gesture_state);
            reset_two_finger_state(&g_gesture_state);
            reset_three_finger_state(&g_gesture_state);
            break;

        case 1:
            // === SINGLE FINGER SESSION ===
            if (finger_count_changed) {
                LOG_DBG("=== SINGLE FINGER SESSION START ===");
                LOG_DBG("Initial: F0(%d,%d), strength=%d",
                        data->fingers[0].ax, data->fingers[0].ay, data->fingers[0].strength);
            }

            // Only reset others if they were active
            if (g_gesture_state.twoFingerActive) {
                LOG_DBG("Ending two finger session");
                reset_two_finger_state(&g_gesture_state);
            }
            if (g_gesture_state.threeFingersPressed) {
                LOG_DBG("Ending three finger session");
                reset_three_finger_state(&g_gesture_state);
            }

            // Handle single finger movement (but gestures were already handled above)
            if (!has_gesture) {
                handle_single_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 2:
            // === TWO FINGER SESSION ===
            if (finger_count_changed) {
                LOG_DBG("=== TWO FINGER SESSION START ===");
                LOG_DBG("Initial: F0(%d,%d) F1(%d,%d)",
                        data->fingers[0].ax, data->fingers[0].ay,
                        data->fingers[1].ax, data->fingers[1].ay);
            }

            // Only reset others if they were active
            if (g_gesture_state.isDragging) {
                LOG_DBG("Ending single finger drag");
                reset_single_finger_state(&g_gesture_state);
            }
            if (g_gesture_state.threeFingersPressed) {
                LOG_DBG("Ending three finger session");
                reset_three_finger_state(&g_gesture_state);
            }

            // Handle two finger gestures
            if (!has_gesture) {
                handle_two_finger_gestures(dev, data, &g_gesture_state);
            }
            break;

        case 3:
            // === THREE FINGER SESSION ===
            if (finger_count_changed) {
                LOG_DBG("=== THREE FINGER SESSION START ===");
                LOG_DBG("Initial: F0(%d,%d) F1(%d,%d) F2(%d,%d)",
                        data->fingers[0].ax, data->fingers[0].ay,
                        data->fingers[1].ax, data->fingers[1].ay,
                        data->fingers[2].ax, data->fingers[2].ay);
            }

            // Only reset others if they were active
            if (g_gesture_state.isDragging) {
                LOG_DBG("Ending single finger drag");
                reset_single_finger_state(&g_gesture_state);
            }
            if (g_gesture_state.twoFingerActive) {
                LOG_DBG("Ending two finger session");
                reset_two_finger_state(&g_gesture_state);
            }
            handle_three_finger_gestures(dev, data, &g_gesture_state);
            break;

        default:
            // 4+ fingers - reset all
            LOG_DBG("=== TOO MANY FINGERS (%d) - RESET ALL ===", data->finger_count);
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
    LOG_INF("Initializing trackpad handler");

    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }
    trackpad_device = trackpad;

    // Verify device is ready
    if (!device_is_ready(trackpad)) {
        LOG_ERR("IQS5XX device not ready");
        return -ENODEV;
    }

    // Get configuration for sensitivity
    const struct iqs5xx_config *config = trackpad->config;

    // Initialize the keyboard events system
    int ret = trackpad_keyboard_init(trackpad_device);
    if (ret < 0) {
        LOG_ERR("Failed to initialize keyboard events: %d", ret);
        return ret;
    }

    // Initialize gesture state with devicetree sensitivity
    memset(&g_gesture_state, 0, sizeof(g_gesture_state));
    g_gesture_state.mouseSensitivity = config->sensitivity;

    LOG_INF("Trackpad config: sensitivity=%d, rot90=%d, inv_x=%d, inv_y=%d",
            config->sensitivity, config->rotate_90, config->invert_x, config->invert_y);

    // Set up trigger handler
    int err = iqs5xx_trigger_set(trackpad, trackpad_trigger_handler);
    if(err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return -EINVAL;
    }

    LOG_INF("Trackpad initialization complete");
    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
