#pragma once

// Gesture recognition thresholds (tunable for performance)
#define SWIPE_MIN_DISTANCE          50      // Minimum pixels for swipe detection
#define SWIPE_MAX_TIME_MS           300     // Maximum time for swipe gesture
#define TAP_MAX_DISTANCE            10      // Maximum movement for tap detection
#define TAP_MAX_TIME_MS             200     // Maximum time for tap
#define HOLD_MIN_TIME_MS            500     // Minimum time for press-and-hold

#define ZOOM_GESTURE_THRESHOLD      20      // Minimum distance change for zoom
#define SCROLL_GESTURE_THRESHOLD    5       // Minimum movement for scroll

// Gesture state machine states
enum gesture_state {
    GESTURE_IDLE,
    GESTURE_DETECTING,
    GESTURE_CONFIRMED,
    GESTURE_COMPLETED
};

// Common gesture data structure
struct gesture_event {
    uint8_t type;           // Gesture type (from bit masks above)
    uint16_t start_x, start_y;  // Gesture start coordinates
    uint16_t end_x, end_y;      // Gesture end coordinates
    uint32_t duration_ms;       // Gesture duration
    uint8_t finger_count;       // Number of fingers involved
    enum gesture_state state;   // Current gesture state
};

// Function prototypes for gesture modules
bool validate_gesture_timing(uint32_t start_time, uint32_t max_duration);
bool validate_gesture_distance(uint16_t start_x, uint16_t start_y,
                              uint16_t end_x, uint16_t end_y, uint16_t min_distance);
void reset_gesture_context(struct gesture_context *ctx);
