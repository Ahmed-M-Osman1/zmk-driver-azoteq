/**
 * @file trackpad.c
 * @brief Enhanced ZMK-integrated trackpad gestures with proper 3-finger detection
 */
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <math.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include "iqs5xx.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// ===== TIMING CONSTANTS =====
#define TRACKPAD_LEFTCLICK_RELEASE_TIME     50
#define TRACKPAD_RIGHTCLICK_RELEASE_TIME    50
#define TRACKPAD_MIDDLECLICK_RELEASE_TIME   50
#define TRACKPAD_THREE_FINGER_CLICK_TIME    300
#define SCROLL_REPORT_DISTANCE              35
#define THREE_FINGER_SWIPE_THRESHOLD        80

// ===== GESTURE STATE =====
typedef enum {
    GESTURE_NONE,
    GESTURE_CURSOR_MOVE,
    GESTURE_THREE_FINGER_TAP,
    GESTURE_THREE_FINGER_SWIPE,
    GESTURE_TWO_FINGER_SCROLL
} gesture_state_t;

static gesture_state_t current_gesture = GESTURE_NONE;
static const struct device *trackpad = NULL;

// ===== STATE TRACKING =====
static bool inputEventActive = false;
static bool isHolding = false;
static uint8_t lastFingerCount = 0;
static uint8_t mouseSensitivity = 128;

// Three-finger gesture tracking
static struct {
    bool active;
    uint16_t start_x, start_y;
    int64_t start_time;
    bool swipe_detected;
} three_finger = {0};

// Scroll tracking
static int16_t lastXScrollReport = 0;

// Movement accumulation
static struct {
    float x, y;
} accumPos = {0, 0};

// ===== CLICK TIMERS =====
static void trackpad_leftclick_release(struct k_timer *timer) {
    zmk_hid_mouse_button_release(0);
    zmk_endpoints_send_mouse_report();
    inputEventActive = false;
}
K_TIMER_DEFINE(leftclick_release_timer, trackpad_leftclick_release, NULL);

static void trackpad_rightclick_release(struct k_timer *timer) {
    zmk_hid_mouse_button_release(1);
    zmk_endpoints_send_mouse_report();
    inputEventActive = false;
}
K_TIMER_DEFINE(rightclick_release_timer, trackpad_rightclick_release, NULL);

static void trackpad_middleclick_release(struct k_timer *timer) {
    zmk_hid_mouse_button_release(2);
    zmk_endpoints_send_mouse_report();
    inputEventActive = false;
}
K_TIMER_DEFINE(middleclick_release_timer, trackpad_middleclick_release, NULL);

// ===== CLICK FUNCTIONS =====
static inline void trackpad_leftclick(void) {
    if (isHolding) {
        zmk_hid_mouse_button_release(0);
        isHolding = false;
        inputEventActive = false;
    } else {
        if (inputEventActive) return;

        zmk_hid_mouse_button_press(0);
        k_timer_start(&leftclick_release_timer, K_MSEC(TRACKPAD_LEFTCLICK_RELEASE_TIME), K_NO_WAIT);
        inputEventActive = true;
    }
}

static inline void trackpad_rightclick(void) {
    if (inputEventActive) return;

    zmk_hid_mouse_button_press(1);
    k_timer_start(&rightclick_release_timer, K_MSEC(TRACKPAD_RIGHTCLICK_RELEASE_TIME), K_NO_WAIT);
    inputEventActive = true;
}

static inline void trackpad_middleclick(void) {
    if (inputEventActive) return;

    LOG_INF("=== MIDDLE CLICK ===");
    zmk_hid_mouse_button_press(2);
    k_timer_start(&middleclick_release_timer, K_MSEC(TRACKPAD_MIDDLECLICK_RELEASE_TIME), K_NO_WAIT);
    inputEventActive = true;
}

static inline void trackpad_tap_and_hold(bool enable) {
    if (!isHolding && enable) {
        zmk_hid_mouse_button_press(0);
        isHolding = true;
    }
}

// ===== THREE-FINGER GESTURE HANDLERS =====
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
            // Send F3 key
            zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_F3);
            zmk_endpoints_send_keyboard_report();
            k_msleep(10);
            zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_F3);
            zmk_endpoints_send_keyboard_report();
        } else {
            // Horizontal swipe - Desktop switch
            if (delta_x > 0) {
                LOG_INF("=== 3-FINGER SWIPE RIGHT - DESKTOP SWITCH ===");
                zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_LEFT_CONTROL);
                zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_RIGHT_ARROW);
                zmk_endpoints_send_keyboard_report();
                k_msleep(10);
                zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_RIGHT_ARROW);
                zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_LEFT_CONTROL);
                zmk_endpoints_send_keyboard_report();
            } else {
                LOG_INF("=== 3-FINGER SWIPE LEFT - DESKTOP SWITCH ===");
                zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_LEFT_CONTROL);
                zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_LEFT_ARROW);
                zmk_endpoints_send_keyboard_report();
                k_msleep(10);
                zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_LEFT_ARROW);
                zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_LEFT_CONTROL);
                zmk_endpoints_send_keyboard_report();
            }
        }
    }
}

static void handle_three_finger_end(void) {
    if (!three_finger.active) return;

    int64_t duration = k_uptime_get() - three_finger.start_time;

    if (!three_finger.swipe_detected && duration < TRACKPAD_THREE_FINGER_CLICK_TIME) {
        LOG_INF("=== 3-FINGER TAP - MIDDLE CLICK ===");
        trackpad_middleclick();
    }

    three_finger.active = false;
    three_finger.swipe_detected = false;
    current_gesture = GESTURE_NONE;
}

