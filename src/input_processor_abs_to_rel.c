/*
 * Simple ABS-to-REL input processor with optional rotation.
 * Converts INPUT_ABS_X/Y events to INPUT_REL_X/Y deltas from previous position.
 * Each device instance maintains its own state, so multiple independent copies
 * can coexist in different input-processor chains.
 *
 * Rotation applies a CCW rotation to the output deltas using fixed-point math
 * (1024 scale factor). The X cross-axis term uses the previous cycle's dy as
 * an approximation (1-sample lag), which is imperceptible at normal speeds.
 */

#define DT_DRV_COMPAT zmk_input_processor_abs_to_rel

#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

LOG_MODULE_REGISTER(abs_to_rel, CONFIG_ZMK_LOG_LEVEL);

struct abs_to_rel_config {
    uint16_t max_delta;
    int32_t rotation_degrees;
};

struct abs_to_rel_data {
    bool initialized;
    int16_t prev_x;
    int16_t prev_y;
    int32_t cur_dx;   /* this cycle's raw dx, used by Y for the sin cross-term */
    int32_t prev_dy;  /* previous cycle's raw dy, used by X for the sin cross-term */
    int32_t cos_fp;   /* cos(rotation) * 1024 */
    int32_t sin_fp;   /* sin(rotation) * 1024 */
    uint32_t last_event_ms;
};

static int abs_to_rel_handle_event(const struct device *dev, struct input_event *event,
                                   uint32_t param1, uint32_t param2,
                                   struct zmk_input_processor_state *state) {
    struct abs_to_rel_data *data = (struct abs_to_rel_data *)dev->data;
    const struct abs_to_rel_config *cfg = (const struct abs_to_rel_config *)dev->config;

    if (event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    uint32_t now = k_uptime_get();
    if (data->initialized && (now - data->last_event_ms) > 50) {
        data->initialized = false;
        data->cur_dx = 0;
        data->prev_dy = 0;
    }
    data->last_event_ms = now;

    if (event->code == INPUT_ABS_X) {
        int16_t cur = (int16_t)event->value;
        int16_t dx = data->initialized ? (cur - data->prev_x) : 0;
        if (cfg->max_delta > 0) {
            if (dx >  (int16_t)cfg->max_delta) dx =  (int16_t)cfg->max_delta;
            if (dx < -(int16_t)cfg->max_delta) dx = -(int16_t)cfg->max_delta;
        }
        data->prev_x = cur;
        data->cur_dx = dx;
        /* X component: uses prev_dy as 1-sample approximation for the -dy*sin term */
        int32_t rotated = (dx * data->cos_fp - data->prev_dy * data->sin_fp) >> 10;
        event->type = INPUT_EV_REL;
        event->code = INPUT_REL_X;
        event->value = rotated;
    } else if (event->code == INPUT_ABS_Y) {
        int16_t cur = (int16_t)event->value;
        int16_t dy = data->initialized ? (cur - data->prev_y) : 0;
        if (cfg->max_delta > 0) {
            if (dy >  (int16_t)cfg->max_delta) dy =  (int16_t)cfg->max_delta;
            if (dy < -(int16_t)cfg->max_delta) dy = -(int16_t)cfg->max_delta;
        }
        data->prev_y = cur;
        data->initialized = true;
        data->prev_dy = dy;
        /* Y component: uses cur_dx (this cycle's X delta) for the sin cross-term */
        int32_t rotated = (data->cur_dx * data->sin_fp + (int32_t)dy * data->cos_fp) >> 10;
        event->type = INPUT_EV_REL;
        event->code = INPUT_REL_Y;
        event->value = rotated;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int abs_to_rel_init(const struct device *dev) {
    const struct abs_to_rel_config *cfg = (const struct abs_to_rel_config *)dev->config;
    struct abs_to_rel_data *data = (struct abs_to_rel_data *)dev->data;

    double rad = cfg->rotation_degrees * M_PI / 180.0;
    data->cos_fp = (int32_t)(cos(rad) * 1024.0);
    data->sin_fp = (int32_t)(sin(rad) * 1024.0);
    return 0;
}

static const struct zmk_input_processor_driver_api abs_to_rel_driver_api = {
    .handle_event = abs_to_rel_handle_event,
};

#define ABS_TO_REL_INST(n)                                                              \
    static struct abs_to_rel_data abs_to_rel_data_##n = {0};                           \
    static const struct abs_to_rel_config abs_to_rel_config_##n = {                    \
        .max_delta = DT_INST_PROP(n, max_delta),                                        \
        .rotation_degrees = DT_INST_PROP(n, rotation_degrees),                          \
    };                                                                                   \
    DEVICE_DT_INST_DEFINE(n, abs_to_rel_init, NULL, &abs_to_rel_data_##n,              \
                          &abs_to_rel_config_##n, POST_KERNEL,                           \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                           \
                          &abs_to_rel_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ABS_TO_REL_INST)
