/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#define PERIPH_GESTURE_VEL_WINDOW 5
#define PERIPH_GESTURE_ANIMATE_MSEC 16

struct periph_gesture_data {
    /* abs-to-rel state */
    bool initialized;
    int16_t prev_x, prev_y;
    int32_t pending_dx;

    /* touch detection */
    bool touching;
    uint32_t last_event_ms;
    struct k_work_delayable touch_end_work;

    /* velocity window */
    int32_t vel_dx[PERIPH_GESTURE_VEL_WINDOW];
    int32_t vel_dy[PERIPH_GESTURE_VEL_WINDOW];
    uint32_t vel_dt[PERIPH_GESTURE_VEL_WINDOW];
    uint8_t vel_head;
    uint8_t vel_count;

    /* inertial animation (Q8 fixed-point: value × 256 = raw_px) */
    struct k_work_delayable inertial_work;
    int32_t delta_x_fp, delta_y_fp;
    int32_t accum_x_fp, accum_y_fp;

    const struct device *dev;
};

struct periph_gesture_config {
    const struct device *cirque_dev;
    const struct device *touch_key_dev;
    uint16_t max_delta;
    uint8_t touch_timeout_ms;
    uint8_t velocity_threshold;
    uint8_t decay_percent;
    uint8_t speed_scale;
};
