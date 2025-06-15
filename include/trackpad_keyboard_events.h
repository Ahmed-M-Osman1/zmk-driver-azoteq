// include/trackpad_keyboard_events.h - Updated for ZMK
#pragma once

#include <zephyr/device.h>

// Initialize the keyboard events system
int trackpad_keyboard_init(const struct device *input_dev);

// Function declarations for trackpad keyboard events using ZMK's event system
void send_trackpad_f3(void);
void send_trackpad_f4(void);
void send_trackpad_zoom_in(void);
void send_trackpad_zoom_out(void);
