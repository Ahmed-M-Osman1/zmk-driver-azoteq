#pragma once

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <math.h>
#include <string.h>
#include "iqs5xx.h"

// Common gesture state and configuration
struct gesture_state {
    // Accumulated position for movement
    struct {
        float x;
        float y;
    } accumPos;

    // Single finger / drag state
    bool isDragging;
    bool dragStartSent;

    // Two finger state
    bool twoFingerActive;
    int16_t lastXScrollReport;
    int64_t twoFingerStartTime;
    struct {
        uint16_t x;
        uint16_t y;
    } twoFingerStartPos[2];

    // Three finger state
    bool threeFingersPressed;
    int64_t threeFingerPressTime;
    struct {
        int16_t x; // Changed from uint16_t to int16_t
        int16_t y; // Changed from uint16_t to int16_t
    } threeFingerStartPos[3];
    bool gestureTriggered; // Added to track if a gesture was triggered

    // General state
    uint8_t lastFingerCount;
    uint8_t mouseSensitivity;
};

// Configuration constants
#define TRACKPAD_THREE_FINGER_CLICK_TIME    200
#define TRACKPAD_THREE_FINGER_SWIPE_MIN_DIST 30   // Reduced from 50
#define SCROLL_REPORT_DISTANCE              15
#define MOVEMENT_THRESHOLD                  0.5f
#define ZOOM_THRESHOLD                      50    // Increased from 20 to prevent multiple triggers
#define ZOOM_SENSITIVITY                    25    // Increased from 15

// Function declarations for each gesture handler file

// single_finger.c
void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_single_finger_state(struct gesture_state *state);

// two_finger.c
void handle_two_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_two_finger_state(struct gesture_state *state);

// three_finger.c
void handle_three_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_three_finger_state(struct gesture_state *state);

// Input event helper (defined in trackpad.c) - for mouse events
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync);

// NEW: Keyboard event helpers using input events (defined in trackpad.c)
void send_keyboard_key(uint16_t keycode);
void send_keyboard_combo(uint16_t modifier, uint16_t keycode);

// Debug helpers (defined in debug_helpers.c)
void debug_two_finger_positions(const struct iqs5xx_rawdata *data, struct gesture_state *state);
