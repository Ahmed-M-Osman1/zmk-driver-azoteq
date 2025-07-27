#pragma once

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <math.h>
#include <string.h>
#include "iqs5xx.h"

// MINIMAL FIX: Keep original gesture state but add basic thread safety fields
struct gesture_state {
    // MINIMAL FIX: Add basic mutex for thread safety (required by trackpad.c)
    struct k_mutex state_mutex;

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

    // General state
    uint8_t lastFingerCount;
    uint8_t mouseSensitivity;

    // MINIMAL FIX: Add validation fields for memory corruption detection (required by headers)
    uint32_t state_magic;       // Magic number to detect corruption
    int64_t last_update_time;   // Track when state was last updated
    bool state_initialized;     // Flag to ensure proper initialization
};

// MINIMAL FIX: Magic number for state validation (required by trackpad.c)
#define GESTURE_STATE_MAGIC 0xDEADBEEF

// Configuration constants - PRESERVED ORIGINAL VALUES
#define TRACKPAD_THREE_FINGER_CLICK_TIME    200
#define TRACKPAD_THREE_FINGER_SWIPE_MIN_DIST 30
#define SCROLL_REPORT_DISTANCE              15
#define MOVEMENT_THRESHOLD                  0.3f  // Reduced for faster response
#define ZOOM_THRESHOLD                      80
#define ZOOM_SENSITIVITY                    40

// PRESERVED ORIGINAL: Function declarations unchanged
void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_single_finger_state(struct gesture_state *state);

void handle_two_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_two_finger_state(struct gesture_state *state);

void handle_three_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_three_finger_state(struct gesture_state *state);

// MINIMAL FIX: Add required function declarations (implemented in trackpad.c)
int init_gesture_state(struct gesture_state *state, uint8_t sensitivity);
bool validate_gesture_state(const struct gesture_state *state);
void cleanup_gesture_state(struct gesture_state *state);
uint8_t get_current_finger_count(struct gesture_state *state);
void set_current_finger_count(struct gesture_state *state, uint8_t count);

// Input event helper (defined in trackpad.c) - for mouse events - PRESERVED ORIGINAL
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync);

// Keyboard event helpers using input events (defined in trackpad.c) - PRESERVED ORIGINAL
void send_keyboard_key(uint16_t keycode);
void send_keyboard_combo(uint16_t modifier, uint16_t keycode);
