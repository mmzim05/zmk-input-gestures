/*
 * ABS-to-REL input processor with optional CCW rotation.
 * Converts INPUT_ABS_X/Y to INPUT_REL_X/Y deltas.
 *
 * Rotation uses precomputed fixed-point cos/sin (1024-scaled) from DT config,
 * avoiding runtime trig. The X cross-axis term uses the previous cycle's dy
 * (1-sample lag), imperceptible at normal trackpad speeds.
 */

#define DT_DRV_COMPAT zmk_input_processor_abs_to_rel

#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(abs_to_rel, CONFIG_ZMK_LOG_LEVEL);

struct abs_to_rel_config {
    uint16_t max_delta;
    int16_t cos_fp;  /* cos(rotation) * 1024 */
    int16_t sin_fp;  /* sin(rotation) * 1024 */
};

struct abs_to_rel_data {
    bool initialized;
    int16_t prev_x;
    int16_t prev_y;
    int32_t cur_dx;   /* this cycle's raw dx, for Y sin cross-term */
    int32_t prev_dy;  /* previous cycle's raw dy, for X sin cross-term (1-sample lag) */
    uint32_t last_event_ms;
};

static int abs_to_rel_handle_event(const struct device *dev, struct input_event *event,
                                   uint32_t param1, uint32_t param2,
                                   struct zmk_input_processor_state *state) {
    const struct abs_to_rel_config *cfg = (const struct abs_to_rel_config *)dev->config;
    struct abs_to_rel_data *data = (struct abs_to_rel_data *)dev->data;

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
        /* X: prev_dy used as 1-sample approximation for -dy*sin cross-term */
        int32_t rotated = ((int32_t)dx * cfg->cos_fp - data->prev_dy * cfg->sin_fp) >> 10;
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
        /* Y: cur_dx (this cycle's dx) gives exact sin cross-term */
        int32_t rotated = (data->cur_dx * cfg->sin_fp + (int32_t)dy * cfg->cos_fp) >> 10;
        event->type = INPUT_EV_REL;
        event->code = INPUT_REL_Y;
        event->value = rotated;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int abs_to_rel_init(const struct device *dev) { return 0; }

static const struct zmk_input_processor_driver_api abs_to_rel_driver_api = {
    .handle_event = abs_to_rel_handle_event,
};

#define ABS_TO_REL_INST(n)                                                              \
    static struct abs_to_rel_data abs_to_rel_data_##n = {0};                           \
    static const struct abs_to_rel_config abs_to_rel_config_##n = {                    \
        .max_delta = DT_INST_PROP(n, max_delta),                                        \
        .cos_fp    = DT_INST_PROP(n, rotation_cos_fp),                                  \
        .sin_fp    = DT_INST_PROP(n, rotation_sin_fp),                                  \
    };                                                                                   \
    DEVICE_DT_INST_DEFINE(n, abs_to_rel_init, NULL, &abs_to_rel_data_##n,              \
                          &abs_to_rel_config_##n, POST_KERNEL,                           \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                           \
                          &abs_to_rel_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ABS_TO_REL_INST)
