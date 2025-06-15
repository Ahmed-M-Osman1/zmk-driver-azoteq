// src/trackpad_keyboard_events.c - Fixed ZMK event macro usage
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/event_manager.h>

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Work items for key releases
static struct k_work_delayable f3_release_work;
static struct k_work_delayable f4_release_work;
static struct k_work_delayable zoom_in_release_work;
static struct k_work_delayable zoom_out_release_work;

// Work callbacks for key releases
static void f3_release_cb(struct k_work *work) {
    LOG_INF("F3 key release");
    struct zmk_keycode_state_changed release_event = {
        .keycode = 0x3C, // F3 keycode
        .state = false,
        .timestamp = k_uptime_get()
    };

    ZMK_EVENT_RAISE(as_zmk_keycode_state_changed(release_event));
}

static void f4_release_cb(struct k_work *work) {
    LOG_INF("F4 key release");
    struct zmk_keycode_state_changed release_event = {
        .keycode = 0x3D, // F4 keycode
        .state = false,
        .timestamp = k_uptime_get()
    };

    ZMK_EVENT_RAISE(as_zmk_keycode_state_changed(release_event));
}

static void zoom_in_modifier_release_cb(struct k_work *work) {
    LOG_INF("Zoom in combo release");

    // Release EQUAL key first
    struct zmk_keycode_state_changed key_release = {
        .keycode = 0x2E, // EQUAL keycode
        .state = false,
        .timestamp = k_uptime_get()
    };
    ZMK_EVENT_RAISE(as_zmk_keycode_state_changed(key_release));

    // Then release CTRL
    k_msleep(10);
    struct zmk_keycode_state_changed mod_release = {
        .keycode = 0xE0, // LEFT_CONTROL keycode
        .state = false,
        .timestamp = k_uptime_get()
    };
    ZMK_EVENT_RAISE(as_zmk_keycode_state_changed(mod_release));
}

static void zoom_out_modifier_release_cb(struct k_work *work) {
    LOG_INF("Zoom out combo release");

    // Release MINUS key first
    struct zmk_keycode_state_changed key_release = {
        .keycode = 0x2D, // MINUS keycode
        .state = false,
        .timestamp = k_uptime_get()
    };
    ZMK_EVENT_RAISE(as_zmk_keycode_state_changed(key_release));

    // Then release CTRL
    k_msleep(10);
    struct zmk_keycode_state_changed mod_release = {
        .keycode = 0xE0, // LEFT_CONTROL keycode
        .state = false,
        .timestamp = k_uptime_get()
    };
    ZMK_EVENT_RAISE(as_zmk_keycode_state_changed(mod_release));
}

// Initialize the keyboard events system
int trackpad_keyboard_init(const struct device *input_dev) {
    // Initialize work items for key releases
    k_work_init_delayable(&f3_release_work, f3_release_cb);
    k_work_init_delayable(&f4_release_work, f4_release_cb);
    k_work_init_delayable(&zoom_in_release_work, zoom_in_modifier_release_cb);
    k_work_init_delayable(&zoom_out_release_work, zoom_out_modifier_release_cb);

    LOG_INF("ZMK trackpad keyboard events initialized");
    return 0;
}

// Send a single keycode through ZMK's event system
static int send_zmk_keycode(uint8_t keycode, struct k_work_delayable *release_work) {
    LOG_INF("Sending ZMK keycode: %d", keycode);

    // Create and raise key press event
    struct zmk_keycode_state_changed press_event = {
        .keycode = keycode,
        .state = true,
        .timestamp = k_uptime_get()
    };

    // Raise the event using proper macro
    ZMK_EVENT_RAISE(as_zmk_keycode_state_changed(press_event));

    // Schedule key release
    k_work_schedule(release_work, K_MSEC(50));

    LOG_DBG("ZMK keycode sent successfully");
    return 0;
}

// Send combination keys through ZMK's event system
static int send_zmk_combo(uint8_t modifier, uint8_t keycode, struct k_work_delayable *release_work) {
    LOG_INF("Sending ZMK combo: mod=%d, key=%d", modifier, keycode);

    // Create and raise modifier press event
    struct zmk_keycode_state_changed mod_press = {
        .keycode = modifier,
        .state = true,
        .timestamp = k_uptime_get()
    };

    ZMK_EVENT_RAISE(as_zmk_keycode_state_changed(mod_press));

    // Small delay before key
    k_msleep(10);

    // Create and raise key press event
    struct zmk_keycode_state_changed key_press = {
        .keycode = keycode,
        .state = true,
        .timestamp = k_uptime_get()
    };

    ZMK_EVENT_RAISE(as_zmk_keycode_state_changed(key_press));

    // Schedule combination release
    k_work_schedule(release_work, K_MSEC(100));

    LOG_DBG("ZMK combo sent successfully");
    return 0;
}

// Public functions for trackpad gestures using ZMK events
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 KEY ***");
    send_zmk_keycode(0x3C, &f3_release_work); // F3 keycode
}

void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 KEY ***");
    send_zmk_keycode(0x3D, &f4_release_work); // F4 keycode
}

void send_trackpad_zoom_in(void) {
    LOG_INF("*** TRACKPAD ZOOM IN ***");
    send_zmk_combo(0xE0, 0x2E, &zoom_in_release_work); // CTRL + EQUAL
}

void send_trackpad_zoom_out(void) {
    LOG_INF("*** TRACKPAD ZOOM OUT ***");
    send_zmk_combo(0xE0, 0x2D, &zoom_out_release_work); // CTRL + MINUS
}k_keycode_state_changed, mod_release);
}

