#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zephyr:kernel.h>
#include "gesture_handlers.h"

LOG_MODULE_DECLARE(azoteq_iqs5xx, CONFIG_ZMK_LOG_LEVEL);

// Debug mode - set to 1 to enable verbose two finger debugging
#define DEBUG_TWO_FINGER_MODE 1

void debug_two_finger_positions(const struct iqs5xx_rawdata *data, struct gesture_state *state) {
    #if DEBUG_TWO_FINGER_MODE
    if (data->finger_count == 2 && data->fingers[0].strength > 0 && data->fingers[1].strength > 0) {
        LOG_INF("=== TWO FINGER DEBUG ===");
        LOG_INF("Finger 0: (%d,%d) strength=%d",
                data->fingers[0].ax, data->fingers[0].ay, data->fingers[0].strength);
        LOG_INF("Finger 1: (%d,%d) strength=%d",
                data->fingers[1].ax, data->fingers[1].ay, data->fingers[1].strength);

        if (state->twoFingerActive) {
            // Calculate distances
            float dx0 = (float)(data->fingers[0].ax - state->twoFingerStartPos[0].x);
            float dy0 = (float)(data->fingers[0].ay - state->twoFingerStartPos[0].y);
            float dx1 = (float)(data->fingers[1].ax - state->twoFingerStartPos[1].x);
            float dy1 = (float)(data->fingers[1].ay - state->twoFingerStartPos[1].y);

            LOG_INF("Movement: F0=(%.1f,%.1f) F1=(%.1f,%.1f)",
                    (double)dx0, (double)dy0, (double)dx1, (double)dy1);

            // Calculate current distance
            float dx = (float)(data->fingers[1].ax - data->fingers[0].ax);
            float dy = (float)(data->fingers[1].ay - data->fingers[0].ay);
            float currentDist = sqrtf(dx*dx + dy*dy);

            // Calculate initial distance
            float idx = (float)(state->twoFingerStartPos[1].x - state->twoFingerStartPos[0].x);
            float idy = (float)(state->twoFingerStartPos[1].y - state->twoFingerStartPos[0].y);
            float initialDist = sqrtf(idx*idx + idy*idy);

            float distChange = currentDist - initialDist;

            LOG_INF("Distance: initial=%.1f, current=%.1f, change=%.1f",
                    (double)initialDist, (double)currentDist, (double)distChange);
            LOG_INF("Gestures: g0=0x%02x, g1=0x%02x, rx=%d, ry=%d",
                    data->gestures0, data->gestures1, data->rx, data->ry);
            LOG_INF("========================");
        }
    }
    #endif
}
