// src/trackpad_keyboard_events.c - OPTIMIZED FOR SPEED
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/keys.h>

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Initialize the keyboard events system
int trackpad_keyboard_init(const struct device *input_dev) {
    LOG_INF("ZMK trackpad keyboard events initialized");
    return 0;
}

// OPTIMIZED: Much faster zoom with minimal delays
static int send_zoom_combo_fast(uint8_t modifier, uint8_t key, const char* description) {
    LOG_INF("*** FAST %s ***", description);

    // Quick clear
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);
    k_msleep(10); // Reduced from 50ms

    // Press modifier
    zmk_hid_keyboard_press(modifier);
    zmk_endpoints_send_report(0x07);
    k_msleep(5); // Reduced from 30ms

    // Press key
    zmk_hid_keyboard_press(key);
    zmk_endpoints_send_report(0x07);
    k_msleep(30); // Reduced from 150ms

    // Quick release
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);

    LOG_INF("%s sent", description);
    return 0;
}

// FAST ZOOM IN - Only send the most effective command
void send_trackpad_zoom_in(void) {
    LOG_INF("*** ZOOM IN ***");

    // Only send Ctrl+Plus - fastest method
    send_zoom_combo_fast(LCTRL, EQUAL, "Ctrl+Plus");

    LOG_INF("Zoom in complete");
}

// FAST ZOOM OUT - Only send the most effective command
void send_trackpad_zoom_out(void) {
    LOG_INF("*** ZOOM OUT ***");

    // Only send Ctrl+Minus - fastest method
    send_zoom_combo_fast(LCTRL, MINUS, "Ctrl+Minus");

    LOG_INF("Zoom out complete");
}

// Test functions
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 TEST ***");
    send_zoom_combo_fast(0, F3, "F3_Test");
}

void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 TEST ***");
    send_zoom_combo_fast(0, F4, "F4_Test");
}
