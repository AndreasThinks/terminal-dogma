# Testing Terminal Dogma

## Host tests

Run the canonical host test suite:

```bash
bash tests/run.sh
```

This builds `tests/c/test_logic.c` against `src/c/td_logic.c` with the host C compiler (`-Wall -Wextra -Werror`) and runs `tests/js/test_phone.js` with Node against `src/pkjs/phone.js`. Neither needs the Pebble SDK.

**C tests cover:**

- Settings defaults, migrations, clamping, and avoiding flash writes for identical values.
- Weather freshness bands, timestamp validation, clock skew, cache recovery, refresh pacing, and bounded retry behavior.
- Missing health data, battery states, clock formatting, and link-flap filtering.
- MAGI votes and the launch/recovery transition state machine, including cooldown, cancellation, low battery, Quick View, and non-advancing clocks.
- Shake-to-show settings and migrations, forecast validation and expiry, weather labels and conversion, and heart-rate statistics with gaps.
- Shake-screen transitions: open, hold, reverse return, early return on another shake, reversing during the opening, debounce, and hard-cut behavior with animations off or low battery.

**JavaScript tests cover:**

- Recovering settings from the watch when phone storage is empty or corrupt.
- AppMessage serialization, bounded retries, lost callbacks, and same-kind coalescing.
- Weather caching and refresh behavior, including incomplete hourly forecasts, stale readings, request failures, and geolocation timeouts.
- Forecast encoding and validation, shake settings, and the configuration page.

## Emery emulator checks

With the Pebble SDK and Emery emulator available:

```bash
pebble build
PBW="build/$(basename "$PWD").pbw"
pebble install --emulator emery "$PBW"
pebble emu-tap --emulator emery
pebble screenshot --emulator emery emery.png
```

The PBW filename uses the checkout directory name. To test Shake to show, enable it in the watchface settings, trigger a tap with `pebble emu-tap --emulator emery`, then check the selected screen. Trigger another tap after the first 1.5 seconds to test early return. To reverse during opening, trigger the second tap after the debounce interval but before the opening transition finishes. Leave the screen open to check the timed return. Also check the hard cut with animations Off or low battery.

The emulator may have no Pebble Health history. In that case the heart-rate screen should say `NO HEART RATE DATA`; it must not invent readings. Emulator checks do not replace testing on a physical Pebble Time 2.

## 1.0.21 release verification

- `bash tests/run.sh`: 342 C checks and 104 JavaScript checks passed.
- `git diff --check` passed.
- A clean (`pebble clean` followed by `pebble build`) Pebble SDK 4.9.169 build completed. GCC emitted three `-Wzero-length-bounds` warnings at the Pebble `Tuplet` byte-array reads in `receive_forecast` (the SDK declares the variable-length `data` member as `data[0]`); the linker also emitted its RWX LOAD-segment warning. PBW metadata retained UUID `44201a31-161f-4e94-ad74-24429abe073e` and the Emery target.
- The candidate PBW was installed in a private Emery QEMU. The default heart-rate screen opened; timed return, a second-shake return, and reversal during the opening returned to the normal face. The emulator had no Health history and displayed `NO HEART RATE DATA`.
- Version 1.0.21 was verified published and listed on the existing Pebble Store listing.
- No physical-watch test was performed. Real-watch battery cost, timing, Health API behavior, phone GPS, HTTP, and AppMessage behavior remain hardware acceptance items.

## Historical 1.0.18 release verification

- Host tests: 193 C checks and 72 JavaScript checks passed.
- Built with SDK 4.9.169; PBW metadata had the existing UUID and Emery target.
- Private Emery QEMU captures checked the Quiet Time indicator absent, present, and present with Quick View open.
- The on-state was injected into `s_quiet_time` using GDB without changing source or the PBW. This verifies drawing, not a real firmware Quiet Time toggle. The normal minute tick subsequently cleared the injected state using the firmware API.
- A physical-watch Quiet Time on/off check remained outstanding at that release. The indicator could take up to one minute to reflect a toggle.
