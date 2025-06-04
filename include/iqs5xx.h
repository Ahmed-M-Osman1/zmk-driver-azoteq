// Dependencies
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#define AZOTEQ_IQS5XX_ADDR      0b1110100
#define GestureEvents0_adr		0x000D	//(READ)
#define	END_WINDOW				(uint16_t)0xEEEE

// Single finger data
struct iqs5xx_finger {
    // Absolute Y position
    uint16_t ax;
    // Absolute X position
    uint16_t ay;
    // Touch strength
    uint16_t strength;
    // Touch area
    uint16_t area;
};

// Data read from the device
struct iqs5xx_rawdata {
    // Gesture events 0: Single tap, press and hold, swipe -x, swipe +x, swipe -y, swipe +y
    uint8_t gestures0;
    // Gesture events 1: 2 finger tap, scroll, zoom
    uint8_t gestures1;
    // System info 0
    uint8_t system_info0;
    // System info 1
    uint8_t system_info1;
    // Number of fingers
    uint8_t finger_count;
    // Relative X position
    int16_t rx;
    // Relative Y position
    int16_t ry;
    // Fingers
    struct iqs5xx_finger fingers[5];
};


// Instance-Unique Data Struct (Optional)
struct azoteq_iqs5xx_data {
    const struct device *dev;
    struct gpio_callback gpio_cb;
    struct k_work work;
};

// Instance-Unique Config Struct (Optional)
struct azoteq_iqs5xx_config {
    const struct i2c_dt_spec i2c_bus;
    const struct gpio_dt_spec dr;
};

// Initialization Function
static int azoteq_iqs5xx_init(const struct device *dev);
static void azoteq_iqs5xx_gpio_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins);
static void azoteq_iqs5xx_work_cb(struct k_work *work);
//static void azoteq_iqs5xx_report_data(const struct device *dev);
static void azoteq_iqs5xx_report_data(const struct azoteq_iqs5xx_data *data);
static int azoteq_iqs5xx_set_int(const struct device *dev, const bool en);
