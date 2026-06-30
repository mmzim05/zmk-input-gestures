#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <math.h>
#include <stdlib.h>
#include "input_processor_gestures.h"
#include "inertial_cursor.h"

LOG_MODULE_DECLARE(gestures, CONFIG_ZMK_LOG_LEVEL);

#define ANIMATE_MSEC 16

static void inertial_cursor_work_handler(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct inertial_cursor_data *data = CONTAINER_OF(d_work, struct inertial_cursor_data, inertial_work);

    data->delta_x *= data->velocity_decay;
    data->delta_y *= data->velocity_decay;

    data->accum_x += data->delta_x;
    data->accum_y += data->delta_y;

    int sx = (int)data->accum_x;
    int sy = (int)data->accum_y;
    data->accum_x -= (double)sx;
    data->accum_y -= (double)sy;

    if (sx != 0 || sy != 0) {
        zmk_hid_mouse_movement_set(sx, sy);
        zmk_endpoints_send_mouse_report();
        zmk_hid_mouse_movement_set(0, 0);
    }

    if (fabs(data->delta_x) > 0.01 || fabs(data->delta_y) > 0.01 ||
        fabs(data->accum_x) >= 0.5 || fabs(data->accum_y) >= 0.5) {
        k_work_reschedule(&data->inertial_work, K_MSEC(ANIMATE_MSEC));
    }
}

int inertial_cursor_handle_touch(const struct device *dev, struct gesture_event_t *event) {
    struct gesture_data *data = (struct gesture_data *)dev->data;
    struct gesture_config *config = (struct gesture_config *)dev->config;

    if (!config->inertial_cursor.enabled) {
        return -1;
    }

    if (event->delta_x != 0 || event->delta_y != 0) {
        /* skip implausibly large deltas (Pinnacle calibration glitch or BLE corruption) */
        int32_t adx = abs(event->delta_x), ady = abs(event->delta_y);
        if (adx > 250 || ady > 250) {
            return 0;
        }
        uint8_t idx = data->inertial_cursor.vel_head;
        data->inertial_cursor.vel_dx[idx] = event->delta_x;
        data->inertial_cursor.vel_dy[idx] = event->delta_y;
        data->inertial_cursor.vel_dt[idx] = event->delta_time ? event->delta_time : 10;
        data->inertial_cursor.vel_head = (idx + 1) % INERTIAL_CURSOR_VEL_WINDOW;
        if (data->inertial_cursor.vel_count < INERTIAL_CURSOR_VEL_WINDOW) {
            data->inertial_cursor.vel_count++;
        }
    }

    return 0;
}


int inertial_cursor_handle_touch_start(const struct device *dev, struct gesture_event_t *event) {
    struct gesture_data *data = (struct gesture_data *)dev->data;
    struct gesture_config *config = (struct gesture_config *)dev->config;

    if (!config->inertial_cursor.enabled) {
        return -1;
    }

    k_work_cancel_delayable(&data->inertial_cursor.inertial_work);

    data->inertial_cursor.delta_x = 0;
    data->inertial_cursor.delta_y = 0;
    data->inertial_cursor.delta_time = 0;
    data->inertial_cursor.accum_x = 0;
    data->inertial_cursor.accum_y = 0;
    data->inertial_cursor.vel_head = 0;
    data->inertial_cursor.vel_count = 0;

    inertial_cursor_handle_touch(dev, event);

    return 0;
}

int inertial_cursor_handle_end(const struct device *dev) {
    struct gesture_data *data = (struct gesture_data *)dev->data;
    struct gesture_config *config = (struct gesture_config *)dev->config;

    if (!config->inertial_cursor.enabled) {
        return -1;
    }

    /* average the last N events to smooth out lift jitter and outlier spikes */
    int n = data->inertial_cursor.vel_count;
    if (n == 0) {
        return -1;
    }
    double sum_dx = 0, sum_dy = 0;
    uint32_t sum_dt = 0;
    for (int i = 0; i < n; i++) {
        sum_dx += data->inertial_cursor.vel_dx[i];
        sum_dy += data->inertial_cursor.vel_dy[i];
        sum_dt += data->inertial_cursor.vel_dt[i];
    }
    double avg_dx = sum_dx / n;
    double avg_dy = sum_dy / n;
    double avg_dt = (double)sum_dt / n;

    double velocity = sqrt(avg_dx * avg_dx + avg_dy * avg_dy) / avg_dt;

    LOG_DBG("velocity: %d (avg over %d events), threshold: %d",
        (int)velocity, n, (int)config->inertial_cursor.velocity_threshold);

    if (velocity <= (double)config->inertial_cursor.velocity_threshold / 10.0) {
        return -1;
    }

    if (avg_dt > 0) {
        double scale = (double)ANIMATE_MSEC / avg_dt
                       * config->inertial_cursor.speed_scale / 100.0;
        data->inertial_cursor.delta_x = avg_dx * scale;
        data->inertial_cursor.delta_y = avg_dy * scale;
    }

    data->inertial_cursor.accum_x = 0;
    data->inertial_cursor.accum_y = 0;

    k_work_reschedule(&data->inertial_cursor.inertial_work, K_MSEC(ANIMATE_MSEC));

    return 0;
}

int inertial_cursor_init(const struct device *dev) {
    struct gesture_data *data = (struct gesture_data *)dev->data;
    struct gesture_config *config = (struct gesture_config *)dev->config;

    LOG_DBG("inertial_cursor: %s, velocity_threshold: %d, decay_percent: %d", 
        config->inertial_cursor.enabled ? "yes" : "no", 
        config->inertial_cursor.velocity_threshold,
        config->inertial_cursor.decay_percent);


    if (!config->inertial_cursor.enabled) {
        return -1;
    }

    data->inertial_cursor.velocity_decay = (100.0 - config->inertial_cursor.decay_percent) / 100.0;
    LOG_ERR("velocity_decay *1000: %d", (int) (data->inertial_cursor.velocity_decay * 1000.0));

    k_work_init_delayable(&data->inertial_cursor.inertial_work, inertial_cursor_work_handler);
    return 0;
}
