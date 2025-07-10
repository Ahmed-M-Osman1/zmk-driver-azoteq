#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "iqs5xx.h"

// Apply coordinate transformations based on configuration
struct coord_transform apply_coordinate_transform(int16_t x, int16_t y, const struct iqs5xx_config *config) {
    struct coord_transform result = {x, y};

    // Step 1: Apply rotation first
    if (config->rotate_90) {
        // 90° clockwise: (x,y) -> (y,-x)
        int16_t temp = result.x;
        result.x = result.y;
        result.y = -temp;
    } else if (config->rotate_180) {
        // 180°: (x,y) -> (-x,-y)
        result.x = -result.x;
        result.y = -result.y;
        LOG_DBG("Applied 180° rotation: (%d,%d) -> (%d,%d)", x, y, result.x, result.y);
    } else if (config->rotate_270) {
        // 270° clockwise: (x,y) -> (-y,x)
        int16_t temp = result.x;
        result.x = -result.y;
        result.y = temp;
    }

    if (config->invert_x) {
        result.x = -result.x;
    }

    if (config->invert_y) {
        result.y = -result.y;
    }

    return result;
}

// Apply transformation to finger absolute coordinates
void apply_finger_transform(struct iqs5xx_finger *finger, const struct iqs5xx_config *config) {
    if (finger->strength == 0) return; // Skip invalid fingers

    uint16_t orig_x = finger->ax;
    uint16_t orig_y = finger->ay;

    // Convert to signed for transformation
    int16_t x = (int16_t)orig_x;
    int16_t y = (int16_t)orig_y;

    // Apply transformation
    struct coord_transform transformed = apply_coordinate_transform(x, y, config);

    finger->ax = (uint16_t)transformed.x;
    finger->ay = (uint16_t)transformed.y;

}
