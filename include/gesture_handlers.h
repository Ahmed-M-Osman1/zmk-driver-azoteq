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
        int16_t x;
        int16_t y;
    } threeFingerStartPos[3];
    bool gestureTriggered;

    // Edge continuation state
    bool edgeContinuationActive;
    int64_t edgeContinuationStartTime;
    struct {
        float x;
        float y;
    } lastMovementDirection;
    float continuationVelocity;

    // General state
    uint8_t lastFingerCount;
    uint8_t mouseSensitivity;
};

// Configuration constants
#define TRACKPAD_THREE_FINGER_CLICK_TIME    200
#define TRACKPAD_THREE_FINGER_SWIPE_MIN_DIST 30
#define SCROLL_REPORT_DISTANCE              15
#define MOVEMENT_THRESHOLD                  0.3f  // Reduced for faster response
#define ZOOM_THRESHOLD                      80
#define ZOOM_SENSITIVITY                    40

// Edge continuation constants  
#define TRACKPAD_MAX_X                      1530  // Typical trackpad X resolution
#define TRACKPAD_MAX_Y                      1018  // Typical trackpad Y resolution  
#define EDGE_THRESHOLD                      50    // Pixels from edge to trigger continuation
#define CONTINUATION_DURATION_MS            500   // How long to continue movement
#define CONTINUATION_DECAY_FACTOR           0.95f // Velocity decay per update
#define CONTINUATION_UPDATE_INTERVAL_MS     16    // Update every 16ms (~60fps)

// Function declarations for each gesture handler file

// single_finger.c
void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_single_finger_state(struct gesture_state *state);
void update_edge_continuation(struct gesture_state *state);

// two_finger.c
void handle_two_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_two_finger_state(struct gesture_state *state);

// three_finger.c
void handle_three_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_three_finger_state(struct gesture_state *state);

// Input event helper (defined in trackpad.c) - for mouse events
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync);

// Keyboard event helpers using input events (defined in trackpad.c)
void send_keyboard_key(uint16_t keycode);
void send_keyboard_combo(uint16_t modifier, uint16_t keycode);