// Initialize the keyboard events system
int trackpad_keyboard_init(const struct device *input_dev) {
    // Initialize work items for key releases
    k_work_init_delayable(&f3_release_work, f3_release_cb);
    k_work_init_delayable(&f4_release_work, f4_release_cb);
    k_work_init_delayable(&zoom_in_release_work, zoom_in_modifier_release_cb);
    k_work_init_delayable(&zoom_out_release_work, zoom_out_modifier_release_cb);

    LOG_INF("ZMK trackpad keyboard events initialized");
    return 0;
}

// Send a single keycode through ZMK's event system
static int send_zmk_keycode(uint8_t keycode, struct k_work_delayable *release_work) {
    LOG_INF("Sending ZMK keycode: %d", keycode);

    // Create and raise key press event
    struct zmk_keycode_state_changed press_event = {
        .keycode = keycode,
        .state = true,
        .timestamp = k_uptime_get()
    };

    // Raise the event
    int ret = ZMK_EVENT_RAISE(zmk_keycode_state_changed, press_event);
    if (ret < 0) {
        LOG_ERR("Failed to raise key press event: %d", ret);
        return ret;
    }

    // Schedule key release
    k_work_schedule(release_work, K_MSEC(50));

    LOG_DBG("ZMK keycode sent successfully");
    return 0;
}

// Send combination keys through ZMK's event system
static int send_zmk_combo(uint8_t modifier, uint8_t keycode, struct k_work_delayable *release_work) {
    LOG_INF("Sending ZMK combo: mod=%d, key=%d", modifier, keycode);

    // Create and raise modifier press event
    struct zmk_keycode_state_changed mod_press = {
        .keycode = modifier,
        .state = true,
        .timestamp = k_uptime_get()
    };

    int ret = ZMK_EVENT_RAISE(zmk_keycode_state_changed, mod_press);
    if (ret < 0) {
        LOG_ERR("Failed to raise modifier press: %d", ret);
        return ret;
    }

    // Small delay before key
    k_msleep(10);

    // Create and raise key press event
    struct zmk_keycode_state_changed key_press = {
        .keycode = keycode,
        .state = true,
        .timestamp = k_uptime_get()
    };

    ret = ZMK_EVENT_RAISE(zmk_keycode_state_changed, key_press);
    if (ret < 0) {
        LOG_ERR("Failed to raise key press: %d", ret);
        return ret;
    }

    // Schedule combination release
    k_work_schedule(release_work, K_MSEC(100));

    LOG_DBG("ZMK combo sent successfully");
    return 0;
}

// Public functions for trackpad gestures using ZMK events
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 KEY ***");
    send_zmk_keycode(0x3C, &f3_release_work); // F3 keycode
}

void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 KEY ***");
    send_zmk_keycode(0x3D, &f4_release_work); // F4 keycode
}

void send_trackpad_zoom_in(void) {
    LOG_INF("*** TRACKPAD ZOOM IN ***");
    send_zmk_combo(0xE0, 0x2E, &zoom_in_release_work); // CTRL + EQUAL
}

void send_trackpad_zoom_out(void) {
    LOG_INF("*** TRACKPAD ZOOM OUT ***");
    send_zmk_combo(0xE0, 0x2D, &zoom_out_release_work); // CTRL + MINUS
};
}

// Send combination keys through ZMK's event system
static int send_zmk_combo(uint32_t modifier, uint32_t keycode, struct k_work_delayable *release_work) {
    LOG_INF("Sending ZMK combo: mod=%d, key=%d", modifier, keycode);

    // Create and raise modifier press event
    struct zmk_keycode_state_changed *mod_press = new_zmk_keycode_state_changed((struct zmk_keycode_state_changed){
        .usage_page = HID_USAGE_PAGE_KEYBOARD,
        .keycode = modifier,
        .state = true,
        .timestamp = k_uptime_get()
    });

    if (!mod_press) {
        LOG_ERR("Failed to create modifier press event");
        return -ENOMEM;
    }

    int ret = ZMK_EVENT_RAISE(mod_press);
    if (ret < 0) {
        LOG_ERR("Failed to raise modifier press: %d", ret);
        return ret;
    }

    // Small delay before key
    k_msleep(10);

    // Create and raise key press event
    struct zmk_keycode_state_changed *key_press = new_zmk_keycode_state_changed((struct zmk_keycode_state_changed){
        .usage_page = HID_USAGE_PAGE_KEYBOARD,
        .keycode = keycode,
        .state = true,
        .timestamp = k_uptime_get()
    });

    if (!key_press) {
        LOG_ERR("Failed to create key press event");
        return -ENOMEM;
    }

    ret = ZMK_EVENT_RAISE(key_press);
    if (ret < 0) {
        LOG_ERR("Failed to raise key press: %d", ret);
        return ret;
    }

    // Schedule combination release
    k_work_schedule(release_work, K_MSEC(100));

    LOG_DBG("ZMK combo sent successfully");
    return 0;
}

// Public functions for trackpad gestures using ZMK events
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 KEY ***");
    send_zmk_keycode(HID_USAGE_KEY_KEYBOARD_F3, &f3_release_work);
}

void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 KEY ***");
    send_zmk_keycode(HID_USAGE_KEY_KEYBOARD_F4, &f4_release_work);
}

void send_trackpad_zoom_in(void) {
    LOG_INF("*** TRACKPAD ZOOM IN ***");
    send_zmk_combo(HID_USAGE_KEY_KEYBOARD_LEFT_CONTROL,
                   HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS,
                   &zoom_in_release_work);
}

void send_trackpad_zoom_out(void) {
    LOG_INF("*** TRACKPAD ZOOM OUT ***");
    send_zmk_combo(HID_USAGE_KEY_KEYBOARD_LEFT_CONTROL,
                   HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE,
                   &zoom_out_release_work);
}
