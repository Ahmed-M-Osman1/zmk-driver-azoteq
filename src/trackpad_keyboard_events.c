// src/trackpad_keyboard_events.c - FIXED to properly release keys
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

// Send F3 key using ZMK's keyboard HID system (FIXED VERSION)
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 KEY ***");

    // Clear any existing state first
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Press F3
    int ret1 = zmk_hid_keyboard_press(F3);
    if (ret1 < 0) {
        LOG_ERR("Failed to press F3: %d", ret1);
        return;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(50); // Hold key briefly

    // Release F3
    int ret2 = zmk_hid_keyboard_release(F3);
    if (ret2 < 0) {
        LOG_ERR("Failed to release F3: %d", ret2);
    }
    zmk_endpoints_send_report(0x07);

    LOG_INF("F3 key sent and released successfully");
}

// Send F4 key using ZMK's keyboard HID system (FIXED VERSION)
void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 KEY ***");

    // Clear any existing state first
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Press F4
    int ret1 = zmk_hid_keyboard_press(F4);
    if (ret1 < 0) {
        LOG_ERR("Failed to press F4: %d", ret1);
        return;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(50); // Hold key briefly

    // Release F4
    int ret2 = zmk_hid_keyboard_release(F4);
    if (ret2 < 0) {
        LOG_ERR("Failed to release F4: %d", ret2);
    }
    zmk_endpoints_send_report(0x07);

    LOG_INF("F4 key sent and released successfully");
}

// Send zoom in (Ctrl+Plus) - IMPROVED ERROR HANDLING
void send_trackpad_zoom_in(void) {
    LOG_INF("*** TRACKPAD ZOOM IN ***");

    // Clear any existing state first
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Press Ctrl
    int ret1 = zmk_hid_keyboard_press(LCTRL);
    if (ret1 < 0) {
        LOG_ERR("Failed to press CTRL: %d", ret1);
        return;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Press Equal/Plus
    int ret2 = zmk_hid_keyboard_press(EQUAL);
    if (ret2 < 0) {
        LOG_ERR("Failed to press EQUAL: %d", ret2);
        zmk_hid_keyboard_release(LCTRL);
        zmk_endpoints_send_report(0x07);
        return;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(50);

    // Release Equal/Plus
    zmk_hid_keyboard_release(EQUAL);
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Release Ctrl
    zmk_hid_keyboard_release(LCTRL);
    zmk_endpoints_send_report(0x07);

    LOG_INF("Zoom in sent successfully");
}

// Send zoom out (Ctrl+Minus) - IMPROVED ERROR HANDLING
void send_trackpad_zoom_out(void) {
    LOG_INF("*** TRACKPAD ZOOM OUT ***");

    // Clear any existing state first
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Press Ctrl
    int ret1 = zmk_hid_keyboard_press(LCTRL);
    if (ret1 < 0) {
        LOG_ERR("Failed to press CTRL: %d", ret1);
        return;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Press Minus
    int ret2 = zmk_hid_keyboard_press(MINUS);
    if (ret2 < 0) {
        LOG_ERR("Failed to press MINUS: %d", ret2);
        zmk_hid_keyboard_release(LCTRL);
        zmk_endpoints_send_report(0x07);
        return;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(50);

    // Release Minus
    zmk_hid_keyboard_release(MINUS);
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Release Ctrl
    zmk_hid_keyboard_release(LCTRL);
    zmk_endpoints_send_report(0x07);

    LOG_INF("Zoom out sent successfully");
}
