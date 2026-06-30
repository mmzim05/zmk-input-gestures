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
#include <math.h>

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

    data->delta_x *= data->velocity_decay;
    data->delta_y *= data->velocity_decay;
    data->accum_x += data->delta_x;
    data->accum_y += data->delta_y;

    int sx = (int)data->accum_x;
    int sy = (int)data->accum_y;
    data->accum_x -= (double)sx;
    data->accum_y -= (double)sy;

    if (sx != 0 || sy != 0) {
        input_report_rel(cfg->cirque_dev, INPUT_REL_X, sx, false, K_NO_WAIT);
        input_report_rel(cfg->cirque_dev, INPUT_REL_Y, sy, true,  K_NO_WAIT);
    }

    if (fabs(data->delta_x) > 0.01 || fabs(data->delta_y) > 0.01 ||
        fabs(data->accum_x) >= 0.5   || fabs(data->accum_y) >= 0.5) {
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

    double sum_dx = 0, sum_dy = 0;
    uint32_t sum_dt = 0;
    for (int i = 0; i < n; i++) {
        sum_dx += data->vel_dx[i];
        sum_dy += data->vel_dy[i];
        sum_dt += data->vel_dt[i];
    }
    double avg_dx = sum_dx / n;
    double avg_dy = sum_dy / n;
    double avg_dt = (double)sum_dt / n;

    /* if finger was held still before lifting, elapsed >> avg_dt → low velocity */
    uint32_t elapsed = k_uptime_get() - data->last_event_ms;
    double effective_dt = (elapsed > (uint32_t)avg_dt) ? (double)elapsed : avg_dt;

    double velocity = sqrt(avg_dx * avg_dx + avg_dy * avg_dy) / effective_dt;

    LOG_DBG("touch_end: vel=%.3f (thr=%.1f) n=%d eff_dt=%.0f",
            velocity, (double)cfg->velocity_threshold / 10.0, n, effective_dt);

    if (velocity > (double)cfg->velocity_threshold / 10.0) {
        double scale = (double)PERIPH_GESTURE_ANIMATE_MSEC / avg_dt
                       * cfg->speed_scale / 100.0;
        data->delta_x = avg_dx * scale;
        data->delta_y = avg_dy * scale;
        data->accum_x = 0;
        data->accum_y = 0;
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

    /* let injected REL events (and any other non-ABS/non-SYN) pass through */
    if (event->type == INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* stop all SYN — we generate our own via input_report_rel sync=true */
    if (event->type == INPUT_EV_SYN) {
        return ZMK_INPUT_PROC_STOP;
    }

    if (event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_CONTINUE;
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
        data->accum_x = 0;
        data->accum_y = 0;
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
    data->velocity_decay = (100.0 - cfg->decay_percent) / 100.0;

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
