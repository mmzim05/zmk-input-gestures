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
- Converts to `REL_X`/`REL_Y` deltas with `max-delta` clamping
- Maintains a 5-event velocity window for smooth inertial start velocity
- On touch start: cancels any running inertial animation, notifies `touch_kscan` (key press)
- On touch end (timeout): computes velocity with staleness check, starts inertial animation if above threshold, notifies `touch_kscan` (key release)
- Inertial animation: decays velocity by `decay-percent` per 16ms frame, injects `REL_X`/`REL_Y` via `input_report_rel()`
- Stops all `INPUT_EV_ABS` and non-REL events — only injected REL pairs cross BLE

### `zmk,kscan-touch-detect` (`src/kscan_touch_detect.c`)

Virtual kscan driver with one key at (row=0, col=0). Driven by `zmk_kscan_touch_report(dev, pressed)` called from `peripheral_gesture`. Wire into `zmk,kscan-composite` so the touch key looks like any real keyboard key and can carry any ZMK behavior (mo, tap-dance, macros, etc.).

### `zmk,input-processor-abs-to-rel` (`src/input_processor_abs_to_rel.c`)

Standalone ABS→REL converter (still available but no longer used in the Toucan config — superseded by `peripheral_gesture` on the right half).

### `zmk,input-processor-gestures` (`src/input_processor_gestures.c`)

Original gesture processor (tap, inertial cursor/scroll, circular scroll). Still used when the central needs to process gesture data — but not in the current Toucan config.

## DT binding properties — `zmk,input-peripheral-gesture`

| Property | Default | Meaning |
|---|---|---|
| `device` | required | Phandle to the Cirque device (used as source for `input_report_rel`) |
| `touch-key` | required | Phandle to `zmk,kscan-touch-detect` device |
| `max-delta` | 60 | Per-poll abs delta clamp (raw Pinnacle units) |
| `touch-timeout-ms` | 30 | Ms of silence before touch-end declared |
| `velocity-threshold` | 3 | Min velocity in tenths of raw_px/ms to start inertial |
| `decay-percent` | 9 | % speed lost per 16ms animation frame |
| `speed-scale` | 100 | Inertial start speed scale — set to match `zip_xy_scaler` numerator |

## Key files

- `src/peripheral_gesture.c` / `.h` — peripheral-side gesture processor
- `src/kscan_touch_detect.c` / `.h` — virtual kscan driver for touch key
- `src/input_processor_gestures.c` — original central-side gesture processor
- `src/input_processor_abs_to_rel.c` — standalone ABS→REL converter
- `dts/bindings/zmk,input-peripheral-gesture.yaml`
- `dts/bindings/zmk,kscan-touch-detect.yaml`

## Picking up in a new session

1. Read this file and `CLAUDE.md` in `zmk-config-toucan`
2. Check `git log --oneline -10` for recent changes
3. Config repo: `~/Documents/github/zmk-config-toucan`
4. After any change: commit + push to `main`, then trigger build in config repo