// ===== MAIN TRIGGER HANDLER =====
static void enhanced_trackpad_trigger_handler(const struct device *dev, const struct iqs5xx_rawdata *data) {
    bool hasGesture = false;
    bool inputMoved = false;
    uint8_t finger_count = data->finger_count;

    // Log finger count changes
    if (finger_count != lastFingerCount) {
        LOG_INF("*** FINGER COUNT: %d -> %d ***", lastFingerCount, finger_count);
    }

    // ===== THREE-FINGER GESTURE HANDLING =====
    if (finger_count >= 3) {
        if (!three_finger.active) {
            handle_three_finger_start(data);
        } else {
            handle_three_finger_movement(data);
        }
        hasGesture = true;
    } else if (three_finger.active) {
        handle_three_finger_end();
    }

    // ===== FINGER LIFT HANDLING =====
    if (finger_count == 0) {
        accumPos.x = 0;
        accumPos.y = 0;
        // Clear any movement
        zmk_hid_mouse_movement_set(0, 0);
    }

    // ===== TWO-FINGER SCROLL RESET =====
    if (finger_count != 2) {
        zmk_hid_mouse_scroll_set(0, 0);
    }

    // ===== BUILT-IN GESTURE HANDLING =====
    if ((data->gestures0 || data->gestures1) && !hasGesture && !three_finger.active) {
        // Two-finger gestures
        switch (data->gestures1) {
            case GESTURE_TWO_FINGER_TAP:
                hasGesture = true;
                LOG_INF("Two-finger tap -> Right click");
                trackpad_rightclick();
                zmk_hid_mouse_movement_set(0, 0);
                break;

            case GESTURE_SCROLLG:
                hasGesture = true;
                current_gesture = GESTURE_TWO_FINGER_SCROLL;
                lastXScrollReport += data->rx;

                // Vertical scroll (immediate)
                int8_t vertical_scroll = -data->ry / 8;
                if (vertical_scroll == 0 && data->ry != 0) {
                    vertical_scroll = data->ry > 0 ? -1 : 1;
                }

                // Horizontal scroll (accumulated)
                int8_t horizontal_scroll = 0;
                if (abs(lastXScrollReport) >= SCROLL_REPORT_DISTANCE) {
                    horizontal_scroll = lastXScrollReport >= 0 ? 1 : -1;
                    lastXScrollReport = 0;
                }

                if (vertical_scroll != 0 || horizontal_scroll != 0) {
                    LOG_DBG("Scroll: v=%d, h=%d (ry=%d, rx_accum=%d)",
                            vertical_scroll, horizontal_scroll, data->ry, lastXScrollReport);
                    zmk_hid_mouse_scroll_set(vertical_scroll, horizontal_scroll);
                }
                zmk_hid_mouse_movement_set(0, 0);
                break;
        }

        // Single-finger gestures
        switch (data->gestures0) {
            case GESTURE_SINGLE_TAP:
                hasGesture = true;
                LOG_INF("Single tap -> Left click");
                trackpad_leftclick();
                zmk_hid_mouse_movement_set(0, 0);
                break;

            case GESTURE_TAP_AND_HOLD:
                hasGesture = true;
                trackpad_tap_and_hold(true);
                zmk_hid_mouse_movement_set(0, 0);
                isHolding = true;
                break;
        }
    }

    // ===== CURSOR MOVEMENT =====
    if (!hasGesture && !three_finger.active && finger_count == 1) {
        current_gesture = GESTURE_CURSOR_MOVE;

        float sens_multiplier = (float)mouseSensitivity / 128.0f;
        accumPos.x += -data->ry * sens_multiplier;
        accumPos.y += data->rx * sens_multiplier;

        int16_t move_x = (int16_t)accumPos.x;
        int16_t move_y = (int16_t)accumPos.y;

        bool should_update = false;
        if (fabsf(accumPos.x) >= 1) {
            should_update = true;
            accumPos.x = 0;
        }
        if (fabsf(accumPos.y) >= 1) {
            should_update = true;
            accumPos.y = 0;
        }

        if (should_update) {
            zmk_hid_mouse_movement_set(move_x, move_y);
            inputMoved = true;
            LOG_DBG("Cursor move: rx=%d, ry=%d -> mx=%d, my=%d", data->rx, data->ry, move_x, move_y);
        }
    }

    lastFingerCount = finger_count;

    // ===== SEND REPORT =====
    if (inputMoved || hasGesture) {
        zmk_endpoints_send_mouse_report();
    }
}

// ===== INITIALIZATION =====
static int enhanced_trackpad_init(const struct device *_arg) {
    trackpad = DEVICE_DT_GET_ANY(azoteq_iqs5xx);
    if (trackpad == NULL) {
        LOG_ERR("Failed to get IQS5XX device");
        return -EINVAL;
    }

    // Initialize state
    accumPos.x = 0;
    accumPos.y = 0;
    current_gesture = GESTURE_NONE;

    LOG_INF("Enhanced ZMK trackpad gestures initialized");
    LOG_INF("3-finger swipe threshold: %d pixels", THREE_FINGER_SWIPE_THRESHOLD);
    LOG_INF("3-finger tap timeout: %d ms", TRACKPAD_THREE_FINGER_CLICK_TIME);

    int err = iqs5xx_trigger_set(trackpad, enhanced_trackpad_trigger_handler);
    if (err) {
        LOG_ERR("Failed to set trigger handler: %d", err);
        return -EINVAL;
    }

    return 0;
}

SYS_INIT(enhanced_trackpad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
