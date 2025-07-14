#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "iqs5xx.h"
#include <stdlib.h>

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Apply coordinate transformations based on configuration - FIXED VERSION
struct coord_transform apply_coordinate_transform(int16_t x, int16_t y, const struct iqs5xx_config *config) {
    struct coord_transform result = {x, y};

    // Log the input and config for debugging
    if (abs(x) > 5 || abs(y) > 5) { // Only log significant movements
        LOG_DBG("Transform input: (%d,%d), rot90=%d, rot270=%d, inv_x=%d, inv_y=%d",
                x, y, config->rotate_90, config->rotate_270, config->invert_x, config->invert_y);
    }

    // Step 1: Apply rotation first - FIXED rotation logic
    if (config->rotate_90) {
        // 90° clockwise: (x,y) -> (y,-x)
        int16_t temp = result.x;
        result.x = result.y;
        result.y = -temp;

        if (abs(x) > 5 || abs(y) > 5) {
            LOG_DBG("Applied 90° rotation: (%d,%d) -> (%d,%d)", x, y, result.x, result.y);
        }
    } else if (config->rotate_180) {
        // 180°: (x,y) -> (-x,-y)
        result.x = -result.x;
        result.y = -result.y;

        if (abs(x) > 5 || abs(y) > 5) {
            LOG_DBG("Applied 180° rotation: (%d,%d) -> (%d,%d)", x, y, result.x, result.y);
        }
    } else if (config->rotate_270) {
        // 270° clockwise: (x,y) -> (-y,x)
        int16_t temp = result.x;
        result.x = -result.y;
        result.y = temp;

        if (abs(x) > 5 || abs(y) > 5) {
            LOG_DBG("Applied 270° rotation: (%d,%d) -> (%d,%d)", x, y, result.x, result.y);
        }
    }

    // Step 2: Apply axis inversion after rotation
    if (config->invert_x) {
        result.x = -result.x;

        if (abs(x) > 5 || abs(y) > 5) {
            LOG_DBG("Applied X inversion: x=%d -> x=%d", -result.x, result.x);
        }
    }

    if (config->invert_y) {
        result.y = -result.y;

        if (abs(x) > 5 || abs(y) > 5) {
            LOG_DBG("Applied Y inversion: y=%d -> y=%d", -result.y, result.y);
        }
    }

    // Log final result for debugging
    if (abs(x) > 5 || abs(y) > 5) {
        LOG_DBG("Transform final: (%d,%d) -> (%d,%d)", x, y, result.x, result.y);
    }

    return result;
}

// Apply transformation to finger absolute coordinates - IMPROVED
void apply_finger_transform(struct iqs5xx_finger *finger, const struct iqs5xx_config *config) {
    if (finger->strength == 0) return; // Skip invalid fingers

    uint16_t orig_x = finger->ax;
    uint16_t orig_y = finger->ay;

    // Convert to signed for transformation
    int16_t x = (int16_t)orig_x;
    int16_t y = (int16_t)orig_y;

    // Apply transformation
    struct coord_transform transformed = apply_coordinate_transform(x, y, config);

    // Convert back to unsigned, handling potential negative values
    finger->ax = (uint16_t)MAX(0, transformed.x);
    finger->ay = (uint16_t)MAX(0, transformed.y);

    // Log finger transformation for debugging
    if (finger->strength > 1000) {
        LOG_DBG("Finger transform: (%d,%d) -> (%d,%d), strength=%d",
                orig_x, orig_y, finger->ax, finger->ay, finger->strength);
    }
}
