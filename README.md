# Pebble Watchface

Starter Pebble watchface project for Emery, Pebble Time 2, using the `pebble-watchface` Hermes skill.

## Local setup

The Z620 has:

- `pebble-tool` installed via `uv tool install pebble-tool --python 3.13`
- Pebble SDK `4.9.169` installed and active
- Reference skill repo cloned at `~/.hermes/reference-repos/pebble-watchface-agent-skill`

## Build

```bash
pebble build
```

The built `.pbw` lands under `build/`.

## Test

```bash
pebble install --emulator emery
pebble screenshot --no-open --emulator emery screenshot_emery.png
```

## Notes for future work

- Target platform is `emery`, 200x228, 64-color rectangular display.
- Keep `tick_timer_service_subscribe(MINUTE_UNIT, ...)` unless seconds are explicitly needed.
- Use `layer_get_bounds()` for layout. No hardcoded 144x168 assumptions.
