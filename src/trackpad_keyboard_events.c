// src/trackpad_keyboard_events.c - FIXED VERSION
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

// FIXED: More reliable zoom with proper error checking
static int send_zoom_combo_fixed(uint8_t modifier, uint8_t key, const char* description) {
    LOG_INF("*** %s ***", description);

    // Step 1: Clear everything first
    zmk_hid_keyboard_clear();  // void function - no return value
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Step 2: Press modifier and verify
    int ret = zmk_hid_keyboard_press(modifier);
    if (ret < 0) {
        LOG_ERR("Failed to press modifier %d: %d", modifier, ret);
        return ret;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(20);  // Slightly longer for modifier

    // Step 3: Press key and verify
    ret = zmk_hid_keyboard_press(key);
    if (ret < 0) {
        LOG_ERR("Failed to press key %d: %d", key, ret);
        // Try to clean up modifier
        zmk_hid_keyboard_release(modifier);
        zmk_endpoints_send_report(0x07);
        return ret;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(40);  // Hold the combination

    // Step 4: Release key first
    ret = zmk_hid_keyboard_release(key);
    if (ret < 0) {
        LOG_WRN("Failed to release key %d: %d", key, ret);
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Step 5: Release modifier
    ret = zmk_hid_keyboard_release(modifier);
    if (ret < 0) {
        LOG_WRN("Failed to release modifier %d: %d", modifier, ret);
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(10);

    // Step 6: Final clear to ensure clean state
    zmk_hid_keyboard_clear();  // void function - no return value
    zmk_endpoints_send_report(0x07);

    LOG_INF("%s completed successfully", description);
    return 0;
}

// ZOOM IN with better reliability
void send_trackpad_zoom_in(void) {
    LOG_INF("*** ZOOM IN ***");

    int ret = send_zoom_combo_fixed(LCTRL, EQUAL, "Ctrl+Plus");
    if (ret < 0) {
        LOG_ERR("Zoom in failed: %d", ret);
    } else {
        LOG_INF("Zoom in complete");
    }
}

// ZOOM OUT with better reliability
void send_trackpad_zoom_out(void) {
    LOG_INF("*** ZOOM OUT ***");

    int ret = send_zoom_combo_fixed(LCTRL, MINUS, "Ctrl+Minus");
    if (ret < 0) {
        LOG_ERR("Zoom out failed: %d", ret);
    } else {
        LOG_INF("Zoom out complete");
    }
}

// Test functions
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 TEST ***");
    send_zoom_combo_fixed(0, F3, "F3_Test");
}

void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 TEST ***");
    send_zoom_combo_fixed(0, F4, "F4_Test");
}
