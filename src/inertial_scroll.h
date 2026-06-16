#pragma once

#include "input_processor_gestures.h"

struct inertial_scroll_data {
    struct k_work_delayable scroll_work;
    double delta_v, delta_h;
    double accum_v, accum_h;
    uint32_t delta_time;
    double velocity_decay;
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
