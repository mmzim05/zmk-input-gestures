/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Input processor for the right (peripheral) half.
 * Runs on glidepoint_split.input-processors.
 *
 * Receives raw ABS events from the Cirque, computes REL deltas, injects them
 * back into the input subsystem (where glidepoint_split picks them up and
 * forwards over BLE), and drives inertial animation after lift.
 *
 * All ABS and SYN events are stopped here — only injected REL events cross BLE.
 */

#define DT_DRV_COMPAT zmk_input_peripheral_gesture

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <drivers/input_processor.h>

#include "peripheral_gesture.h"
#include "kscan_touch_detect.h"

LOG_MODULE_REGISTER(periph_gesture, CONFIG_ZMK_LOG_LEVEL);

/* ------------------------------------------------------------------ */
/* inertial animation work                                             */
/* ------------------------------------------------------------------ */

static void inertial_work_handler(struct k_work *work) {
    struct k_work_delayable *d = k_work_delayable_from_work(work);
    struct periph_gesture_data *data =
        CONTAINER_OF(d, struct periph_gesture_data, inertial_work);
    const struct periph_gesture_config *cfg = data->dev->config;

    /* decay: Q8 × (100 - decay_percent) / 100 */
    data->delta_x_fp = data->delta_x_fp * (100 - cfg->decay_percent) / 100;
    data->delta_y_fp = data->delta_y_fp * (100 - cfg->decay_percent) / 100;
    data->accum_x_fp += data->delta_x_fp;
    data->accum_y_fp += data->delta_y_fp;

    int sx = data->accum_x_fp >> 8;
    int sy = data->accum_y_fp >> 8;
    data->accum_x_fp -= sx << 8;
    data->accum_y_fp -= sy << 8;

    if (sx != 0 || sy != 0) {
        input_report_rel(cfg->cirque_dev, INPUT_REL_X, sx, false, K_NO_WAIT);
        input_report_rel(cfg->cirque_dev, INPUT_REL_Y, sy, true,  K_NO_WAIT);
    }

    /* stop when |delta| < 0.01 px (Q8: 3) and |accum| < 0.5 px (Q8: 128) */
    if (data->delta_x_fp > 2 || data->delta_x_fp < -2 ||
        data->delta_y_fp > 2 || data->delta_y_fp < -2 ||
        data->accum_x_fp > 127 || data->accum_x_fp < -127 ||
        data->accum_y_fp > 127 || data->accum_y_fp < -127) {
        k_work_reschedule(&data->inertial_work, K_MSEC(PERIPH_GESTURE_ANIMATE_MSEC));
    }
}

/* ------------------------------------------------------------------ */
/* touch-end timeout                                                   */
/* ------------------------------------------------------------------ */

static void touch_end_handler(struct k_work *work) {
    struct k_work_delayable *d = k_work_delayable_from_work(work);
    struct periph_gesture_data *data =
        CONTAINER_OF(d, struct periph_gesture_data, touch_end_work);
    const struct periph_gesture_config *cfg = data->dev->config;

    data->touching = false;

    int n = data->vel_count;
    if (n == 0) {
        zmk_kscan_touch_report(cfg->touch_key_dev, false);
        return;
    }

    int32_t sum_dx = 0, sum_dy = 0;
    uint32_t sum_dt = 0;
    for (int i = 0; i < n; i++) {
        sum_dx += data->vel_dx[i];
        sum_dy += data->vel_dy[i];
        sum_dt += data->vel_dt[i];
    }

    /* staleness check: if held still before lift, elapsed×n > sum_dt → use elapsed */
    uint32_t elapsed = k_uptime_get() - data->last_event_ms;
    uint32_t elapsed_total = elapsed * (uint32_t)n;
    uint32_t effective_sum_dt = (elapsed_total > sum_dt) ? elapsed_total : sum_dt;
    if (effective_sum_dt == 0) effective_sum_dt = 1;

    /* velocity check via Manhattan distance (no sqrt, no float):
     * (|avg_dx| + |avg_dy|) × 10 > velocity_threshold × avg_dt
     * → (|sum_dx| + |sum_dy|) × 10 > velocity_threshold × effective_sum_dt */
    int32_t abs_dx = sum_dx < 0 ? -sum_dx : sum_dx;
    int32_t abs_dy = sum_dy < 0 ? -sum_dy : sum_dy;
    bool fast = ((abs_dx + abs_dy) * 10 > (int32_t)(cfg->velocity_threshold * effective_sum_dt));

    LOG_DBG("touch_end: fast=%d sum=(%d,%d) eff_sum_dt=%u n=%d",
            fast, sum_dx, sum_dy, effective_sum_dt, n);

    if (fast) {
        /* delta_fp (Q8) = sum_dx × ANIMATE_MSEC × speed_scale × 256
         *                 / (effective_sum_dt × 100)
         * int64 intermediate to avoid overflow */
        data->delta_x_fp = (int32_t)((int64_t)sum_dx * PERIPH_GESTURE_ANIMATE_MSEC
                                     * cfg->speed_scale * 256
                                     / ((int64_t)effective_sum_dt * 100));
        data->delta_y_fp = (int32_t)((int64_t)sum_dy * PERIPH_GESTURE_ANIMATE_MSEC
                                     * cfg->speed_scale * 256
                                     / ((int64_t)effective_sum_dt * 100));
        data->accum_x_fp = 0;
        data->accum_y_fp = 0;
        k_work_reschedule(&data->inertial_work, K_MSEC(PERIPH_GESTURE_ANIMATE_MSEC));
    }

    zmk_kscan_touch_report(cfg->touch_key_dev, false);
}

