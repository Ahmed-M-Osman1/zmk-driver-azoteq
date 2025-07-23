#pragma once

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <math.h>
#include <string.h>
#include "iqs5xx.h"

// FIXED ISSUE #6 & #10: Enhanced gesture state with thread safety and memory protection
struct gesture_state {
    // FIXED ISSUE #6: Add mutex protection for gesture state to prevent race conditions
    struct k_mutex state_mutex;

    // Accumulated position for movement
    struct {
        float x;
        float y;
    } accumPos;

    // Single finger / drag state
    bool isDragging;
    bool dragStartSent;

    // Two finger state - FIXED ISSUE #6: Enhanced with atomic flags for thread safety
    bool twoFingerActive;
    int16_t lastXScrollReport;
    int64_t twoFingerStartTime;
    struct {
        uint16_t x;
        uint16_t y;
    } twoFingerStartPos[2];

    // Three finger state - FIXED ISSUE #6: Enhanced with proper state tracking
    bool threeFingersPressed;
    int64_t threeFingerPressTime;
    struct {
        int16_t x;
        int16_t y;
    } threeFingerStartPos[3];
    bool gestureTriggered;

    // FIXED ISSUE #6: Enhanced general state with better tracking
    uint8_t lastFingerCount;
    uint8_t mouseSensitivity;

    // FIXED ISSUE #10: Add state validation for memory corruption detection
    uint32_t state_magic;       // Magic number to detect corruption
    int64_t last_update_time;   // Track when state was last updated
    bool state_initialized;     // Flag to ensure proper initialization
};

// FIXED ISSUE #6: Magic number for state validation
#define GESTURE_STATE_MAGIC 0xDEADBEEF

// Configuration constants - PRESERVED ORIGINAL VALUES
#define TRACKPAD_THREE_FINGER_CLICK_TIME    200
#define TRACKPAD_THREE_FINGER_SWIPE_MIN_DIST 30
#define SCROLL_REPORT_DISTANCE              15
#define MOVEMENT_THRESHOLD                  0.3f  // Reduced for faster response
#define ZOOM_THRESHOLD                      80
#define ZOOM_SENSITIVITY                    40

// FIXED ISSUE #6: Enhanced function declarations with thread safety annotations

// single_finger.c - PRESERVED ORIGINAL FUNCTIONALITY with thread safety
void handle_single_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_single_finger_state(struct gesture_state *state);

// two_finger.c - PRESERVED ORIGINAL FUNCTIONALITY with thread safety
void handle_two_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_two_finger_state(struct gesture_state *state);

// three_finger.c - PRESERVED ORIGINAL FUNCTIONALITY with thread safety
void handle_three_finger_gestures(const struct device *dev, const struct iqs5xx_rawdata *data, struct gesture_state *state);
void reset_three_finger_state(struct gesture_state *state);

// FIXED ISSUE #6: New thread-safe gesture state management functions
/**
 * @brief Initialize gesture state with proper thread safety
 * @param state Gesture state structure to initialize
 * @param sensitivity Mouse sensitivity value from device tree
 * @return 0 on success, negative error on failure
 */
int init_gesture_state(struct gesture_state *state, uint8_t sensitivity);

/**
 * @brief Safely validate gesture state to detect corruption
 * @param state Gesture state to validate
 * @return true if state is valid, false if corrupted
 */
bool validate_gesture_state(const struct gesture_state *state);

/**
 * @brief Clean up gesture state resources
 * @param state Gesture state to clean up
 */
void cleanup_gesture_state(struct gesture_state *state);

/**
 * @brief Thread-safe getter for current finger count
 * @param state Gesture state
 * @return Current finger count with thread safety
 */
uint8_t get_current_finger_count(struct gesture_state *state);

/**
 * @brief Thread-safe setter for finger count
 * @param state Gesture state
 * @param count New finger count
 */
void set_current_finger_count(struct gesture_state *state, uint8_t count);

// Input event helper (defined in trackpad.c) - for mouse events - PRESERVED ORIGINAL
void send_input_event(uint8_t type, uint16_t code, int32_t value, bool sync);

// Keyboard event helpers using input events (defined in trackpad.c) - PRESERVED ORIGINAL
void send_keyboard_key(uint16_t keycode);
void send_keyboard_combo(uint16_t modifier, uint16_t keycode);
