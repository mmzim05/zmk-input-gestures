/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Blinks led0 at 1 Hz as a crash-detection heartbeat.
 * If blinking stops, the MCU crashed. If BLE drops but blinking continues,
 * the right half is alive and the issue is in the BLE link.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

#define LED_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS(LED_NODE, okay)
#error "led0 alias not found — heartbeat_led requires a board with led0 defined"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
static struct k_work_delayable blink_work;

#define BLINK_HALF_PERIOD_MS 500

static void blink_handler(struct k_work *work) {
    gpio_pin_toggle_dt(&led);
    k_work_reschedule(&blink_work, K_MSEC(BLINK_HALF_PERIOD_MS));
}

static int heartbeat_led_init(void) {
    if (!device_is_ready(led.port)) {
        return 0;
    }
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    k_work_init_delayable(&blink_work, blink_handler);
    k_work_reschedule(&blink_work, K_MSEC(BLINK_HALF_PERIOD_MS));
    return 0;
}

SYS_INIT(heartbeat_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
