/**
 * @file trackpad.c
 * @brief Fixed trackpad gestures with working cursor movement
 */
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <math.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include "iqs5xx.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// ===== SIMPLIFIED CONSTANTS =====
#define THREE_FINGER_SWIPE_THRESHOLD        80
#define THREE_FINGER_TAP_TIMEOUT            500     // Longer timeout for stability
#define MOVEMENT_THRESHOLD                  2       // Very low threshold
#define FINGER_STRENGTH_THRESHOLD           10      // Much lower threshold
#define TRACKPAD_WIDTH                      1024
#define TRACKPAD_HEIGHT                     1024
#define FINGER_STABILITY_FRAMES             3       // Require stable count for N frames

// ===== GESTURE STATE =====
typedef enum {
    GESTURE_NONE,
    GESTURE_CURSOR_MOVE,
    GESTURE_THREE_FINGER_TAP,
    GESTURE_THREE_FINGER_SWIPE,
    GESTURE_TWO_FINGER_SCROLL
} gesture_state_t;

static gesture_state_t current_gesture = GESTURE_NONE;
static const struct device *trackpad_device = NULL;

// Movement tracking - simplified
static struct {
    float x, y;
} accumPos = {0, 0};

// Three-finger gesture tracking
static struct {
    bool active;
    uint16_t start_x, start_y;
    int64_t start_time;
    bool swipe_detected;
} three_finger = {0};

// Two-finger scroll tracking
static struct {
    int16_t last_x_report;
    bool active;
} two_finger_scroll = {0};

static uint8_t last_finger_count = 0;
static uint8_t stable_finger_count = 0;
static uint8_t finger_stability_counter = 0;
static uint8_t mouse_sensitivity = 128;

// ===== HELPER FUNCTIONS =====

static void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync) {
    if (trackpad_device) {
        input_report(trackpad_device, type, code, value, sync, K_NO_WAIT);
        LOG_DBG("Sent input: type=%d, code=%d, value=%d, sync=%d", type, code, value, sync);
    }
}

// ===== IMPROVED FINGER DETECTION WITH MINIMAL STABILITY =====
static uint8_t detect_finger_count_stable(const struct iqs5xx_rawdata *data) {
    uint8_t detected_count = 0;

    // First, trust the hardware count if it seems reasonable
    if (data->finger_count > 0 && data->finger_count <= 5) {
        detected_count = data->finger_count;
        LOG_DBG("Using hardware finger count: %d", detected_count);
    } else {
        // Secondary: count by strength
        for (int i = 0; i < 5; i++) {
            if (data->fingers[i].strength > FINGER_STRENGTH_THRESHOLD) {
                detected_count++;
            }
        }

        if (detected_count > 0) {
            LOG_DBG("Count by strength: %d", detected_count);
        } else {
            // Fallback: if there's movement, assume 1 finger
            uint32_t movement = abs(data->rx) + abs(data->ry);
            if (movement > MOVEMENT_THRESHOLD) {
                detected_count = 1;
                LOG_DBG("Movement detected: %d, assuming 1 finger", movement);
            }
        }
    }

    // Simple stability for 3+ fingers only (to prevent false drops)
    if (detected_count >= 3 || last_finger_count >= 3) {
        if (detected_count == last_finger_count) {
            finger_stability_counter++;
            if (finger_stability_counter >= FINGER_STABILITY_FRAMES) {
                stable_finger_count = detected_count;
            }
        } else {
            finger_stability_counter = 0;
            // Allow immediate drop to 0, but delay other changes
            if (detected_count == 0) {
                stable_finger_count = 0;
            }
        }
        return stable_finger_count;
    } else {
        // For 0, 1, 2 fingers, use immediate detection
        finger_stability_counter = 0;
        stable_finger_count = detected_count;
        return detected_count;
    }
}

// ===== GESTURE HANDLERS =====

static void handle_three_finger_start(const struct iqs5xx_rawdata *data) {
    three_finger.active = true;
    three_finger.start_time = k_uptime_get();
    three_finger.swipe_detected = false;
    three_finger.start_x = data->fingers[0].ax;
    three_finger.start_y = data->fingers[0].ay;
    current_gesture = GESTURE_THREE_FINGER_TAP;
    LOG_INF("=== 3-FINGER GESTURE STARTED ===");
}

