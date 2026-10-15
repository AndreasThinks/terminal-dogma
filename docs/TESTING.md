# Testing Terminal Dogma

## Host tests

```bash
tests/run.sh
```

This builds `tests/c/test_logic.c` against `src/c/td_logic.c` with the host C compiler (`-Wall -Wextra -Werror`) and runs `tests/js/test_phone.js` with Node against `src/pkjs/phone.js`. Neither needs the Pebble SDK.

**C tests cover:**
- Settings migration from 1.0.16. The animation default is pinned, and only the new key is written.
- Clamping, and identical resends causing no flash writes.
- Weather freshness bands (fresh, stale at 60 minutes, none at 6 hours) and clock-skew handling.
- Weather timestamp acceptance, and the 30-minute refresh with no request while the cache is fresh.
- No requests while disconnected, and the bounded backoff schedule.
- Missing health data shown as `--` (no placeholders).
- Battery states and labels, and 12-hour versus 24-hour formatting.
- Link-flap filtering.
- Honest MAGI votes.
- The transition state machine:
  - Persisted launch cooldown under rapid relaunch.
  - Off never starts anything, and toggling Off mid-sequence stops it.
  - Subtle, Quick View and low battery shorten the launch. Critical battery skips it.
  - Busy and cooldown rules, and abort on Quick View, Off or critical battery mid-sequence.
  - Clock moving backwards.
  - Guaranteed termination with stuck, jumping or negative clocks.
  - Check order.
  - A simulated 12-hour day with 240 relaunches.

**JS tests cover:**
- Lost phone storage asks the watch instead of pushing defaults.
- 1.0.16-era saved settings are resent without inventing new keys.
- Corrupt storage.
- AppMessage serialisation: one message in flight, retry backoff then give up, a watchdog for lost callbacks, and same-kind coalescing.
- No GPS or HTTP on relaunch with recently delivered weather. A cached reading is resent with its original observation time.
- Overlapping fetch guard, and HTTP error, timeout and parse failures.
- A hung geolocation is abandoned, and late replies are ignored.
- No phone-side timers are left running.

**Not covered by host tests:**
- Drawing code in `pebble-watchface.c` and the Pebble service wiring. The emulator captures below cover these.

## 1.0.18 release verification

- Host tests: 193 C checks and 72 JavaScript checks, all passing.
- Built with SDK 4.9.169 after `pebble clean`; PBW metadata verified as 1.0.18 with the existing UUID and Emery target.
- Actual private Emery QEMU framebuffer captures verify the final PBW with the indicator absent, present, and present with Quick View open.
- The on-state was injected into `s_quiet_time` using GDB, without modifying source or the PBW. This verifies drawing, not a real firmware Quiet Time toggle. The normal minute tick subsequently cleared the injected state using the firmware API.
- A real-watch Quiet Time on/off check remains outstanding. The indicator can take up to one minute to reflect a toggle.
- The SDK linker emits its RWX LOAD-segment warning; the build completes successfully.

## Production build

```bash
pebble build   # SDK 4.9.169, emery only
```

## Emulator evidence (private Emery QEMU)

The shared `pebble install --emulator emery` path wedged on the development workstation (see `exploration/REVIEW.md`). Captures use the private helper from the exploration tree instead:
- `exploration/scripts/emu.sh` runs its own QEMU process on ports 47801-47803, with its own SPI flash copy in `/tmp/td-emu`, and installs over the Pebble serial protocol.
- `exploration/scripts/capture_release.py` drives the release scenarios. It boots a fresh QEMU on a pristine flash copy per scenario, and records the framebuffer with QEMU `screendump` (about 41 ms per grab).
- Watch-side decisions are read from the face's own `TD ...;` log lines on the firmware console.

Run it with the checkout-local venv (Pillow; `make_transition_media.py` also needs imageio and ffmpeg):

```bash
.venv/bin/python -I exploration/scripts/capture_release.py PBW OUT_DIR [scenario ...]
.venv/bin/python -I exploration/scripts/make_transition_media.py OUT_DIR/launch/launch-full --gif X.gif --mp4 X.mp4
```

The scenarios are:

| Scenario | What it shows |
|---|---|
| `launch` | First activation runs the full sequence. Two relaunches run nothing (cooldown). The first fresh weather and a repeated fresh weather update start nothing. Steady screenshots cover 24h, 12h, charging, low, critical and stale weather. |
| `recovery` | Critical-battery launch is skipped, then weather recovery triggers the MAGI-only response. Repeated weather does nothing. A relaunch then runs the full sequence with real data. |
| `off` | Off: relaunch and data recovery start nothing. |
| `off-mid`, `quickview-mid` | Off or Quick View arriving during the launch sequence. |
| `quickview-launch`, `low-battery` | Launch shortened to the MAGI-only response. |

**Emulator limitations:**
- There is no phone (no pypkjs). Weather and settings are injected with `pebble send-app-message`. Repeated `--int` flags override each other, so pass all `KEY=VALUE` pairs to a single `--int`.
- Battery is reported in 10% steps.
- `emu-bt-connection --connected no` wedges the protocol link on this firmware, so `LINK LOST` and link-restored are covered by code review and host tests only.
- Health returns 0 steps and no heart rate.
- Displayed time follows the host clock.

## Not tested

- Real-watch battery cost, frame timing and Health API behaviour.
- Real phone GPS, HTTP and AppMessage behaviour.

These need hardware acceptance on a Pebble Time 2 with the phone app.
