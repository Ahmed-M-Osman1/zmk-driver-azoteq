// src/trackpad_keyboard_events.c - FIXED VERSION with correct types
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

// FIXED: Use correct zmk_key_t type instead of uint8_t
static int send_zoom_combo(zmk_key_t modifier, zmk_key_t key, const char* description, int hold_time) {
    LOG_INF("*** SENDING %s (mod:0x%02x + key:0x%02x) ***", description, modifier, key);

    // Clear any existing state - USE 0x07
    zmk_hid_keyboard_clear();
    int ret = zmk_endpoints_send_report(0x07);
    if (ret < 0) {
        LOG_ERR("Failed to clear keyboard state: %d", ret);
        return ret;
    }
    k_msleep(50);

    // Press modifier first (if not zero)
    if (modifier != 0) {
        ret = zmk_hid_keyboard_press(modifier);
        if (ret < 0) {
            LOG_ERR("Failed to press modifier %d: %d", modifier, ret);
            return ret;
        }
        ret = zmk_endpoints_send_report(0x07);
        if (ret < 0) {
            LOG_ERR("Failed to send modifier report: %d", ret);
            return ret;
        }
        k_msleep(30);
    }

    // Press main key
    ret = zmk_hid_keyboard_press(key);
    if (ret < 0) {
        LOG_ERR("Failed to press key %d: %d", key, ret);
        if (modifier != 0) {
            zmk_hid_keyboard_release(modifier);
            zmk_endpoints_send_report(0x07);
        }
        return ret;
    }
    ret = zmk_endpoints_send_report(0x07);
    if (ret < 0) {
        LOG_ERR("Failed to send key report: %d", ret);
        return ret;
    }
    k_msleep(hold_time);

    // Release main key first
    ret = zmk_hid_keyboard_release(key);
    if (ret < 0) {
        LOG_ERR("Failed to release key: %d", ret);
    }
    ret = zmk_endpoints_send_report(0x07);
    if (ret < 0) {
        LOG_ERR("Failed to send key release report: %d", ret);
    }
    k_msleep(20);

    // Release modifier (if not zero)
    if (modifier != 0) {
        ret = zmk_hid_keyboard_release(modifier);
        if (ret < 0) {
            LOG_ERR("Failed to release modifier: %d", ret);
        }
        ret = zmk_endpoints_send_report(0x07);
        if (ret < 0) {
            LOG_ERR("Failed to send modifier release report: %d", ret);
        }
        k_msleep(30);
    }

    // Final clear
    zmk_hid_keyboard_clear();
    ret = zmk_endpoints_send_report(0x07);
    k_msleep(20);

    LOG_INF("%s sequence completed", description);
    return 0;
}

// ZOOM IN with multiple fallback methods
void send_trackpad_zoom_in(void) {
    LOG_INF("=== STARTING ZOOM IN SEQUENCE ===");

    // Method 1: Ctrl + Plus (using correct keycodes)
    send_zoom_combo(LCTRL, EQUAL, "Ctrl+Equal(Plus)", 150);
    k_msleep(100);

    // Method 2: Ctrl + Shift + Plus (explicit plus)
    LOG_INF("Trying Ctrl+Shift+Plus explicitly");
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);
    k_msleep(50);

    zmk_hid_keyboard_press(LCTRL);
    zmk_endpoints_send_report(0x07);
    k_msleep(20);
    zmk_hid_keyboard_press(LSHIFT);
    zmk_endpoints_send_report(0x07);
    k_msleep(20);
    zmk_hid_keyboard_press(EQUAL); // Shift+Equal = Plus
    zmk_endpoints_send_report(0x07);
    k_msleep(150);

    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(0x07);
    k_msleep(100);

    // Method 3: Cmd+Plus for Mac compatibility
    send_zoom_combo(LGUI, EQUAL, "Cmd+Plus(Mac)", 150);
    k_msleep(100);

    // Method 4: Try numeric keypad plus
    send_zoom_combo(LCTRL, KP_PLUS, "Ctrl+NumPad_Plus", 150);

    LOG_INF("=== ZOOM IN SEQUENCE COMPLETE ===");
}

// ZOOM OUT with multiple fallback methods
void send_trackpad_zoom_out(void) {
    LOG_INF("=== STARTING ZOOM OUT SEQUENCE ===");

    // Method 1: Ctrl + Minus
    send_zoom_combo(LCTRL, MINUS, "Ctrl+Minus", 150);
    k_msleep(100);

    // Method 2: Cmd+Minus for Mac
    send_zoom_combo(LGUI, MINUS, "Cmd+Minus(Mac)", 150);
    k_msleep(100);

    // Method 3: Numeric keypad minus
    send_zoom_combo(LCTRL, KP_MINUS, "Ctrl+NumPad_Minus", 150);

    LOG_INF("=== ZOOM OUT SEQUENCE COMPLETE ===");
}

// Test functions
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 TEST ***");
    send_zoom_combo(0, F3, "F3_Test", 100);
}

void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 TEST ***");
    send_zoom_combo(0, F4, "F4_Test", 100);
}
