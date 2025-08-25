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
    
    // Mac-style drag lock state
    bool dragLockActive;           // True when first finger is "locked" in drag mode
    bool dragLockButtonSent;       // True when button press was sent for drag lock
    int64_t dragLockStartTime;     // When drag lock was initiated
    uint16_t dragLockStartX;       // Initial position when drag lock started
    uint16_t dragLockStartY;
    bool secondFingerMoving;       // True when second finger is providing movement
    uint8_t dragLockFingerID;      // ID of the finger that initiated drag lock
    
    // Movement state for second finger during drag lock
    struct {
        float x;
        float y;
    } dragLockAccumPos;

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

// Mac-style drag lock constants
#define DRAG_LOCK_HOLD_TIME_MS             300    // Time to hold finger before drag lock activates
#define DRAG_LOCK_MAX_MOVEMENT_PX          50     // Maximum movement allowed during hold time
#define DRAG_LOCK_MOVEMENT_THRESHOLD       0.5f   // Movement threshold for second finger during drag lock

// Function declarations for each gesture handler file

// single_finger.c
void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_single_finger_state(struct gesture_state *state);
void handle_drag_lock_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_drag_lock_state(struct gesture_state *state);

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
