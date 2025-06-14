#define DT_DRV_COMPAT azoteq_iqs5xx

#include "iqs5xx_enhanced.h"
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(iqs5xx_driver, CONFIG_ZMK_LOG_LEVEL);

// Performance optimization: Use atomic operations for thread safety
static atomic_t driver_initialized = ATOMIC_INIT(0);

/**
 * Enhanced initialization with performance optimizations
 */
int iqs5xx_init(const struct device *dev) {
    LOG_INF("Initializing IQS5xx trackpad driver");

    struct azoteq_iqs5xx_data *data = dev->data;
    const struct azoteq_iqs5xx_config *config = dev->config;
    int ret;

    // Prevent multiple initialization
    if (!atomic_cas(&driver_initialized, 0, 1)) {
        LOG_WRN("Driver already initialized");
        return -EALREADY;
    }

    // Validate I2C bus availability
    if (!device_is_ready(config->i2c_bus.bus)) {
        LOG_ERR("I2C bus not ready");
        atomic_set(&driver_initialized, 0);
        return -ENODEV;
    }

    // Initialize device data structure
    data->dev = dev;
    data->optimization_flags = IQS5XX_OPT_FAST_RESPONSE; // Default optimization
    data->sensitivity_level = 5; // Medium sensitivity
    data->mouse_mode_enabled = true;
    data->gesture_recognition_enabled = true;
    atomic_set(&data->processing_active, 0);

    // Configure GPIO pins with proper error handling
    ret = gpio_pin_configure_dt(&config->dr_gpio, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure data ready GPIO: %d", ret);
        goto init_error;
    }

    // Initialize GPIO callback for interrupt handling
    gpio_init_callback(&data->gpio_cb, iqs5xx_gpio_interrupt_handler,
                      BIT(config->dr_gpio.pin));

    ret = gpio_add_callback(config->dr_gpio.port, &data->gpio_cb);
    if (ret < 0) {
        LOG_ERR("Failed to add GPIO callback: %d", ret);
        goto init_error;
    }

    // Initialize work queue for deferred processing
    k_work_init(&data->work, iqs5xx_work_handler);

    // Initialize debounce timer for performance optimization
    k_timer_init(&data->debounce_timer, iqs5xx_debounce_timer_handler, NULL);

    // Perform device-specific initialization
    ret = iqs5xx_device_setup(dev);
    if (ret < 0) {
        LOG_ERR("Device setup failed: %d", ret);
        goto init_error;
    }

    // Enable interrupts
    ret = iqs5xx_enable_interrupts(dev, true);
    if (ret < 0) {
        LOG_ERR("Failed to enable interrupts: %d", ret);
        goto init_error;
    }

    LOG_INF("IQS5xx driver initialized successfully");
    return 0;

init_error:
    atomic_set(&driver_initialized, 0);
    return ret;
}

/**
 * Optimized interrupt handler - minimal processing in ISR context
 */
static void iqs5xx_gpio_interrupt_handler(const struct device *port,
                                         struct gpio_callback *cb,
                                         uint32_t pins) {
    struct azoteq_iqs5xx_data *data = CONTAINER_OF(cb, struct azoteq_iqs5xx_data, gpio_cb);

    // Performance optimization: Only submit work if not already processing
    if (atomic_cas(&data->processing_active, 0, 1)) {
        k_work_submit(&data->work);
        data->perf_stats.interrupt_count++;
    }
}

/**
 * Enhanced work handler with performance monitoring
 */
static void iqs5xx_work_handler(struct k_work *work) {
    struct azoteq_iqs5xx_data *data = CONTAINER_OF(work, struct azoteq_iqs5xx_data, work);
    uint32_t start_time = k_cycle_get_32();
    int ret;

    // Read gesture data from device
    ret = iqs5xx_read_gesture_data(data->dev, &data->current_data);
    if (ret < 0) {
        LOG_ERR("Failed to read gesture data: %d", ret);
        data->perf_stats.read_errors++;
        goto work_complete;
    }

    // Process gestures based on configuration
    if (data->gesture_recognition_enabled) {
        process_single_finger_gestures(data);
        process_multi_finger_gestures(data);
    }

    // Generate input events
    if (data->mouse_mode_enabled) {
        generate_mouse_events(data->dev, &data->current_data);
    }

    // Update performance statistics
    uint32_t process_time = k_cyc_to_us_ceil32(k_cycle_get_32() - start_time);
    data->perf_stats.avg_process_time =
        (data->perf_stats.avg_process_time + process_time) / 2;
    if (process_time > data->perf_stats.max_process_time) {
        data->perf_stats.max_process_time = process_time;
    }

    // Store current data as previous for next comparison
    memcpy(&data->previous_data, &data->current_data, sizeof(struct iqs5xx_rawdata));

work_complete:
    atomic_set(&data->processing_active, 0);
}

// Device instance macro with enhanced configuration
#define IQS5XX_DEVICE_INSTANCE(n)                                              \
    static struct azoteq_iqs5xx_data iqs5xx_data_##n;                         \
    static const struct azoteq_iqs5xx_config iqs5xx_config_##n = {            \
        .i2c_bus = I2C_DT_SPEC_INST_GET(n),                                  \
        .dr_gpio = GPIO_DT_SPEC_INST_GET(n, dr_gpios),                       \
        .reset_gpio = GPIO_DT_SPEC_INST_GET_OR(n, reset_gpios, {}),          \
        .poll_rate_hz = DT_INST_PROP_OR(n, poll_rate_hz, 100),               \
        .gesture_sensitivity = DT_INST_PROP_OR(n, gesture_sensitivity, 5),    \
        .movement_threshold = DT_INST_PROP_OR(n, movement_threshold, 2),      \
        .enable_debug_output = DT_INST_PROP_OR(n, enable_debug, false),      \
    };                                                                         \
    DEVICE_DT_INST_DEFINE(n, iqs5xx_init, NULL,                              \
                          &iqs5xx_data_##n, &iqs5xx_config_##n,              \
                          POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(IQS5XX_DEVICE_INSTANCE)
