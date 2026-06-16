/*
 * Simple ABS-to-REL input processor.
 * Converts INPUT_ABS_X/Y events to INPUT_REL_X/Y deltas from previous position.
 * Each device instance maintains its own state, so multiple independent copies
 * can coexist in different input-processor chains.
 */

#define DT_DRV_COMPAT zmk_input_processor_abs_to_rel

#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(abs_to_rel, CONFIG_ZMK_LOG_LEVEL);

struct abs_to_rel_config {
    uint16_t max_delta;
};

struct abs_to_rel_data {
    bool initialized;
    int16_t prev_x;
    int16_t prev_y;
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

    /* If more than 50 ms has passed since the last ABS event the finger was
     * lifted.  Reset so the first event of the new touch produces delta = 0. */
    uint32_t now = k_uptime_get();
    if (data->initialized && (now - data->last_event_ms) > 50) {
        data->initialized = false;
    }
    data->last_event_ms = now;

    if (event->code == INPUT_ABS_X) {
        int16_t cur = (int16_t)event->value;
        int16_t delta = data->initialized ? (cur - data->prev_x) : 0;
        if (cfg->max_delta > 0) {
            if (delta >  (int16_t)cfg->max_delta) delta =  (int16_t)cfg->max_delta;
            if (delta < -(int16_t)cfg->max_delta) delta = -(int16_t)cfg->max_delta;
        }
        data->prev_x = cur;
        event->type = INPUT_EV_REL;
        event->code = INPUT_REL_X;
        event->value = delta;
    } else if (event->code == INPUT_ABS_Y) {
        int16_t cur = (int16_t)event->value;
        int16_t delta = data->initialized ? (cur - data->prev_y) : 0;
        if (cfg->max_delta > 0) {
            if (delta >  (int16_t)cfg->max_delta) delta =  (int16_t)cfg->max_delta;
            if (delta < -(int16_t)cfg->max_delta) delta = -(int16_t)cfg->max_delta;
        }
        data->prev_y = cur;
        data->initialized = true;
        event->type = INPUT_EV_REL;
        event->code = INPUT_REL_Y;
        event->value = delta;
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
    };                                                                                   \
    DEVICE_DT_INST_DEFINE(n, abs_to_rel_init, NULL, &abs_to_rel_data_##n,              \
                          &abs_to_rel_config_##n, POST_KERNEL,                           \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                           \
                          &abs_to_rel_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ABS_TO_REL_INST)