/* ------------------------------------------------------------------ */
/* input processor                                                     */
/* ------------------------------------------------------------------ */

static int periph_gesture_handle_event(const struct device *dev,
                                       struct input_event *event,
                                       uint32_t param1, uint32_t param2,
                                       struct zmk_input_processor_state *state) {
    struct periph_gesture_data *data = dev->data;
    const struct periph_gesture_config *cfg = dev->config;

    /* injected REL events (from our own inertial/live path) pass through to BLE */
    if (event->type == INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* only handle ABS events from Cirque; drop everything else (KEY, etc.) */
    if (event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_STOP;
    }

    /* --- ABS event from Cirque --- */

    /* reschedule touch-end timeout */
    k_work_reschedule(&data->touch_end_work, K_MSEC(cfg->touch_timeout_ms));

    /* detect touch start */
    if (!data->touching) {
        data->touching = true;
        k_work_cancel_delayable(&data->inertial_work);
        data->vel_head = 0;
        data->vel_count = 0;
        data->accum_x_fp = 0;
        data->accum_y_fp = 0;
        data->initialized = false;
        data->last_event_ms = k_uptime_get();
        zmk_kscan_touch_report(cfg->touch_key_dev, true);
    }

    if (event->code == INPUT_ABS_X) {
        int16_t cur = (int16_t)event->value;
        int32_t dx = data->initialized ? (int32_t)(cur - data->prev_x) : 0;
        if (dx >  (int32_t)cfg->max_delta) dx =  (int32_t)cfg->max_delta;
        if (dx < -(int32_t)cfg->max_delta) dx = -(int32_t)cfg->max_delta;
        data->prev_x = cur;
        data->pending_dx = dx;
        return ZMK_INPUT_PROC_STOP;
    }

    if (event->code == INPUT_ABS_Y) {
        int16_t cur = (int16_t)event->value;
        int32_t dy = data->initialized ? (int32_t)(cur - data->prev_y) : 0;
        if (dy >  (int32_t)cfg->max_delta) dy =  (int32_t)cfg->max_delta;
        if (dy < -(int32_t)cfg->max_delta) dy = -(int32_t)cfg->max_delta;
        data->prev_y = cur;
        data->initialized = true;

        int32_t dx = data->pending_dx;

        /* update velocity window */
        uint32_t now = k_uptime_get();
        uint32_t dt = now - data->last_event_ms;
        if (dt == 0) dt = 1;
        data->last_event_ms = now;

        uint8_t idx = data->vel_head;
        data->vel_dx[idx] = dx;
        data->vel_dy[idx] = dy;
        data->vel_dt[idx] = dt;
        data->vel_head = (idx + 1) % PERIPH_GESTURE_VEL_WINDOW;
        if (data->vel_count < PERIPH_GESTURE_VEL_WINDOW) data->vel_count++;

        /* inject REL pair only when there is actual movement */
        if (dx != 0 || dy != 0) {
            input_report_rel(cfg->cirque_dev, INPUT_REL_X, dx, false, K_NO_WAIT);
            input_report_rel(cfg->cirque_dev, INPUT_REL_Y, dy, true,  K_NO_WAIT);
        }

        return ZMK_INPUT_PROC_STOP;
    }

    return ZMK_INPUT_PROC_STOP;
}

static const struct zmk_input_processor_driver_api periph_gesture_driver_api = {
    .handle_event = periph_gesture_handle_event,
};

/* ------------------------------------------------------------------ */
/* init                                                                */
/* ------------------------------------------------------------------ */

static int periph_gesture_init(const struct device *dev) {
    struct periph_gesture_data *data = dev->data;
    const struct periph_gesture_config *cfg = dev->config;

    data->dev = dev;

    k_work_init_delayable(&data->touch_end_work, touch_end_handler);
    k_work_init_delayable(&data->inertial_work,  inertial_work_handler);

    LOG_DBG("periph_gesture init: thr=%d decay=%d scale=%d max_delta=%d timeout=%d",
            cfg->velocity_threshold, cfg->decay_percent, cfg->speed_scale,
            cfg->max_delta, cfg->touch_timeout_ms);
    return 0;
}

/* ------------------------------------------------------------------ */
/* devicetree instantiation                                            */
/* ------------------------------------------------------------------ */

#define PERIPH_GESTURE_INST(n)                                                    \
    static struct periph_gesture_data periph_gesture_data_##n = {0};             \
    static const struct periph_gesture_config periph_gesture_config_##n = {      \
        .cirque_dev      = DEVICE_DT_GET(DT_INST_PHANDLE(n, device)),            \
        .touch_key_dev   = DEVICE_DT_GET(DT_INST_PHANDLE(n, touch_key)),         \
        .max_delta       = DT_INST_PROP(n, max_delta),                           \
        .touch_timeout_ms = DT_INST_PROP(n, touch_timeout_ms),                   \
        .velocity_threshold = DT_INST_PROP(n, velocity_threshold),               \
        .decay_percent   = DT_INST_PROP(n, decay_percent),                       \
        .speed_scale     = DT_INST_PROP(n, speed_scale),                         \
    };                                                                            \
    DEVICE_DT_INST_DEFINE(n, periph_gesture_init, NULL,                          \
                          &periph_gesture_data_##n,                               \
                          &periph_gesture_config_##n,                             \
                          POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY,                \
                          &periph_gesture_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PERIPH_GESTURE_INST)
