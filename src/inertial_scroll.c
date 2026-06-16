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

    if (event->delta_x != 0) {
        data->inertial_scroll.delta_h = event->delta_x;
    }
    if (event->delta_y != 0) {
        data->inertial_scroll.delta_v = event->delta_y;
    }
    if (event->delta_time != 0) {
        data->inertial_scroll.delta_time = event->delta_time;
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

    inertial_scroll_handle_touch(dev, event);

    return 0;
}

int inertial_scroll_handle_end(const struct device *dev) {
    struct gesture_data *data = (struct gesture_data *)dev->data;
    struct gesture_config *config = (struct gesture_config *)dev->config;

    if (!config->inertial_scroll.enabled) {
        return -1;
    }

    double velocity = sqrt(
        data->inertial_scroll.delta_v * data->inertial_scroll.delta_v +
        data->inertial_scroll.delta_h * data->inertial_scroll.delta_h
    ) / (data->inertial_scroll.delta_time > 0 ? data->inertial_scroll.delta_time : 1);

    if (velocity <= config->inertial_scroll.velocity_threshold) {
        return -1;
    }

    if (data->inertial_scroll.delta_time > 0) {
        double scale = (double)SCROLL_ANIMATE_MSEC / data->inertial_scroll.delta_time * SCROLL_SENSITIVITY;
        data->inertial_scroll.delta_v *= scale;
        data->inertial_scroll.delta_h *= scale;
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
