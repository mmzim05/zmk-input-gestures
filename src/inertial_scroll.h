#pragma once

#include "input_processor_gestures.h"

#define INERTIAL_SCROLL_VEL_WINDOW 5

struct inertial_scroll_data {
    struct k_work_delayable scroll_work;
    double delta_v, delta_h;
    double accum_v, accum_h;
    uint32_t delta_time;
    double velocity_decay;
    int32_t vel_dh[INERTIAL_SCROLL_VEL_WINDOW];
    int32_t vel_dv[INERTIAL_SCROLL_VEL_WINDOW];
    uint32_t vel_dt[INERTIAL_SCROLL_VEL_WINDOW];
    uint8_t vel_head;
    uint8_t vel_count;
    gesture_data *all;
};

struct inertial_scroll_config {
    const bool enabled;
    const uint16_t velocity_threshold;
    const uint8_t decay_percent;
};

handle_init_t inertial_scroll_init;
handle_touch_t inertial_scroll_handle_touch_start;
handle_touch_t inertial_scroll_handle_touch;
handle_touch_end_t inertial_scroll_handle_end;
