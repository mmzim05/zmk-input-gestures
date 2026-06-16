#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <math.h>
#include "input_processor_gestures.h"
#include "inertial_scroll.h"
#include "touch_detection.h"

LOG_MODULE_DECLARE(gestures, CONFIG_ZMK_LOG_LEVEL);

#define SCROLL_ANIMATE_MSEC 16

static void inertial_scroll_work_handler(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct inertial_scroll_data *data = CONTAINER_OF(d_work, struct inertial_scroll_data, scroll_work);

    data->delta_v *= data->velocity_decay;
    data->delta_h *= data->velocity_decay;

    data->accum_v += data->delta_v;
    data->accum_h += data->delta_h;

    int sv = (int)data->accum_v;
    int sh = (int)data->accum_h;
    data->accum_v -= (double)sv;
    data->accum_h -= (double)sh;

    if (sv != 0 || sh != 0) {
        zmk_hid_mouse_scroll_set(sh, sv);
        zmk_endpoints_send_mouse_report();
        zmk_hid_mouse_scroll_set(0, 0);
    }

    if (fabs(data->delta_v) > 0.01 || fabs(data->delta_h) > 0.01 ||
        fabs(data->accum_v) >= 0.5 || fabs(data->accum_h) >= 0.5) {
        k_work_reschedule(&data->scroll_work, K_MSEC(SCROLL_ANIMATE_MSEC));
    }
}

int inertial_scroll_handle_touch(const struct device *dev, struct gesture_event_t *event) {
    struct gesture_data *data = (struct gesture_data *)dev->data;
    struct gesture_config *config = (struct gesture_config *)dev->config;

    if (!config->inertial_scroll.enabled) {
        return -1;
    }

    if (event->delta_x != 0 || event->delta_y != 0) {
        uint8_t idx = data->inertial_scroll.vel_head;
        data->inertial_scroll.vel_dh[idx] = event->delta_x;
        data->inertial_scroll.vel_dv[idx] = event->delta_y;
        data->inertial_scroll.vel_dt[idx] = event->delta_time ? event->delta_time : 10;
        data->inertial_scroll.vel_head = (idx + 1) % INERTIAL_SCROLL_VEL_WINDOW;
        if (data->inertial_scroll.vel_count < INERTIAL_SCROLL_VEL_WINDOW) {
            data->inertial_scroll.vel_count++;
        }
    }

    return 0;
}

int inertial_scroll_handle_touch_start(const struct device *dev, struct gesture_event_t *event) {
    struct gesture_data *data = (struct gesture_data *)dev->data;
    struct gesture_config *config = (struct gesture_config *)dev->config;

    if (!config->inertial_scroll.enabled) {
        return -1;
    }

    k_work_cancel_delayable(&data->inertial_scroll.scroll_work);

    data->inertial_scroll.delta_v = 0;
    data->inertial_scroll.delta_h = 0;
    data->inertial_scroll.delta_time = 0;
    data->inertial_scroll.accum_v = 0;
    data->inertial_scroll.accum_h = 0;
    data->inertial_scroll.vel_head = 0;
    data->inertial_scroll.vel_count = 0;

    inertial_scroll_handle_touch(dev, event);

    return 0;
}

int inertial_scroll_handle_end(const struct device *dev) {
    struct gesture_data *data = (struct gesture_data *)dev->data;
    struct gesture_config *config = (struct gesture_config *)dev->config;

    if (!config->inertial_scroll.enabled) {
        return -1;
    }

    int n = data->inertial_scroll.vel_count;
    if (n == 0) {
        return -1;
    }
    double sum_dh = 0, sum_dv = 0;
    uint32_t sum_dt = 0;
    for (int i = 0; i < n; i++) {
        sum_dh += data->inertial_scroll.vel_dh[i];
        sum_dv += data->inertial_scroll.vel_dv[i];
        sum_dt += data->inertial_scroll.vel_dt[i];
    }
    double avg_dh = sum_dh / n;
    double avg_dv = sum_dv / n;
    double avg_dt = (double)sum_dt / n;

    double velocity = sqrt(avg_dh * avg_dh + avg_dv * avg_dv) / avg_dt;

    if (velocity <= (double)config->inertial_scroll.velocity_threshold / 10.0) {
        return -1;
    }

    if (avg_dt > 0) {
        double scale = (double)SCROLL_ANIMATE_MSEC / avg_dt * SCROLL_SENSITIVITY;
        data->inertial_scroll.delta_v = avg_dv * scale;
        data->inertial_scroll.delta_h = avg_dh * scale;
    }

    data->inertial_scroll.accum_v = 0;
    data->inertial_scroll.accum_h = 0;

    zmk_hid_mouse_scroll_set(0, 0);
    zmk_endpoints_send_mouse_report();

    k_work_reschedule(&data->inertial_scroll.scroll_work, K_MSEC(SCROLL_ANIMATE_MSEC));

    return 0;
}

int inertial_scroll_init(const struct device *dev) {
    struct gesture_data *data = (struct gesture_data *)dev->data;
    struct gesture_config *config = (struct gesture_config *)dev->config;

    if (!config->inertial_scroll.enabled) {
        return -1;
    }

    data->inertial_scroll.velocity_decay = (100.0 - config->inertial_scroll.decay_percent) / 100.0;
    k_work_init_delayable(&data->inertial_scroll.scroll_work, inertial_scroll_work_handler);

    return 0;
}
