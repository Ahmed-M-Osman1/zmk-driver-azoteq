#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/keys.h>

LOG_MODULE_DECLARE(trackpad, CONFIG_ZMK_LOG_LEVEL);

// Initialize the keyboard events system
int trackpad_keyboard_init(const struct device *input_dev) {
    LOG_INF("Trackpad keyboard events initialized");
    return 0;
}

// FIXED: Use proper HID usage codes
static int send_zoom_combo(uint8_t modifier, uint8_t key, const char* description, int hold_time) {
    // Clear any existing state - USE HID_USAGE_GD_KEYBOARD
    zmk_hid_keyboard_clear();
    int ret = zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    if (ret < 0) {
        LOG_ERR("Failed to clear keyboard state: %d", ret);
        return ret;
    }
    k_msleep(50);

    // Press modifier first if specified
    if (modifier != 0) {
        ret = zmk_hid_keyboard_press(modifier);
        if (ret < 0) {
            LOG_ERR("Failed to press modifier: %d", ret);
            return ret;
        }
        ret = zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
        if (ret < 0) {
            LOG_ERR("Failed to send modifier report: %d", ret);
            return ret;
        }
        k_msleep(30);
    }

    // Press main key
    ret = zmk_hid_keyboard_press(key);
    if (ret < 0) {
        LOG_ERR("Failed to press key: %d", ret);
        if (modifier != 0) {
            zmk_hid_keyboard_release(modifier);
            zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
        }
        return ret;
    }
    ret = zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    if (ret < 0) {
        LOG_ERR("Failed to send key report: %d", ret);
        return ret;
    }
    k_msleep(hold_time);

    // Release main key first
    ret = zmk_hid_keyboard_release(key);
    if (ret < 0) {
        LOG_ERR("Failed to release key: %d", ret);
        return ret;
    }
    ret = zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    if (ret < 0) {
        LOG_ERR("Failed to send key release report: %d", ret);
        return ret;
    }
    k_msleep(20);

    // Release modifier if it was pressed
    if (modifier != 0) {
        ret = zmk_hid_keyboard_release(modifier);
        if (ret < 0) {
            LOG_ERR("Failed to release modifier: %d", ret);
            return ret;
        }
        ret = zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
        if (ret < 0) {
            LOG_ERR("Failed to send modifier release report: %d", ret);
            return ret;
        }
        k_msleep(30);
    }

    // Final clear
    zmk_hid_keyboard_clear();
    ret = zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(20);

    LOG_DBG("Sent %s", description);
    return 0;
}

// ZOOM IN with multiple fallback methods
void send_trackpad_zoom_in(void) {
    LOG_INF("Sending zoom in command");

    // Method 1: Ctrl + Plus (using correct keycodes)
    send_zoom_combo(HID_USAGE_KEY_KEYBOARD_LEFT_CONTROL, HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, "Ctrl+Equal(Plus)", 150);
    k_msleep(100);

    // Method 2: Ctrl + Shift + Plus (explicit plus)
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(50);

    zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_LEFT_CONTROL);
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(20);
    zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_LEFT_SHIFT);
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(20);
    zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS); // Shift+Equal = Plus
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(150);

    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
    k_msleep(100);

    // Method 3: Cmd+Plus for Mac compatibility
    send_zoom_combo(HID_USAGE_KEY_KEYBOARD_LEFT_GUI, HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, "Cmd+Plus(Mac)", 150);
    k_msleep(100);

    // Method 4: Try numeric keypad plus
    send_zoom_combo(HID_USAGE_KEY_KEYBOARD_LEFT_CONTROL, HID_USAGE_KEY_KEYPAD_PLUS_AND_PLUS, "Ctrl+NumPad_Plus", 150);
}

// ZOOM OUT with multiple fallback methods
void send_trackpad_zoom_out(void) {
    LOG_INF("Sending zoom out command");

    // Method 1: Ctrl + Minus
    send_zoom_combo(HID_USAGE_KEY_KEYBOARD_LEFT_CONTROL, HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE, "Ctrl+Minus", 150);
    k_msleep(100);

    // Method 2: Cmd+Minus for Mac
    send_zoom_combo(HID_USAGE_KEY_KEYBOARD_LEFT_GUI, HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE, "Cmd+Minus(Mac)", 150);
    k_msleep(100);

    // Method 3: Numeric keypad minus
    send_zoom_combo(HID_USAGE_KEY_KEYBOARD_LEFT_CONTROL, HID_USAGE_KEY_KEYPAD_MINUS_AND_MINUS, "Ctrl+NumPad_Minus", 150);
}

// Test functions
void send_trackpad_f3(void) {
    send_zoom_combo(0, HID_USAGE_KEY_KEYBOARD_F3, "F3_Test", 100);
}

void send_trackpad_f4(void) {
    send_zoom_combo(0, HID_USAGE_KEY_KEYBOARD_F4, "F4_Test", 100);
}
