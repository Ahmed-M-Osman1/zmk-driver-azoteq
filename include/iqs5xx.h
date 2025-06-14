//*****************************************************************************
//
//! The memory map registers, and bit definitions are defined in this header
//! file.  The access writes of the memory map are also indicated as follows:
//! (READ)          : Read only
//! (READ/WRITE)    : Read & Write
//! (READ/WRITE/E2) : Read, Write & default loaded from non-volatile memory
//
//*****************************************************************************
#pragma once

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/kernel.h>

// Hardware configuration
#define IQS5XX_DEFAULT_SENSITIVITY     128
#define IQS5XX_DEFAULT_REFRESH_ACTIVE  5
#define IQS5XX_DEFAULT_REFRESH_IDLE    20


#define AZOTEQ_IQS5XX_ADDR          0x74
#define IQS5XX_GESTURE_EVENTS0      0x000D
#define IQS5XX_END_WINDOW           0xEEEE
#define IQS5XX_MAX_FINGERS          5

// Performance tuning constants
#define IQS5XX_DEBOUNCE_MS          5     // Debounce time for touch events
#define IQS5XX_POLL_INTERVAL_MS     10    // Polling interval optimization
#define IQS5XX_GESTURE_TIMEOUT_MS   500   // Gesture recognition timeout

// Gesture event bit masks (from datasheet)
#define GESTURE_SINGLE_TAP          BIT(0)
#define GESTURE_PRESS_HOLD          BIT(1)
#define GESTURE_SWIPE_NEG_X         BIT(2)
#define GESTURE_SWIPE_POS_X         BIT(3)
#define GESTURE_SWIPE_NEG_Y         BIT(4)
#define GESTURE_SWIPE_POS_Y         BIT(5)

#define GESTURE_TWO_FINGER_TAP      BIT(0)  // In gestures1 register
#define GESTURE_SCROLL              BIT(1)
#define GESTURE_ZOOM                BIT(2)

// Performance optimization flags
#define IQS5XX_OPT_FAST_RESPONSE    BIT(0)  // Prioritize response time
#define IQS5XX_OPT_LOW_POWER        BIT(1)  // Optimize for power consumption
#define IQS5XX_OPT_HIGH_PRECISION   BIT(2)  // Enhanced precision mode

// Single finger data structure (optimized for performance)
struct iqs5xx_finger {
    uint16_t abs_x;         // Absolute X position
    uint16_t abs_y;         // Absolute Y position
    uint16_t strength;      // Touch strength (pressure)
    uint16_t area;          // Contact area
    bool active;            // Finger tracking state
    uint32_t timestamp;     // Last update timestamp
};

// Raw data from device (aligned for efficient memory access)
struct iqs5xx_rawdata {
    uint8_t gestures0;      // Single finger gestures
    uint8_t gestures1;      // Multi-finger gestures
    uint8_t system_info0;   // System status information
    uint8_t system_info1;   // Additional system info
    uint8_t finger_count;   // Active finger count
    int16_t rel_x;          // Relative X movement
    int16_t rel_y;          // Relative Y movement
    struct iqs5xx_finger fingers[IQS5XX_MAX_FINGERS];
    uint32_t timestamp;     // Data capture timestamp
} __packed;

// Gesture processing context
struct gesture_context {
    uint8_t last_gestures0;     // Previous gesture state
    uint8_t last_gestures1;     // Previous multi-finger state
    uint32_t gesture_start_time; // Gesture timing
    bool gesture_in_progress;    // Gesture detection state
};

// Performance metrics (for tuning)
struct iqs5xx_performance {
    uint32_t interrupt_count;   // Total interrupts processed
    uint32_t read_errors;       // I2C read error count
    uint32_t avg_process_time;  // Average processing time (μs)
    uint32_t max_process_time;  // Peak processing time (μs)
};

// Enhanced device data structure
struct azoteq_iqs5xx_data {
    const struct device *dev;
    struct gpio_callback gpio_cb;
    struct k_work work;

    // Performance optimization
    struct iqs5xx_rawdata current_data;
    struct iqs5xx_rawdata previous_data;
    struct gesture_context gesture_ctx;
    struct iqs5xx_performance perf_stats;

    // Configuration flags
    uint32_t optimization_flags;
    uint8_t sensitivity_level;      // 1-10 sensitivity scale
    bool mouse_mode_enabled;
    bool gesture_recognition_enabled;

    // Timing optimization
    struct k_timer debounce_timer;
    atomic_t processing_active;
};

// Enhanced configuration structure
struct azoteq_iqs5xx_config {
    struct i2c_dt_spec i2c_bus;
    struct gpio_dt_spec dr_gpio;       // Data ready GPIO
    struct gpio_dt_spec reset_gpio;    // Reset GPIO (optional)

    // Performance tuning parameters
    uint16_t poll_rate_hz;             // Polling rate (Hz)
    uint8_t gesture_sensitivity;       // Gesture detection sensitivity
    uint8_t movement_threshold;        // Minimum movement for mouse events
    bool enable_debug_output;          // Debug logging control
};

// Function declarations with enhanced error handling
int iqs5xx_init(const struct device *dev);
int iqs5xx_configure_performance(const struct device *dev, uint32_t flags);
int iqs5xx_set_sensitivity(const struct device *dev, uint8_t level);
int iqs5xx_get_performance_stats(const struct device *dev, struct iqs5xx_performance *stats);

// I2C communication functions (optimized)
int iqs5xx_read_registers(const struct device *dev, uint16_t start_addr,
                         uint8_t *buffer, uint32_t length);
int iqs5xx_write_registers(const struct device *dev, uint16_t start_addr,
                          const uint8_t *buffer, uint32_t length);
int iqs5xx_read_gesture_data(const struct device *dev, struct iqs5xx_rawdata *data);

// Gesture processing functions
void process_single_finger_gestures(struct azoteq_iqs5xx_data *data);
void process_multi_finger_gestures(struct azoteq_iqs5xx_data *data);
bool detect_gesture_start(const struct iqs5xx_rawdata *current,
                         const struct iqs5xx_rawdata *previous);

// Input event generation
void generate_mouse_events(const struct device *dev, const struct iqs5xx_rawdata *data);
void generate_scroll_events(const struct device *dev, int16_t scroll_x, int16_t scroll_y);
void generate_button_events(const struct device *dev, uint8_t button_state);

// Utility functions
static inline bool is_finger_active(const struct iqs5xx_finger *finger) {
    return finger->active && (finger->strength > 0);
}

static inline uint32_t calculate_distance(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    int32_t dx = x2 - x1;
    int32_t dy = y2 - y1;
    return dx * dx + dy * dy; // Square distance (avoid sqrt for performance)
}

int iqs5xx_device_setup(const struct device *dev);
int iqs5xx_enable_interrupts(const struct device *dev, bool enable);
void iqs5xx_debounce_timer_handler(struct k_timer *timer);
void iqs5xx_work_handler(struct k_work *work);
void iqs5xx_gpio_interrupt_handler(const struct device *port, struct gpio_callback *cb, uint32_t pins);
static void parse_gesture_data(const uint8_t *raw_buffer, struct iqs5xx_rawdata *data);
