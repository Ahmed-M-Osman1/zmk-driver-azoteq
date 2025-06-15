// src/trackpad_keyboard_events.c - Simplified approach for external modules
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Since we can't access ZMK's internal APIs, we'll use a different approach
// We'll create a virtual input device that sends keyboard events

static const struct device *keyboard_dev = NULL;

// Initialize keyboard device reference
int trackpad_keyboard_init(const struct device *input_dev) {
    keyboard_dev = input_dev;
    LOG_INF("Trackpad keyboard events initialized with device: %p", keyboard_dev);
    return 0;
}

// Send a keyboard key through the input system
static int send_keyboard_event(uint16_t keycode) {
    if (!keyboard_dev) {
        LOG_ERR("Keyboard device not initialized");
        return -ENODEV;
    }

    LOG_INF("Sending keyboard event: keycode=%d", keycode);

    // Send key press
    int ret = input_report(keyboard_dev, INPUT_EV_KEY, keycode, 1, false, K_NO_WAIT);
    if (ret < 0) {
        LOG_ERR("Failed to send key press: %d", ret);
        return ret;
    }

    // Small delay
    k_msleep(10);

    // Send key release
    ret = input_report(keyboard_dev, INPUT_EV_KEY, keycode, 0, true, K_NO_WAIT);
    if (ret < 0) {
        LOG_ERR("Failed to send key release: %d", ret);
        return ret;
    }

    LOG_DBG("Keyboard event sent successfully");
    return 0;
}

// Send combination keys (modifier + key)
static int send_keyboard_combo(uint16_t modifier, uint16_t keycode) {
    if (!keyboard_dev) {
        LOG_ERR("Keyboard device not initialized");
        return -ENODEV;
    }

    LOG_INF("Sending keyboard combo: mod=%d, key=%d", modifier, keycode);

    // Send modifier press
    int ret = input_report(keyboard_dev, INPUT_EV_KEY, modifier, 1, false, K_NO_WAIT);
    if (ret < 0) {
        LOG_ERR("Failed to send modifier press: %d", ret);
        return ret;
    }

    k_msleep(5);

    // Send key press
    ret = input_report(keyboard_dev, INPUT_EV_KEY, keycode, 1, false, K_NO_WAIT);
    if (ret < 0) {
        LOG_ERR("Failed to send key press: %d", ret);
        return ret;
    }

    k_msleep(10);

    // Send key release
    ret = input_report(keyboard_dev, INPUT_EV_KEY, keycode, 0, false, K_NO_WAIT);
    if (ret < 0) {
        LOG_ERR("Failed to send key release: %d", ret);
        return ret;
    }

    k_msleep(5);

    // Send modifier release
    ret = input_report(keyboard_dev, INPUT_EV_KEY, modifier, 0, true, K_NO_WAIT);
    if (ret < 0) {
        LOG_ERR("Failed to send modifier release: %d", ret);
        return ret;
    }

    LOG_DBG("Keyboard combo sent successfully");
    return 0;
}

// Public functions for trackpad gestures
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 KEY ***");
    send_keyboard_event(INPUT_KEY_F3);
}

void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 KEY ***");
    send_keyboard_event(INPUT_KEY_F4);
}

void send_trackpad_zoom_in(void) {
    LOG_INF("*** TRACKPAD ZOOM IN ***");
    send_keyboard_combo(INPUT_KEY_LEFTCTRL, INPUT_KEY_EQUAL);
}

void send_trackpad_zoom_out(void) {
    LOG_INF("*** TRACKPAD ZOOM OUT ***");
    send_keyboard_combo(INPUT_KEY_LEFTCTRL, INPUT_KEY_MINUS);
}
