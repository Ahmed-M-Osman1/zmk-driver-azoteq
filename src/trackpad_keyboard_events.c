// src/trackpad_keyboard_events.c - Simplified approach using behavior system
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/matrix.h>
#include <zmk/endpoints.h>

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Work items for key releases
static struct k_work_delayable f3_release_work;
static struct k_work_delayable f4_release_work;

// Work callbacks for key releases
static void f3_release_cb(struct k_work *work) {
    LOG_INF("F3 key release");
    // Use zmk behavior queue to send F3 key release
    struct zmk_behavior_binding binding = {
        .behavior_dev = "kp",
        .param1 = 60, // F3 keycode
        .param2 = 0
    };
    zmk_behavior_queue_add(binding, false, k_uptime_get());
}

static void f4_release_cb(struct k_work *work) {
    LOG_INF("F4 key release");
    // Use zmk behavior queue to send F4 key release
    struct zmk_behavior_binding binding = {
        .behavior_dev = "kp",
        .param1 = 61, // F4 keycode
        .param2 = 0
    };
    zmk_behavior_queue_add(binding, false, k_uptime_get());
}

// Initialize the keyboard events system
int trackpad_keyboard_init(const struct device *input_dev) {
    // Initialize work items for key releases
    k_work_init_delayable(&f3_release_work, f3_release_cb);
    k_work_init_delayable(&f4_release_work, f4_release_cb);

    LOG_INF("ZMK trackpad keyboard events initialized");
    return 0;
}

// Send a single keycode through ZMK's behavior system
static int send_zmk_keycode_simple(uint8_t keycode, struct k_work_delayable *release_work) {
    LOG_INF("Sending ZMK keycode: %d", keycode);

    // Use zmk behavior queue to send key press
    struct zmk_behavior_binding binding = {
        .behavior_dev = "kp",
        .param1 = keycode,
        .param2 = 0
    };

    int ret = zmk_behavior_queue_add(binding, true, k_uptime_get());
    if (ret < 0) {
        LOG_ERR("Failed to queue key press: %d", ret);
        return ret;
    }

    // Schedule key release
    k_work_schedule(release_work, K_MSEC(50));

    LOG_DBG("ZMK keycode sent successfully");
    return 0;
}

// Public functions for trackpad gestures using ZMK behavior system
void send_trackpad_f3(void) {
    LOG_INF("*** TRACKPAD F3 KEY ***");
    send_zmk_keycode_simple(60, &f3_release_work); // F3 = 60 in ZMK
}

void send_trackpad_f4(void) {
    LOG_INF("*** TRACKPAD F4 KEY ***");
    send_zmk_keycode_simple(61, &f4_release_work); // F4 = 61 in ZMK
}

void send_trackpad_zoom_in(void) {
    LOG_INF("*** TRACKPAD ZOOM IN ***");
    // Send Ctrl+Plus combination
    struct zmk_behavior_binding ctrl_binding = {
        .behavior_dev = "kp",
        .param1 = 224, // LEFT_CONTROL
        .param2 = 0
    };
    zmk_behavior_queue_add(ctrl_binding, true, k_uptime_get());

    k_msleep(10);

    struct zmk_behavior_binding plus_binding = {
        .behavior_dev = "kp",
        .param1 = 46, // EQUAL/PLUS key
        .param2 = 0
    };
    zmk_behavior_queue_add(plus_binding, true, k_uptime_get());

    k_msleep(50);

    zmk_behavior_queue_add(plus_binding, false, k_uptime_get());
    zmk_behavior_queue_add(ctrl_binding, false, k_uptime_get());
}

void send_trackpad_zoom_out(void) {
    LOG_INF("*** TRACKPAD ZOOM OUT ***");
    // Send Ctrl+Minus combination
    struct zmk_behavior_binding ctrl_binding = {
        .behavior_dev = "kp",
        .param1 = 224, // LEFT_CONTROL
        .param2 = 0
    };
    zmk_behavior_queue_add(ctrl_binding, true, k_uptime_get());

    k_msleep(10);

    struct zmk_behavior_binding minus_binding = {
        .behavior_dev = "kp",
        .param1 = 45, // MINUS key
        .param2 = 0
    };
    zmk_behavior_queue_add(minus_binding, true, k_uptime_get());

    k_msleep(50);

    zmk_behavior_queue_add(minus_binding, false, k_uptime_get());
    zmk_behavior_queue_add(ctrl_binding, false, k_uptime_get());
}
