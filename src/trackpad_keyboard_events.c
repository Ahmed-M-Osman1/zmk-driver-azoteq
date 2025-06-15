// src/trackpad_keyboard_events.c - IMPROVED VERSION with multiple zoom options
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

// Helper function to clear and wait
static void clear_and_wait(int ms) {
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);
    k_msleep(ms);
}

// Helper function to send key combination
static int send_key_combo(uint8_t modifier, uint8_t key, const char* description) {
    LOG_INF("Sending %s: mod=0x%02x, key=0x%02x", description, modifier, key);

    clear_and_wait(10);

    // Press modifier
    int ret1 = zmk_hid_keyboard_press(modifier);
    if (ret1 < 0) {
        LOG_ERR("Failed to press modifier: %d", ret1);
        return ret1;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(20);

    // Press main key
    int ret2 = zmk_hid_keyboard_press(key);
    if (ret2 < 0) {
        LOG_ERR("Failed to press key: %d", ret2);
        zmk_hid_keyboard_clear();
        zmk_endpoints_send_report(0x07);
        return ret2;
    }
    zmk_endpoints_send_report(0x07);
    k_msleep(100); // Hold longer

    // Release main key
    zmk_hid_keyboard_release(key);
    zmk_endpoints_send_report(0x07);
    k_msleep(20);

    // Release modifier
    zmk_hid_keyboard_release(modifier);
    zmk_endpoints_send_report(0x07);
    k_msleep(20);

    clear_and_wait(10);

    LOG_INF("%s completed successfully", description);
    return 0;
}

// ZOOM IN - Try multiple methods
void send_trackpad_zoom_in(void) {
    LOG_INF("*** TRACKPAD ZOOM IN - TRYING MULTIPLE METHODS ***");

    // Method 1: Ctrl+Plus (most common)
    LOG_INF("Method 1: Ctrl+Equal (Plus)");
    send_key_combo(LCTRL, EQUAL, "Ctrl+Plus");
    k_msleep(50);

    // Method 2: Ctrl+Shift+Plus (some systems)
    LOG_INF("Method 2: Ctrl+Shift+Equal");
    clear_and_wait(10);
    zmk_hid_keyboard_press(LCTRL);
    zmk_endpoints_send_report(0x07);
    k_msleep(10);
    zmk_hid_keyboard_press(LSHIFT);
    zmk_endpoints_send_report(0x07);
    k_msleep(10);
    zmk_hid_keyboard_press(EQUAL);
    zmk_endpoints_send_report(0x07);
    k_msleep(100);
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);
    k_msleep(50);

    // Method 3: Cmd+Plus (Mac)
    LOG_INF("Method 3: Cmd+Equal (Mac style)");
    send_key_combo(LGUI, EQUAL, "Cmd+Plus");
    k_msleep(50);

    LOG_INF("All zoom in methods completed");
}

// ZOOM OUT - Try multiple methods
void send_trackpad_zoom_out(void) {
    LOG_INF("*** TRACKPAD ZOOM OUT - TRYING MULTIPLE METHODS ***");

    // Method 1: Ctrl+Minus (most common)
    LOG_INF("Method 1: Ctrl+Minus");
    send_key_combo(LCTRL, MINUS, "Ctrl+Minus");
    k_msleep(50);

    // Method 2: Ctrl+0 (reset zoom, some browsers)
    LOG_INF("Method 2: Ctrl+0 (reset)");
    send_key_combo(LCTRL, N0, "Ctrl+0");
    k_msleep(50);

    // Method 3: Cmd+Minus (Mac)
    LOG_INF("Method 3: Cmd+Minus (Mac style)");
    send_key_combo(LGUI, MINUS, "Cmd+Minus");
    k_msleep(50);

    LOG_INF("All zoom out methods completed");
}

// Test function - send F3 for testing
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 TEST ***");
    send_key_combo(0, F3, "F3 Key Test");
}

// Test function - send F4 for testing
void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 TEST ***");
    send_key_combo(0, F4, "F4 Key Test");
}