static void handle_three_finger_movement(const struct iqs5xx_rawdata *data) {
    if (!three_finger.active || three_finger.swipe_detected) {
        return;
    }

    uint16_t current_x = data->fingers[0].ax;
    uint16_t current_y = data->fingers[0].ay;

    int16_t delta_x = current_x - three_finger.start_x;
    int16_t delta_y = current_y - three_finger.start_y;
    uint32_t total_movement = abs(delta_x) + abs(delta_y);

    if (total_movement > THREE_FINGER_SWIPE_THRESHOLD) {
        three_finger.swipe_detected = true;
        current_gesture = GESTURE_THREE_FINGER_SWIPE;

        if (abs(delta_y) > abs(delta_x)) {
            // Vertical swipe - Mission Control
            LOG_INF("=== 3-FINGER SWIPE VERTICAL - MISSION CONTROL ===");
            send_input_event(INPUT_EV_KEY, INPUT_KEY_F3, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_KEY_F3, 0, true);
        } else {
            // Horizontal swipe - Desktop switch
            if (delta_x > 0) {
                LOG_INF("=== 3-FINGER SWIPE RIGHT - DESKTOP SWITCH ===");
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 1, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_RIGHT, 1, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_RIGHT, 0, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 0, true);
            } else {
                LOG_INF("=== 3-FINGER SWIPE LEFT - DESKTOP SWITCH ===");
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 1, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFT, 1, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFT, 0, false);
                send_input_event(INPUT_EV_KEY, INPUT_KEY_LEFTCTRL, 0, true);
            }
        }
    }
}

static void handle_three_finger_end(void) {
    if (!three_finger.active) return;

    int64_t duration = k_uptime_get() - three_finger.start_time;

    if (!three_finger.swipe_detected && duration < THREE_FINGER_TAP_TIMEOUT) {
        LOG_INF("=== 3-FINGER TAP - MIDDLE CLICK ===");
        send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 1, true);
        send_input_event(INPUT_EV_KEY, INPUT_BTN_2, 0, true);
    }

    three_finger.active = false;
    three_finger.swipe_detected = false;
    current_gesture = GESTURE_NONE;
}

static void handle_cursor_movement(const struct iqs5xx_rawdata *data) {
    // Skip if in multi-finger gesture
    if (current_gesture == GESTURE_THREE_FINGER_TAP ||
        current_gesture == GESTURE_THREE_FINGER_SWIPE ||
        current_gesture == GESTURE_TWO_FINGER_SCROLL) {
        return;
    }

    // Check if movement is significant enough
    if (abs(data->rx) < MOVEMENT_THRESHOLD && abs(data->ry) < MOVEMENT_THRESHOLD) {
        return;
    }

    current_gesture = GESTURE_CURSOR_MOVE;

    // Apply sensitivity and coordinate transformation
    float sens_multiplier = (float)mouse_sensitivity / 128.0f;
    accumPos.x += -data->ry * sens_multiplier;  // Swap and invert Y for proper direction
    accumPos.y += data->rx * sens_multiplier;   // X becomes Y

    int16_t move_x = (int16_t)accumPos.x;
    int16_t move_y = (int16_t)accumPos.y;

    // Send movement if significant
    if (abs(move_x) >= 1 || abs(move_y) >= 1) {
        LOG_DBG("Cursor move: rx=%d, ry=%d -> mx=%d, my=%d", data->rx, data->ry, move_x, move_y);
        send_input_event(INPUT_EV_REL, INPUT_REL_X, move_x, false);
        send_input_event(INPUT_EV_REL, INPUT_REL_Y, move_y, true);

        // Subtract what we sent to avoid accumulation
        accumPos.x -= move_x;
        accumPos.y -= move_y;
    }
}

