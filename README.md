# Terminal Dogma

A NERV / Evangelion-inspired Pebble Time 2 watchface for **Emery** devices, 200×228.

The face is built as a dense MAGI-style HUD: large readable time, NERV logo block, PWR chevrons, BIO monitor, configurable lower data modules, live weather, and a slanted SYNC rail.

![Terminal Dogma Emery preview](emery_screenshot.png)

## Features

- Pebble Time 2 / Emery only.
- Large system-font time display using `LECO_42_NUMBERS`.
- NERV logo asset and MAGI/PWR HUD framing.
- Battery-backed PWR chevrons.
- BIO monitor using current heart rate when available.
- Configurable bottom modules:
  - Date | Steps | Weather
  - Date | Battery | Weather
  - Date | Steps | Battery
- Live weather from Open-Meteo via phone GPS.
- Weather unit setting:
  - Celsius, e.g. `18°C`
  - Fahrenheit, e.g. `64°F`
- Configurable HUD accent color:
  - NERV red
  - Amber
  - Green
  - Cyan
- Configurable SYNC rail source:
  - **Data freshness:** fills after a successful phone/weather update and drains over 30 minutes.
  - **Steps progress:** fills toward a 10,000-step daily goal.
  - **Battery level:** mirrors watch battery percentage.
  - **Aesthetic diagnostic:** fixed at 85% for pure HUD flavour.

## What SYNC means

`SYNC` is a configurable system rail styled as a NERV sync-ratio meter. By default it shows external data freshness: a full rail means the watch recently received phone/weather data, while an empty rail means the data is stale or unavailable.

Users can change it in settings to show step progress, battery level, or a fixed diagnostic state instead.

## Repository layout

```text
package.json                  Pebble app metadata, capabilities, message keys
src/c/pebble-watchface.c      Native watchface implementation
src/pkjs/index.js             PebbleKit JS weather, settings UI, AppMessage bridge
resources/images/             NERV logo asset
scripts/render_preview.py     Deterministic 200×228 preview renderer
emery_screenshot.png          Current App Store screenshot
preview_sync_ratio.png        Local deterministic preview output
wscript                       Pebble SDK build config
```

## Build

```bash
pebble build
```

The PBW is written to:

```text
build/pebble-watchface.pbw
```

## Preview

The Emery emulator install/screenshot path is unreliable on this workstation, so layout verification uses a deterministic renderer that mirrors the C geometry closely enough for spacing checks.

```bash
uv run python scripts/render_preview.py
cp preview_sync_ratio.png emery_screenshot.png
```

The preview is useful for layout, clipping, and screenshot generation. It is **not** font-perfect Pebble emulator output.

## Publish

Bump the version in `package.json`, build, regenerate `emery_screenshot.png`, then publish:

```bash
pebble publish \
  --non-interactive \
  --no-gif-all-platforms \
  --is-published \
  --release-notes 'Release notes here' \
  --screenshots emery_screenshot.png
```

Notes:

- Re-publishing the same version fails. Always bump the version first.
- Screenshot filenames must start with the platform name, e.g. `emery_screenshot.png`.
- `pebble publish --description` does not update the public description for an existing app. It is effectively new-app metadata. Use the dashboard for existing app description edits.

## Development notes

- Keep `MINUTE_UNIT` for the tick timer unless seconds are explicitly required.
- Use Pebble system fonts for small labels. 1×1 hand-drawn pixel text disappears in QEMU.
- Weather should show `--°` until real data arrives. No plausible fake live weather defaults.
- Keep the deterministic preview renderer in sync with C geometry whenever layout changes.
