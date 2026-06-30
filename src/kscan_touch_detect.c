/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Virtual kscan device driven by the peripheral_gesture processor.
 * Reports a single key at (row=0, col=0) — wired into zmk,kscan-composite
 * so the touch shows up as any real key and can carry any ZMK behavior.
 */

#define DT_DRV_COMPAT zmk_kscan_touch_detect

#include <zephyr/device.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(kscan_touch_detect, CONFIG_ZMK_LOG_LEVEL);

struct kscan_touch_data {
    kscan_callback_t cb;
};

void zmk_kscan_touch_report(const struct device *dev, bool pressed) {
    struct kscan_touch_data *data = dev->data;
    if (data->cb) {
        data->cb(dev, 0, 0, pressed);
    }
}

static int kscan_touch_enable_cb(const struct device *dev) {
    return 0;
}

static int kscan_touch_disable_cb(const struct device *dev) {
    return 0;
}

static int kscan_touch_configure(const struct device *dev, kscan_callback_t cb) {
    struct kscan_touch_data *data = dev->data;
    data->cb = cb;
    return 0;
}

static const struct kscan_driver_api kscan_touch_driver_api = {
    .config   = kscan_touch_configure,
    .enable_callback  = kscan_touch_enable_cb,
    .disable_callback = kscan_touch_disable_cb,
};

static int kscan_touch_init(const struct device *dev) {
    LOG_DBG("kscan_touch_detect init");
    return 0;
}

#define KSCAN_TOUCH_INST(n)                                                 \
    static struct kscan_touch_data kscan_touch_data_##n = {0};             \
    DEVICE_DT_INST_DEFINE(n, kscan_touch_init, NULL,                       \
                          &kscan_touch_data_##n, NULL,                      \
                          POST_KERNEL, CONFIG_KSCAN_INIT_PRIORITY,          \
                          &kscan_touch_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_TOUCH_INST)
