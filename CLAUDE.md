# zmk-input-gestures

Custom ZMK input processor module for the Toucan split keyboard.

## Architecture

All trackpad intelligence runs on the right (peripheral) half. The central receives only:
- Non-zero `REL_X`/`REL_Y` events during live finger movement or inertial animation
- Virtual key press/release via the kscan path when the finger touches/lifts

This eliminates BLE overload: old arch sent ~300 ABS events/sec (3 per poll × 100Hz), which exceeded the 133-slot/sec capacity of a 7.5ms BLE connection interval.

## Drivers and processors

### `zmk,input-peripheral-gesture` (`src/peripheral_gesture.c`)

Input processor for the right half's `glidepoint_split.input-processors`.

- Receives raw `ABS_X`/`ABS_Y` from Cirque at ~100Hz
- Applies virtual rotation (`rotate-cdeg`, Q8 sin/cos precomputed with sinf/cosf at init — float OK at init, not in hot path)
- Converts to `REL_X`/`REL_Y` deltas with `max-delta` clamping
- Maintains a 5-event velocity window (Q8 fixed-point — no float in hot path)
- On touch start: submits `touch_start_work` to `gesture_work_q` to call `zmk_kscan_touch_report(true)` — MUST NOT call directly from INPUT THREAD (deep BLE chain overflows 1024B default INPUT THREAD stack)
- On touch end (timeout): computes velocity with staleness check, starts inertial animation if above threshold, notifies `touch_kscan` key release
- Inertial animation: 32ms timer, decays velocity by `decay_percent` per frame, injects `REL_X`/`REL_Y` via `input_report_rel()`; uses Q8 fixed-point accumulator for sub-pixel precision
- Stops all `INPUT_EV_ABS` and non-REL events — only injected REL pairs cross BLE

**Dedicated work queue (`gesture_work_q`):** all three work items (`touch_start_work`, `touch_end_work`, `inertial_work`) run on this queue with a configurable stack (default 2048B, override to 4096B in right half conf). This keeps the `kscan_composite → ZMK keyboard → bt_gatt_notify` call chain (~650B) off both the INPUT THREAD and the 1024B system workqueue.

**No float in hot path:** `math.h` / `sinf` / `cosf` are only called once during `periph_gesture_init()` to precompute Q8 rotation coefficients. All per-event math is integer Q8 fixed-point.

### `zmk,kscan-touch-detect` (`src/kscan_touch_detect.c`)

Virtual kscan driver with one key at (row=0, col=0). Driven by `zmk_kscan_touch_report(dev, pressed)` called from `peripheral_gesture`. Wire into `zmk,kscan-composite` so the touch key looks like any real keyboard key and can carry any ZMK behavior (mo, tap-dance, macros, etc.).

### `zmk,input-processor-abs-to-rel` (`src/input_processor_abs_to_rel.c`)

Standalone ABS→REL converter (still available but not used in the Toucan config — superseded by `peripheral_gesture`).

### `zmk,input-processor-gestures` (`src/input_processor_gestures.c`)

Original gesture processor (tap, inertial cursor/scroll, circular scroll). Still used when the central needs to process gesture data — not in current Toucan config.

### `heartbeat_led` (`src/heartbeat_led.c`)

Optional crash-detection heartbeat: blinks `led0` at 1 Hz via system workqueue. Enable with `CONFIG_ZMK_HEARTBEAT_LED=y`. If blinking stops, the MCU crashed (not a BLE drop).

## DT binding properties — `zmk,input-peripheral-gesture`

| Property | Default | Meaning |
|---|---|---|
| `device` | required | Phandle to the Cirque device (used as source for `input_report_rel`) |
| `touch-key` | required | Phandle to `zmk,kscan-touch-detect` device |
| `max-delta` | 60 | Per-poll abs delta clamp (raw Pinnacle units) |
| `touch-timeout-ms` | 30 | Ms of silence before touch-end declared (set ≥ 2× Cirque poll interval) |
| `velocity-threshold` | 3 | Min velocity in tenths of raw_px/ms to start inertial |
| `decay-percent` | 9 | % speed lost per `PERIPH_GESTURE_ANIMATE_MSEC` frame |
| `speed-scale` | 100 | Inertial start speed scale — set to match `zip_xy_scaler` numerator |
| `rotate-cdeg` | 0 | Virtual rotation in centidegrees, CCW positive |

## Key constants (`src/peripheral_gesture.h`)

- `PERIPH_GESTURE_VEL_WINDOW 5` — number of events in velocity ring buffer
- `PERIPH_GESTURE_ANIMATE_MSEC 32` — inertial tick interval (31 Hz); lower = smoother but more BLE events

## Kconfig options

| Symbol | Default | Meaning |
|---|---|---|
| `ZMK_INPUT_PERIPHERAL_GESTURE_WORKQ_STACK_SIZE` | 2048 | gesture_work_q stack bytes; set 4096 in right.conf |
| `ZMK_INPUT_PERIPHERAL_GESTURE_WORKQ_PRIORITY` | 5 | gesture_work_q thread priority |
| `ZMK_HEARTBEAT_LED` | n | Enable 1 Hz LED blink on led0 (crash detection) |

## Key files

- `src/peripheral_gesture.c` / `.h` — peripheral-side gesture processor
- `src/kscan_touch_detect.c` / `.h` — virtual kscan driver for touch key
- `src/heartbeat_led.c` — optional heartbeat LED
- `src/input_processor_gestures.c` — original central-side gesture processor
- `src/input_processor_abs_to_rel.c` — standalone ABS→REL converter
- `dts/bindings/zmk,input-peripheral-gesture.yaml`
- `dts/bindings/zmk,kscan-touch-detect.yaml`

## Picking up in a new session

1. Read this file and `CLAUDE.md` in `zmk-config-toucan`
2. Check `git log --oneline -10` for recent changes
3. Config repo: `~/Documents/github/zmk-config-toucan`
4. After any change: commit + push to `main` — CI builds automatically on push
