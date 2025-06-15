// src/trackpad_keyboard_events.c
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/key_position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Virtual key positions for trackpad gestures
// These should be higher than your actual keyboard matrix
#define TRACKPAD_GESTURE_F3_POS     100
#define TRACKPAD_GESTURE_F4_POS     101
#define TRACKPAD_GESTURE_ZOOM_IN    102
#define TRACKPAD_GESTURE_ZOOM_OUT   103

// Function to send a key press/release through ZMK's keymap system
static int send_trackpad_keycode(uint8_t key_position, bool pressed) {
    struct zmk_keycode_state_changed *ev = new_zmk_keycode_state_changed();
    if (!ev) {
        LOG_ERR("Failed to allocate keycode state changed event");
        return -ENOMEM;
    }

    // Get the keycode from the keymap for this position
    ev->usage_page = HID_USAGE_KEY;
    ev->keycode = zmk_keymap_layer_event_from_user_input(0, key_position);
    ev->state = pressed;
    ev->timestamp = k_uptime_get();

    LOG_INF("Sending trackpad keycode: pos=%d, code=0x%04x, pressed=%d",
            key_position, ev->keycode, pressed);

    return ZMK_EVENT_RAISE(ev);
}

// Alternative approach using key position events
static int send_trackpad_key_position(uint8_t key_position, bool pressed) {
    struct zmk_key_position_state_changed *ev = new_zmk_key_position_state_changed();
    if (!ev) {
        LOG_ERR("Failed to allocate key position state changed event");
        return -ENOMEM;
    }

    ev->position = key_position;
    ev->state = pressed;
    ev->timestamp = k_uptime_get();

    LOG_INF("Sending trackpad key position: pos=%d, pressed=%d", key_position, pressed);

    return ZMK_EVENT_RAISE(ev);
}

// Public functions for your gesture handlers
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 KEY ***");
    send_trackpad_key_position(TRACKPAD_GESTURE_F3_POS, true);
    k_msleep(10);
    send_trackpad_key_position(TRACKPAD_GESTURE_F3_POS, false);
}

void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 KEY ***");
    send_trackpad_key_position(TRACKPAD_GESTURE_F4_POS, true);
    k_msleep(10);
    send_trackpad_key_position(TRACKPAD_GESTURE_F4_POS, false);
}

void send_trackpad_zoom_in(void) {
    LOG_INF("*** TRACKPAD ZOOM IN ***");
    send_trackpad_key_position(TRACKPAD_GESTURE_ZOOM_IN, true);
    k_msleep(10);
    send_trackpad_key_position(TRACKPAD_GESTURE_ZOOM_IN, false);
}

void send_trackpad_zoom_out(void) {
    LOG_INF("*** TRACKPAD ZOOM OUT ***");
    send_trackpad_key_position(TRACKPAD_GESTURE_ZOOM_OUT, true);
    k_msleep(10);
    send_trackpad_key_position(TRACKPAD_GESTURE_ZOOM_OUT, false);
}
