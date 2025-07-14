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

// FIXED: Use 0x07 for keyboard HID usage page
static int send_zoom_combo(uint8_t modifier, uint8_t key, const char* description, int hold_time) {
    // Clear any existing state - USE 0x07
    zmk_hid_keyboard_clear();
    int ret = zmk_endpoints_send_report(0x07);
    if (ret < 0) {
        return ret;
    }
    k_msleep(50);

    // Press modifier first
    ret = zmk_hid_keyboard_press(modifier);
    if (ret < 0) {
        return ret;
    }
    ret = zmk_endpoints_send_report(0x07);
    if (ret < 0) {
        return ret;
    }
    k_msleep(30);

    // Press main key
    ret = zmk_hid_keyboard_press(key);
    if (ret < 0) {
        zmk_hid_keyboard_release(modifier);
        zmk_endpoints_send_report(0x07);
        return ret;
    }
    ret = zmk_endpoints_send_report(0x07);
    if (ret < 0) {
        return ret;
    }
    k_msleep(hold_time);

    // Release main key first
    ret = zmk_hid_keyboard_release(key);
    if (ret < 0) {
        // Continue with cleanup
    }
    ret = zmk_endpoints_send_report(0x07);
    if (ret < 0) {
        // Continue with cleanup
    }
    k_msleep(20);

    // Release modifier
    ret = zmk_hid_keyboard_release(modifier);
    if (ret < 0) {
        // Continue with cleanup
    }
    ret = zmk_endpoints_send_report(0x07);
    if (ret < 0) {
        // Continue with cleanup
    }
    k_msleep(30);

    // Final clear
    zmk_hid_keyboard_clear();
    ret = zmk_endpoints_send_report(0x07);
    k_msleep(20);

    return 0;
}

// ZOOM IN with multiple fallback methods
void send_trackpad_zoom_in(void) {
    // Method 1: Ctrl + Plus (using correct keycodes)
    send_zoom_combo(LCTRL, EQUAL, "Ctrl+Equal(Plus)", 150);
    k_msleep(100);

    // Method 2: Ctrl + Shift + Plus (explicit plus)
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
}

// ZOOM OUT with multiple fallback methods
void send_trackpad_zoom_out(void) {
    // Method 1: Ctrl + Minus
    send_zoom_combo(LCTRL, MINUS, "Ctrl+Minus", 150);
    k_msleep(100);

    // Method 2: Cmd+Minus for Mac
    send_zoom_combo(LGUI, MINUS, "Cmd+Minus(Mac)", 150);
    k_msleep(100);

    // Method 3: Numeric keypad minus
    send_zoom_combo(LCTRL, KP_MINUS, "Ctrl+NumPad_Minus", 150);
}

// Test functions
void send_trackpad_f3(void) {
    send_zoom_combo(0, F3, "F3_Test", 100);
}

void send_trackpad_f4(void) {
    send_zoom_combo(0, F4, "F4_Test", 100);
}
