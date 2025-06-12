/**
 * @file trackpad_config.h
 * @brief Configuration options for enhanced IQS5XX trackpad gestures
 */

/**
 * Gesture Priority Order (higher number = higher priority):
 * 1. Three-finger gestures (Mission Control, desktop switching)
 * 2. Circular scroll (rim area)
 * 3. Two-finger scroll
 * 4. Tap detection
 * 5. Cursor movement
 * 6. Inertial cursor
 */

#pragma once

// ===== GESTURE ENABLE/DISABLE =====
#define ENABLE_TAP_DETECTION                1
#define ENABLE_CIRCULAR_SCROLL              1
#define ENABLE_INERTIAL_CURSOR              1
#define ENABLE_THREE_FINGER_GESTURES        1
#define ENABLE_TWO_FINGER_SCROLL            1

// ===== TRACKPAD DIMENSIONS =====
// Adjust these to match your IQS5XX configuration
#define TRACKPAD_WIDTH                      1024
#define TRACKPAD_HEIGHT                     1024

// ===== SENSITIVITY SETTINGS =====
#define DEFAULT_MOUSE_SENSITIVITY           128     // 128 = normal, 64 = slower, 256 = faster
#define CIRCULAR_SCROLL_RIM_PERCENT         15      // Outer 15% of trackpad for circular scroll
#define CIRCULAR_SCROLL_SENSITIVITY         15.0f   // Degrees per scroll step (lower = more sensitive)

// ===== TIMING SETTINGS =====
#define TAP_TIMEOUT_MS                      150     // Max time for tap detection
#define THREE_FINGER_CLICK_TIME             300     // Max time for 3-finger tap vs swipe

// ===== MOVEMENT THRESHOLDS =====
#define THREE_FINGER_SWIPE_THRESHOLD        100     // Distance required for 3-finger swipe
#define TAP_MOVEMENT_THRESHOLD              50      // Max movement allowed during tap
#define INERTIAL_THRESHOLD                  20      // Minimum speed for momentum cursor
#define SCROLL_REPORT_DISTANCE              35      // Distance for 2-finger scroll step

// ===== INERTIAL CURSOR SETTINGS =====
#define INERTIAL_DECAY_PERCENT              25      // How quickly momentum fades (higher = faster fade)
#define INERTIAL_UPDATE_INTERVAL            50      // Update interval in ms

// ===== KEYBOARD SHORTCUTS =====
// Customize these based on your OS and preferences

// macOS Mission Control (default: F3)
#define MISSION_CONTROL_KEY                 INPUT_KEY_F3

// Desktop switching keys (default: Ctrl+Arrow)
#define DESKTOP_MODIFIER_KEY                INPUT_KEY_LEFTCTRL
#define DESKTOP_PREV_KEY                    INPUT_KEY_LEFT
#define DESKTOP_NEXT_KEY                    INPUT_KEY_RIGHT

// Alternative Mission Control options (uncomment to use):
// #define MISSION_CONTROL_USE_CMD_F3       // Send Cmd+F3 instead of F3
// #define MISSION_CONTROL_CUSTOM_KEY       INPUT_KEY_F4  // Use different key

// ===== ADVANCED SETTINGS =====
#define CIRCULAR_SCROLL_MIN_ANGLE           5.0f    // Minimum angle change for scroll
#define GESTURE_LOG_LEVEL                   LOG_LEVEL_DBG  // Set to LOG_LEVEL_INF for less verbose

// ===== FEATURE FLAGS =====
// Enable/disable specific gesture combinations
#define ALLOW_CIRCULAR_SCROLL_IN_CORNER     1       // Allow circular scroll to start near corners
#define SUPPRESS_CURSOR_DURING_GESTURES     1       // Prevent cursor movement during multi-finger gestures
#define ENHANCED_FINGER_DETECTION           1       // Use improved finger counting algorithm

// ===== DEBUGGING =====
#define ENABLE_GESTURE_LOGGING              1       // Enable detailed gesture logging
#define LOG_TOUCH_COORDINATES               0       // Log all touch coordinates (very verbose)
#define LOG_MOVEMENT_DELTAS                 0       // Log movement calculations

// ===== COMPATIBILITY SETTINGS =====
// Adjust these if you have issues with specific gestures
#define FALLBACK_TO_BASIC_GESTURES          0       // Use only basic IQS5XX gestures if enhanced ones fail
#define GESTURE_RETRY_ON_FAILURE            1       // Retry gesture detection on recognition failure

// ===== VALIDATION =====
#if CIRCULAR_SCROLL_RIM_PERCENT > 50
#error "CIRCULAR_SCROLL_RIM_PERCENT cannot be greater than 50%"
#endif

#if TAP_TIMEOUT_MS > 1000
#warning "TAP_TIMEOUT_MS is quite high, consider reducing it"
#endif

#if INERTIAL_DECAY_PERCENT > 90
#warning "INERTIAL_DECAY_PERCENT is very high, momentum will fade very quickly"
#endif
