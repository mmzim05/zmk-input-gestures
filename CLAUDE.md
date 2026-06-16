# zmk-input-gestures

Custom ZMK input processor module for the Toucan split keyboard. Adds gesture support (tap, inertial cursor, inertial scroll, circular scroll) on top of the Cirque Pinnacle trackpad.

## What was done

- Added `zmk,input-processor-abs-to-rel` — converts ABS_X/Y events from the Cirque (absolute mode) into REL_X/Y deltas, with max-delta clamping and sub-pixel accumulators so small movements aren't lost to truncation
- Fixed inertial cursor velocity estimation: replaced single-event snapshot with a 5-event circular buffer (`vel_dx/dy/dt`) that averages the last 5 event pairs — eliminates outlier spikes at lift
- Fixed inertial cursor speed: added `inertial-cursor-speed-scale` property (percent) applied to starting velocity so inertial animation matches live cursor speed (compensates for `zip_xy_scaler` being bypassed during HID output)
- Rescaled velocity threshold to tenths of raw_px/ms so small values are expressible (DT value 3 = 0.3 raw_px/ms)

## Key files

- `src/input_processor_abs_to_rel.c` — ABS→REL converter
- `src/inertial_cursor.c` / `src/inertial_cursor.h` — inertial animation, velocity window
- `dts/bindings/zmk,input-processor-gestures.yaml` — gesture processor DT binding
- `dts/bindings/zmk,input-processor-abs-to-rel.yaml` — abs-to-rel DT binding

## Picking up in a new session

1. Read this file and `CLAUDE.md` in `zmk-config-toucan`
2. Check `git log --oneline -10` to see recent changes
3. The companion config repo is `mmzim05/zmk-config-toucan` at `~/Documents/github/zmk-config-toucan`
4. After any code change: commit + push to `main`, then trigger a build in the config repo and download firmware
