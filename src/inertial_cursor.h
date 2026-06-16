#pragma once

#include "input_processor_gestures.h"

#define INERTIAL_CURSOR_VEL_WINDOW 5

struct inertial_cursor_data {
    struct k_work_delayable inertial_work;
    uint16_t previous_x, previous_y;
    double delta_x, delta_y;
    double accum_x, accum_y;
    uint32_t delta_time;
    double velocity_decay;
    /* rolling window for velocity estimation at release */
    int32_t vel_dx[INERTIAL_CURSOR_VEL_WINDOW];
    int32_t vel_dy[INERTIAL_CURSOR_VEL_WINDOW];
    uint32_t vel_dt[INERTIAL_CURSOR_VEL_WINDOW];
    uint8_t vel_head;
    uint8_t vel_count;
    gesture_data *all;
};

struct inertial_cursor_config {
    const bool enabled;
    const uint16_t velocity_threshold;
    const uint8_t decay_percent;
    const uint8_t speed_scale; /* percent: match to zip_xy_scaler numerator */
};

handle_init_t inertial_cursor_init;
handle_touch_t inertial_cursor_handle_touch_start;
handle_touch_t inertial_cursor_handle_touch;
handle_touch_end_t inertial_cursor_handle_end;