static void handle_two_finger_scroll(const struct iqs5xx_rawdata *data) {
    if (!two_finger_scroll.active) {
        two_finger_scroll.active = true;
        two_finger_scroll.last_x_report = 0;
        LOG_INF("=== TWO-FINGER SCROLL STARTED ===");
    }

    current_gesture = GESTURE_TWO_FINGER_SCROLL;

    // Use relative movement for scrolling with better scaling
    int8_t vertical_scroll = 0;
    int8_t horizontal_scroll = 0;

    // Vertical scroll (more sensitive)
    if (abs(data->ry) > 8) {
        vertical_scroll = -data->ry / 8;  // Scale down and invert
        if (vertical_scroll == 0 && data->ry != 0) {
            vertical_scroll = data->ry > 0 ? -1 : 1;  // Ensure minimum movement
        }
    }

    // Horizontal scroll (accumulate for smoother experience)
    two_finger_scroll.last_x_report += data->rx;
    if (abs(two_finger_scroll.last_x_report) > 20) {
        horizontal_scroll = two_finger_scroll.last_x_report > 0 ? 1 : -1;
        two_finger_scroll.last_x_report = 0;
    }

    // Send scroll events
    if (vertical_scroll != 0) {
        LOG_DBG("Vertical scroll: %d (ry=%d)", vertical_scroll, data->ry);
        send_input_event(INPUT_EV_REL, INPUT_REL_WHEEL, vertical_scroll, true);
    }
    if (horizontal_scroll != 0) {
        LOG_DBG("Horizontal scroll: %d", horizontal_scroll);
        send_input_event(INPUT_EV_REL, INPUT_REL_HWHEEL, horizontal_scroll, true);
    }
}

// ===== MAIN TRIGGER HANDLER =====
static void enhanced_trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    uint8_t finger_count = detect_finger_count_stable(data);
    bool finger_count_changed = (finger_count != last_finger_count);

    // Log important events
    if (finger_count_changed) {
        LOG_INF("*** FINGER COUNT: %d -> %d (stable=%d, counter=%d) ***",
                last_finger_count, finger_count, stable_finger_count, finger_stability_counter);
    }

    // Log movement for debugging
    if (abs(data->rx) > 2 || abs(data->ry) > 2) {
        LOG_DBG("Movement: rx=%d, ry=%d, fingers=%d, gestures0=0x%02x, gestures1=0x%02x",
                data->rx, data->ry, finger_count, data->gestures0, data->gestures1);
    }

    // ===== GESTURE HANDLING BY FINGER COUNT =====

    if (finger_count >= 3) {
        // Three+ finger gestures
        if (!three_finger.active) {
            handle_three_finger_start(data);
        } else {
            handle_three_finger_movement(data);
        }
        // Reset two-finger scroll
        two_finger_scroll.active = false;
    } else if (three_finger.active && finger_count < 3) {
        // End three-finger gesture
        handle_three_finger_end();
    } else if (finger_count == 2) {
        // Two-finger scroll - check for scroll gesture OR significant movement
        if ((data->gestures1 & GESTURE_SCROLLG) ||
            (abs(data->rx) > 5 || abs(data->ry) > 5)) {
            handle_two_finger_scroll(data);
        }
    } else if (finger_count == 1) {
        // Single finger - cursor movement
        // Reset two-finger scroll
        if (two_finger_scroll.active) {
            two_finger_scroll.active = false;
            LOG_INF("=== TWO-FINGER SCROLL ENDED ===");
        }
        handle_cursor_movement(data);
    } else if (finger_count == 0) {
        // No fingers - reset everything
        if (current_gesture == GESTURE_CURSOR_MOVE) {
            accumPos.x = 0;
            accumPos.y = 0;
        }
        if (two_finger_scroll.active) {
            two_finger_scroll.active = false;
            LOG_INF("=== TWO-FINGER SCROLL ENDED ===");
        }
        current_gesture = GESTURE_NONE;
    }

    // ===== HANDLE BUILT-IN TAP GESTURES =====
    // Only process taps if no complex gesture is active
    if (current_gesture == GESTURE_NONE || current_gesture == GESTURE_CURSOR_MOVE) {
        if (data->gestures0 & GESTURE_SINGLE_TAP) {
            LOG_INF("Single tap -> Left click");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
        }

        if (data->gestures1 & GESTURE_TWO_FINGER_TAP) {
            LOG_INF("Two-finger tap -> Right click");
            send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 1, true);
            send_input_event(INPUT_EV_KEY, INPUT_BTN_1, 0, true);
        }
    }

    last_finger_count = finger_count;
}

// ===== INITIALIZATION =====
static int enhanced_trackpad_init(void) {
    const struct device *trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }

    trackpad_device = trackpad;

    LOG_INF("Enhanced trackpad gestures initialized");
    LOG_INF("Mouse sensitivity: %d", mouse_sensitivity);
    LOG_INF("Movement threshold: %d", MOVEMENT_THRESHOLD);

    int err = iqs5xx_trigger_set(trackpad, enhanced_trackpad_trigger_handler);
    if (err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return -EINVAL;
    }

    return 0;
}

SYS_INIT(enhanced_trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
