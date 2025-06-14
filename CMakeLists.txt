zephyr_library()
zephyr_include_directories(include)

# Core driver (your enhanced iqs5xx.c)
zephyr_library_sources(src/iqs5xx.c)

# Optional gesture processing modules (conditional compilation)
if(CONFIG_IQS5XX_GESTURE_PROCESSING)
    zephyr_library_sources_ifdef(CONFIG_IQS5XX_GESTURE_PROCESSING src/gestures/one_finger.c)
    zephyr_library_sources_ifdef(CONFIG_IQS5XX_GESTURE_PROCESSING src/gestures/two_finger.c)
    zephyr_library_sources_ifdef(CONFIG_IQS5XX_GESTURE_PROCESSING src/gestures/gesture_common.c)
endif()

# Performance optimization utilities
if(CONFIG_IQS5XX_PERFORMANCE_OPTIMIZATION)
    zephyr_library_sources_ifdef(CONFIG_IQS5XX_PERFORMANCE_OPTIMIZATION src/utils/iqs5xx_i2c.c)
    zephyr_library_sources_ifdef(CONFIG_IQS5XX_PERFORMANCE_OPTIMIZATION src/input/mouse_handler.c)
endif()

# Debug statistics (optional)
zephyr_library_sources_ifdef(CONFIG_IQS5XX_DEBUG_STATS src/utils/performance_monitor.c)
