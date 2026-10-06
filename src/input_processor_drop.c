/*
 * No-op input processor: zeroes the value of every event it sees.
 * Wire into an input-listener's per-layer override to fully disable
 * a pointing device's effect (movement, scroll, buttons) on specific layers.
 *
 * Must mutate the event rather than return ZMK_INPUT_PROC_STOP: a STOP
 * returned from inside a layer override is swallowed by
 * filter_with_input_config() in ZMK core (it always returns 0 for a
 * matched, non-process-next override), so the event still reaches
 * handle_rel_code()/handle_key_code() afterward regardless.
 */

#define DT_DRV_COMPAT zmk_input_processor_drop

#include <drivers/input_processor.h>

static int drop_handle_event(const struct device *dev, struct input_event *event,
                             uint32_t param1, uint32_t param2,
                             struct zmk_input_processor_state *state) {
    event->value = 0;
    return ZMK_INPUT_PROC_CONTINUE;
}

static int drop_init(const struct device *dev) { return 0; }

static const struct zmk_input_processor_driver_api drop_driver_api = {
    .handle_event = drop_handle_event,
};

#define DROP_INST(n)                                                                    \
    DEVICE_DT_INST_DEFINE(n, drop_init, NULL, NULL, NULL, POST_KERNEL,                 \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &drop_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DROP_INST)
