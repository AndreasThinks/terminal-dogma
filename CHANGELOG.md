# Changelog

## 1.0.18

- Quiet Time indicator: a cyan crescent moon and `QUIET` tag in the top bar while Quiet Time is on, checked every minute.

## 1.0.17

The name is now Terminal Dogma everywhere. The UUID and Store listing are unchanged.

**Honest data**
- Removed the placeholder heart rate (72) and step count (7,061).
  - BIO shows the watch's filtered heart rate, which is at most 15 minutes old, or `--` when there is none.
  - Steps show `--` when Health data is unavailable.
- Weather survives relaunches. The watch stores the reading and its observation time.
  - After an hour without an update the weather turns grey and reads `STALE`.
  - After six hours it shows `--°`, whatever the SYNC rail is set to.

**Useful state**
- Numeric battery tag: `PWR 70%`.
  - Yellow `LOW` at 20% or less.
  - Inverted red plate at 10% or less.
  - `CHG` while charging, `EXT` when plugged in.
- A `LINK LOST` tag appears while the phone is disconnected.
- The 12-hour clock shows AM/PM in the `LOCAL TIME` label.
- The PWR chevrons are slightly narrower so they no longer touch the scale labels.

**Reliability**
- Settings are no longer reset when the phone app's storage is cleared. The phone asks the watch for its settings instead of pushing defaults.
- The watch writes settings to flash only when they change.
- The phone sends one message at a time to the watch, with bounded retries and timeouts. This fixes settings and weather messages colliding at launch.
- Weather requests are paced:
  - Refresh every 30 minutes.
  - Bounded backoff (2, 4, 8, 16, then 30 minutes) when the phone does not answer.
  - Nothing is sent while disconnected.
  - No GPS or HTTP on relaunch when recent weather is already on the watch.

**Transitions (new, setting: Off / Subtle / Full, default Full)**
- Full plays a short NERV system-boot and MAGI-check sequence when the face is first shown. Real power, link and data checks resolve as the HUD builds, then the original face returns.
- It plays at most once every 30 minutes, so notifications and app switches do not replay it.
- A brief MAGI-only check plays when the phone link or weather data genuinely recovers. Routine updates do not trigger it.
- Shortened or skipped at low battery and with Quick View. Off uses no animation timers.

Store release-notes text (short form):

> Honest health and weather states (no placeholder numbers, STALE weather), battery % with low/charging states, LINK LOST indicator, AM/PM in 12h mode, more reliable settings and weather sync, and optional MAGI/system-boot transitions (Off/Subtle/Full).
