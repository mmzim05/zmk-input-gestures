/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>

void zmk_kscan_touch_report(const struct device *dev, bool pressed);
