// src/trackpad_keyboard_events.c - FIXED VERSION with HID_USAGE_KEY
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/keys.h>


// Initialize the keyboard events system
int trackpad_keyboard_init(const struct device *input_dev) {
    return 0;
}

// FIXED: Use HID_USAGE_KEY for keyboard HID usage page
static int send_zoom_combo(uint32_t modifier, uint32_t key, const char* description, int hold_time) {

    // Clear any existing state - USE HID_USAGE_KEY
    zmk_hid_keyboard_clear();
    int ret = zmk_endpoints_send_report(HID_USAGE_KEY);
    if (ret < 0) {
        return ret;
    }
    k_msleep(50);

    // Press modifier first (if any)
    if (modifier != 0) {
        ret = zmk_hid_keyboard_press(modifier);
        if (ret < 0) {
            return ret;
        }
        ret = zmk_endpoints_send_report(HID_USAGE_KEY);
        if (ret < 0) {
            return ret;
        }
        k_msleep(30);
    }

    // Press main key
    ret = zmk_hid_keyboard_press(key);
    if (ret < 0) {
        if (modifier != 0) {
            zmk_hid_keyboard_release(modifier);
            zmk_endpoints_send_report(HID_USAGE_KEY);
        }
        return ret;
    }
    ret = zmk_endpoints_send_report(HID_USAGE_KEY);
    if (ret < 0) {
        return ret;
    }
    k_msleep(hold_time);

    // Release main key first
    ret = zmk_hid_keyboard_release(key);
    if (ret < 0) {
        return ret;
    }
    ret = zmk_endpoints_send_report(HID_USAGE_KEY);
    if (ret < 0) {
        return ret;
    }
    k_msleep(20);

    // Release modifier (if any)
    if (modifier != 0) {
        ret = zmk_hid_keyboard_release(modifier);
        if (ret < 0) {
            return ret;
        }
        ret = zmk_endpoints_send_report(HID_USAGE_KEY);
        if (ret < 0) {
            return ret;
        }
        k_msleep(30);
    }

    // Final clear
    zmk_hid_keyboard_clear();
    ret = zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(20);

    return 0;
}

// ZOOM IN with multiple fallback methods
void send_trackpad_zoom_in(void) {

    // Method 1: Ctrl + Plus (using correct keycodes)
    send_zoom_combo(LEFT_CONTROL, EQUAL, "Ctrl+Equal(Plus)", 150);
    k_msleep(100);

    // Method 2: Ctrl + Shift + Plus (explicit plus)
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(50);

    zmk_hid_keyboard_press(LEFT_CONTROL);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(20);
    zmk_hid_keyboard_press(LEFT_SHIFT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(20);
    zmk_hid_keyboard_press(EQUAL); // Shift+Equal = Plus
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(150);

    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(100);

    // Method 3: Cmd+Plus for Mac compatibility
    send_zoom_combo(LEFT_GUI, EQUAL, "Cmd+Plus(Mac)", 150);
    k_msleep(100);

    // Method 4: Try numeric keypad plus
    send_zoom_combo(LEFT_CONTROL, KP_PLUS, "Ctrl+NumPad_Plus", 150);
}

// ZOOM OUT with multiple fallback methods
void send_trackpad_zoom_out(void) {

    // Method 1: Ctrl + Minus
    send_zoom_combo(LEFT_CONTROL, MINUS, "Ctrl+Minus", 150);
    k_msleep(100);

    // Method 2: Cmd+Minus for Mac
    send_zoom_combo(LEFT_GUI, MINUS, "Cmd+Minus(Mac)", 150);
    k_msleep(100);

    // Method 3: Numeric keypad minus
    send_zoom_combo(LEFT_CONTROL, KP_MINUS, "Ctrl+NumPad_Minus", 150);
}

// Test functions
void send_trackpad_f3(void) {
    send_zoom_combo(0, F3, "F3_Test", 100);
}

void send_trackpad_f4(void) {
    send_zoom_combo(0, F4, "F4_Test", 100);
}
