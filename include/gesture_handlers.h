#pragma once

#include <zephyr/device.h>
#include <zephyr/kernel.h>
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
        uint16_t x;
        uint16_t y;
    } threeFingerStartPos[3];

    // General state
    uint8_t lastFingerCount;
    uint8_t mouseSensitivity;
};

// Configuration constants
#define TRACKPAD_THREE_FINGER_CLICK_TIME    200
#define TRACKPAD_THREE_FINGER_SWIPE_MIN_DIST 50
#define SCROLL_REPORT_DISTANCE              15
#define MOVEMENT_THRESHOLD                  0.5f
#define ZOOM_THRESHOLD                      30
#define ZOOM_SENSITIVITY                    10

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

// Input event helper (defined in trackpad.c)
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync);
