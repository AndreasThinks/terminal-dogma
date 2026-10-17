# Changelog

## 1.0.21

- The shake screen now returns to the face with the same transition played in reverse, after the 10-second hold or on a second shake. A shake during the opening reverses it from the frame on screen.
- With animations Off or a low battery the return is a hard cut, as the opening is.

## 1.0.20

- Fix forecast recovery when an older phone cache contains a current temperature but no hourly forecast; it now fetches the missing forecast instead of treating the cache as complete.
- Heart rate, last 3 hours, is now the default shake screen. Sync Graph remains the default transition.
- Existing 1.0.19 installs migrate the old Off default to Heart rate. Other saved shake-screen and transition choices are preserved.

## 1.0.19

- Shake to show (optional, off by default): a wrist shake opens either the next six hours of weather or a three-hour heart-rate graph, then returns to the face after 10 seconds. Shake again to close it.
  - Two Evangelion transitions to choose from: **Sync Graph** (sync waves collapsing onto the data) and **Emergency Hex** (EMERGENCY panel cascade).
  - Animations Off or a low battery opens it with a hard cut.
  - Hours already past are never shown; heart-rate slots without a reading stay as gaps.
- The phone now fetches a 12-hour hourly forecast (temperature, rain chance, condition) with the current temperature, in the same request.

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
