// src/trackpad_keyboard_events.c - Fixed API calls
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Initialize the keyboard events system
int trackpad_keyboard_init(const struct device *input_dev) {
    LOG_INF("ZMK trackpad keyboard events initialized");
    return 0;
}

// Send F3 key using ZMK's HID system directly
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 KEY ***");

    // Use ZMK's HID keyboard system directly
    zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_F3);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);

    // Short delay then release
    k_msleep(50);

    zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_F3);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);

    LOG_INF("F3 key sent via HID");
}

// Send F4 key using ZMK's HID system directly
void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 KEY ***");

    // Use ZMK's HID keyboard system directly
    zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_F4);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);

    // Short delay then release
    k_msleep(50);

    zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_F4);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);

    LOG_INF("F4 key sent via HID");
}

// Send zoom in (Ctrl+Plus)
void send_trackpad_zoom_in(void) {
    LOG_INF("*** TRACKPAD ZOOM IN ***");

    // Press Ctrl
    zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);
    k_msleep(10);

    // Press Equal/Plus
    zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);
    k_msleep(50);

    // Release Equal/Plus
    zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);
    k_msleep(10);

    // Release Ctrl
    zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);

    LOG_INF("Zoom in sent via HID");
}

// Send zoom out (Ctrl+Minus)
void send_trackpad_zoom_out(void) {
    LOG_INF("*** TRACKPAD ZOOM OUT ***");

    // Press Ctrl
    zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);
    k_msleep(10);

    // Press Minus
    zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);
    k_msleep(50);

    // Release Minus
    zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);
    k_msleep(10);

    // Release Ctrl
    zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);

    LOG_INF("Zoom out sent via HID");
}
