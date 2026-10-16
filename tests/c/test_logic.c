/* Host tests for src/c/td_logic.c. Build and run: tests/run.sh */
#include <stdio.h>
#include <string.h>

#include "../../src/c/td_logic.h"

static int s_failures, s_checks;

#define CHECK(cond) do { s_checks++; if (!(cond)) { s_failures++; \
  printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(got, want) do { s_checks++; if (strcmp((got), (want)) != 0) { s_failures++; \
  printf("  FAIL %s:%d: got \"%s\" want \"%s\"\n", __FILE__, __LINE__, (got), (want)); } } while (0)
#define TEST(name) static void name(void)
#define RUN(name) do { int before = s_failures; name(); printf("%s %s\n", s_failures == before ? "ok  " : "FAIL", #name); } while (0)

#define MIN 60
#define HOUR 3600
static const int64_t T0 = 1760000000; /* arbitrary 2025 epoch */

static TdAnimEnv env(int motion, int batt, bool charging, bool obstructed) {
  TdAnimEnv e = {motion, batt, charging, obstructed};
  return e;
}

/* Runs a started sequence to completion with a fixed step; returns steps taken. */
static int run_to_end(TdAnim *a, int dt) {
  int n = 0;
  while (td_anim_step(a, dt)) {
    n++;
    if (n > 10000) break;
  }
  return n + 1;
}

/* ---------- settings ---------- */

TEST(settings_upgrade_from_1_0_16_pins_animation_default) {
  TdSettings s;
  int stored[TD_SETTING_COUNT] = {2, 1, 3, 1, 2, 0, 0, 0};
  uint32_t present = 0x1F; /* the five 1.0.16 keys */
  uint32_t write = td_settings_load(&s, stored, present, 1);
  CHECK(s.v[TD_SET_CLOCK] == TD_CLOCK_12H);
  CHECK(s.v[TD_SET_LAYOUT] == TD_LAYOUT_BATTERY_WEATHER);
  CHECK(s.v[TD_SET_ACCENT] == TD_ACCENT_CYAN);
  CHECK(s.v[TD_SET_TEMP_UNIT] == TD_TEMP_F);
  CHECK(s.v[TD_SET_SYNC] == TD_SYNC_BATTERY);
  CHECK(s.v[TD_SET_MOTION] == TD_MOTION_FULL);
  CHECK(s.v[TD_SET_SHAKE] == TD_SHAKE_HEART);
  /* only the migrated keys are written */
  CHECK(write == ((1u << TD_SET_MOTION) | (1u << TD_SET_SHAKE) | (1u << TD_SET_SHAKE_FX)));
}

TEST(settings_schema2_keeps_user_choice_and_adds_heart_rate_default) {
  TdSettings s;
  int stored[TD_SETTING_COUNT] = {0, 0, 0, 0, 0, TD_MOTION_OFF, 0, 0};
  uint32_t write = td_settings_load(&s, stored, 0x3F, 2);
  CHECK(s.v[TD_SET_MOTION] == TD_MOTION_OFF);
  CHECK(s.v[TD_SET_SHAKE] == TD_SHAKE_HEART);
  CHECK(s.v[TD_SET_SHAKE_FX] == TD_FX_SYNC_GRAPH);
  CHECK(write == ((1u << TD_SET_SHAKE) | (1u << TD_SET_SHAKE_FX)));
}

TEST(settings_schema3_keeps_shake_choice_and_writes_nothing) {
  TdSettings s;
  int stored[TD_SETTING_COUNT] = {0, 0, 0, 0, 0, TD_MOTION_FULL, TD_SHAKE_HEART, TD_FX_EMERGENCY_HEX};
  uint32_t write = td_settings_load(&s, stored, 0xFF, 3);
  CHECK(s.v[TD_SET_SHAKE] == TD_SHAKE_HEART);
  CHECK(s.v[TD_SET_SHAKE_FX] == TD_FX_EMERGENCY_HEX);
  CHECK(write == 0);
  stored[TD_SET_SHAKE] = 3;
  stored[TD_SET_SHAKE_FX] = 2;
  write = td_settings_load(&s, stored, 0xFF, 3);
  CHECK(s.v[TD_SET_SHAKE] == TD_SHAKE_HEART); /* invalid and old default fall back to HR */
  CHECK(s.v[TD_SET_SHAKE_FX] == TD_FX_SYNC_GRAPH);
  CHECK(write == 0);
}

TEST(settings_schema3_default_off_migrates_to_heart_and_schema4_preserves_off_choice) {
  TdSettings s;
  int stored[TD_SETTING_COUNT] = {0, 0, 0, 0, 0, TD_MOTION_FULL, TD_SHAKE_OFF, TD_FX_SYNC_GRAPH};
  uint32_t write = td_settings_load(&s, stored, 0xFF, 3);
  CHECK(s.v[TD_SET_SHAKE] == TD_SHAKE_HEART);
  CHECK(s.v[TD_SET_SHAKE_FX] == TD_FX_SYNC_GRAPH);
  CHECK(write == (1u << TD_SET_SHAKE));
  stored[TD_SET_SHAKE] = TD_SHAKE_OFF;
  write = td_settings_load(&s, stored, 0xFF, 4);
  CHECK(s.v[TD_SET_SHAKE] == TD_SHAKE_OFF);
  CHECK(write == 0);
}

TEST(settings_fresh_install_and_clamping) {
  TdSettings s;
  int stored[TD_SETTING_COUNT] = {9, -1, 4, 7, 99, 3, 5, -2};
  uint32_t write = td_settings_load(&s, stored, 0, 0);
  TdSettings d;
  td_settings_defaults(&d);
  CHECK(td_settings_diff(&s, &d) == 0);
  CHECK(write == ((1u << TD_SET_MOTION) | (1u << TD_SET_SHAKE) | (1u << TD_SET_SHAKE_FX)));
  td_settings_load(&s, stored, 0xFF, 3); /* every stored value out of range */
  CHECK(td_settings_diff(&s, &d) == 0);
}

TEST(settings_identical_resend_needs_no_flash_write) {
  TdSettings saved, incoming;
  td_settings_defaults(&saved);
  incoming = saved;
  CHECK(td_settings_diff(&incoming, &saved) == 0);
  incoming.v[TD_SET_ACCENT] = TD_ACCENT_GREEN;
  CHECK(td_settings_diff(&incoming, &saved) == (1u << TD_SET_ACCENT));
}

/* ---------- weather ---------- */

TEST(weather_freshness_bands) {
  CHECK(td_weather_state(false, T0, T0) == TD_WX_NONE);
  CHECK(td_weather_state(true, 0, T0) == TD_WX_NONE);
  CHECK(td_weather_state(true, T0, T0) == TD_WX_FRESH);
  CHECK(td_weather_state(true, T0, T0 + 59 * MIN) == TD_WX_FRESH);
  CHECK(td_weather_state(true, T0, T0 + 60 * MIN) == TD_WX_STALE);
  CHECK(td_weather_state(true, T0, T0 + 6 * HOUR - 1) == TD_WX_STALE);
  CHECK(td_weather_state(true, T0, T0 + 6 * HOUR) == TD_WX_NONE);
  CHECK(td_weather_state(true, T0 + 2 * MIN, T0) == TD_WX_FRESH);   /* small skew */
  CHECK(td_weather_state(true, T0 + 2 * HOUR, T0) == TD_WX_STALE);  /* clock moved back */
}

TEST(weather_sync_rail_drains_over_30_minutes) {
  CHECK(td_sync_freshness_pct(true, T0, T0) == 100);
  CHECK(td_sync_freshness_pct(true, T0, T0 + 15 * MIN) == 50);
  CHECK(td_sync_freshness_pct(true, T0, T0 + 30 * MIN) == 0);
  CHECK(td_sync_freshness_pct(false, T0, T0) == 0);
}

TEST(weather_timestamp_acceptance) {
  CHECK(td_weather_accept_ts(false, 0, T0) == T0);
  CHECK(td_weather_accept_ts(true, T0 - 5 * MIN, T0) == T0 - 5 * MIN);
  CHECK(td_weather_accept_ts(true, T0 + 5 * MIN, T0) == T0);
  CHECK(td_weather_accept_ts(true, T0 - 2 * TD_WEATHER_EXPIRE_S, T0) == T0 - TD_WEATHER_EXPIRE_S);
}

TEST(weather_recovery_is_edge_triggered) {
  CHECK(td_weather_is_recovery(TD_WX_NONE, TD_WX_FRESH));
  CHECK(td_weather_is_recovery(TD_WX_STALE, TD_WX_FRESH));
  CHECK(!td_weather_is_recovery(TD_WX_FRESH, TD_WX_FRESH));
  CHECK(!td_weather_is_recovery(TD_WX_NONE, TD_WX_STALE));
}

TEST(weather_fresh_cache_makes_no_request) {
  TdWeatherSched s = {0, 0};
  CHECK(!td_weather_should_request(&s, true, true, T0, T0 + 29 * MIN));
  CHECK(td_weather_should_request(&s, true, true, T0, T0 + 30 * MIN));
  CHECK(td_weather_should_request(&s, true, false, 0, T0));
}

TEST(weather_never_requests_while_disconnected) {
  TdWeatherSched s = {0, 0};
  int requests = 0;
  for (int m = 0; m < 24 * 60; m++) {
    int64_t now = T0 + m * MIN;
    if (td_weather_should_request(&s, false, true, T0 - 2 * HOUR, now)) {
      requests++;
      td_weather_note_attempt(&s, now);
    }
  }
  CHECK(requests == 0);
}

TEST(weather_unanswered_requests_back_off_and_stay_bounded) {
  TdWeatherSched s = {0, 0};
  int64_t times[64];
  int n = 0;
  for (int m = 0; m < 3 * 60; m++) { /* three hours of minute ticks, phone never replies */
    int64_t now = T0 + m * MIN;
    if (td_weather_should_request(&s, true, false, 0, now)) {
      if (n < 64) times[n] = now;
      n++;
      td_weather_note_attempt(&s, now);
    }
  }
  /* gaps 2, 4, 8, 16, then 30 min: 0,2,6,14,30,60,90,120,150 -> 9 requests */
  CHECK(n == 9);
  CHECK(times[1] - times[0] == 2 * MIN);
  CHECK(times[2] - times[1] == 4 * MIN);
  CHECK(times[3] - times[2] == 8 * MIN);
  CHECK(times[4] - times[3] == 16 * MIN);
  CHECK(times[5] - times[4] == 30 * MIN);
  CHECK(times[8] - times[7] == 30 * MIN);
}

TEST(weather_success_and_reconnect_reset_backoff) {
  TdWeatherSched s = {0, 0};
  td_weather_note_attempt(&s, T0);
  td_weather_note_attempt(&s, T0 + 2 * MIN);
  CHECK(!td_weather_should_request(&s, true, false, 0, T0 + 3 * MIN));
  td_weather_note_reconnect(&s);
  CHECK(td_weather_should_request(&s, true, false, 0, T0 + 3 * MIN));
  td_weather_note_attempt(&s, T0 + 3 * MIN);
  td_weather_note_success(&s);
  CHECK(s.attempts == 0);
  CHECK(!td_weather_should_request(&s, true, true, T0 + 3 * MIN, T0 + 4 * MIN));
}

TEST(weather_formatting) {
  char buf[16];
  td_format_weather(buf, sizeof(buf), TD_WX_NONE, 18, TD_TEMP_C);
  CHECK_STR(buf, "--°");
  td_format_weather(buf, sizeof(buf), TD_WX_FRESH, 18, TD_TEMP_C);
  CHECK_STR(buf, "18°C");
  td_format_weather(buf, sizeof(buf), TD_WX_STALE, 18, TD_TEMP_F);
  CHECK_STR(buf, "64°F");
  td_format_weather(buf, sizeof(buf), TD_WX_FRESH, -5, TD_TEMP_F);
  CHECK_STR(buf, "23°F");
}

/* ---------- health ---------- */

TEST(health_missing_data_never_shows_placeholder_numbers) {
  char buf[16];
  td_format_steps(buf, sizeof(buf), (TdReading){false, 7061});
  CHECK_STR(buf, "--");
  td_format_steps(buf, sizeof(buf), (TdReading){true, 0});
  CHECK_STR(buf, "0");
  td_format_steps(buf, sizeof(buf), (TdReading){true, 7061});
  CHECK_STR(buf, "7,061");
  td_format_steps(buf, sizeof(buf), (TdReading){true, 12005});
  CHECK_STR(buf, "12,005");
  td_format_hr(buf, sizeof(buf), (TdReading){false, 72});
  CHECK_STR(buf, "--");
  td_format_hr(buf, sizeof(buf), (TdReading){true, 0});
  CHECK_STR(buf, "--");
  td_format_hr(buf, sizeof(buf), (TdReading){true, 64});
  CHECK_STR(buf, "64");
  CHECK(td_steps_pct((TdReading){false, 7061}, 10000) == 0);
  CHECK(td_steps_pct((TdReading){true, 5000}, 10000) == 50);
  CHECK(td_steps_pct((TdReading){true, 15000}, 10000) == 100);
}

/* ---------- power, clock, link ---------- */

TEST(power_states_and_labels) {
  char buf[16];
  CHECK(td_battery_state(68, false, false) == TD_BATT_NORMAL);
  CHECK(td_battery_state(20, false, false) == TD_BATT_LOW);
  CHECK(td_battery_state(10, false, false) == TD_BATT_CRITICAL);
  CHECK(td_battery_state(10, true, true) == TD_BATT_CHARGING);
  CHECK(td_battery_state(100, false, true) == TD_BATT_PLUGGED);
  td_format_power(buf, sizeof(buf), 68, TD_BATT_NORMAL);
  CHECK_STR(buf, "PWR 68%");
  td_format_power(buf, sizeof(buf), 15, TD_BATT_LOW);
  CHECK_STR(buf, "LOW 15%");
  td_format_power(buf, sizeof(buf), 8, TD_BATT_CRITICAL);
  CHECK_STR(buf, "LOW 8%");
  td_format_power(buf, sizeof(buf), 40, TD_BATT_CHARGING);
  CHECK_STR(buf, "CHG 40%");
  td_format_power(buf, sizeof(buf), 100, TD_BATT_PLUGGED);
  CHECK_STR(buf, "EXT 100%");
}

TEST(clock_12h_has_meridiem_24h_does_not) {
  char t[8], m[3];
  td_format_time(t, sizeof(t), m, 13, 5, false);
  CHECK_STR(t, "1:05");
  CHECK_STR(m, "PM");
  td_format_time(t, sizeof(t), m, 0, 0, false);
  CHECK_STR(t, "12:00");
  CHECK_STR(m, "AM");
  td_format_time(t, sizeof(t), m, 12, 30, false);
  CHECK_STR(t, "12:30");
  CHECK_STR(m, "PM");
  td_format_time(t, sizeof(t), m, 9, 7, true);
  CHECK_STR(t, "09:07");
  CHECK_STR(m, "");
}

TEST(link_flaps_ignored_real_outage_reported_once) {
  TdLink l = {true, 0};
  CHECK(!td_link_update(&l, false, T0));
  CHECK(!td_link_update(&l, true, T0 + 20)); /* 20 s flap */
  CHECK(!td_link_update(&l, false, T0 + 100));
  CHECK(td_link_update(&l, true, T0 + 100 + 2 * MIN));
  CHECK(!td_link_update(&l, true, T0 + 100 + 3 * MIN)); /* repeated connected: no edge */
}

/* ---------- MAGI checks ---------- */

TEST(magi_votes_are_honest) {
  CHECK(td_vote_power(68, false) == TD_VOTE_PASS);
  CHECK(td_vote_power(15, false) == TD_VOTE_LOW);
  CHECK(td_vote_power(15, true) == TD_VOTE_PASS);
  CHECK(td_vote_link(false) == TD_VOTE_LOST);
  CHECK(td_vote_data(TD_WX_STALE) == TD_VOTE_OLD);
  CHECK(td_vote_data(TD_WX_NONE) == TD_VOTE_NONE);
  CHECK_STR(td_vote_label(TD_VOTE_NONE), "NONE");
}

/* ---------- transition state machine ---------- */

TEST(anim_launch_then_rapid_relaunches_hit_persisted_cooldown) {
  TdAnim a;
  td_anim_init(&a, 0);
  TdAnimEnv e = env(TD_MOTION_FULL, 68, false, false);
  bool persist = false;
  CHECK(td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, &persist) == TD_DECIDE_START);
  CHECK(a.kind == TD_ANIM_BOOT);
  CHECK(persist && a.last_full_s == T0);
  run_to_end(&a, TD_FRAME_MS);
  int64_t stored = a.last_full_s;

  /* Notification closes: the watchface process restarts and reloads persisted state. */
  for (int i = 1; i <= 20; i++) {
    TdAnim r;
    td_anim_init(&r, stored);
    CHECK(td_anim_trigger(&r, TD_TRIG_LAUNCH, &e, T0 + i * 15, &persist) == TD_DECIDE_SKIP_COOLDOWN);
    CHECK(!persist && r.kind == TD_ANIM_NONE);
  }
  TdAnim late;
  td_anim_init(&late, stored);
  CHECK(td_anim_trigger(&late, TD_TRIG_LAUNCH, &e, T0 + 29 * MIN, &persist) == TD_DECIDE_SKIP_COOLDOWN);
  CHECK(td_anim_trigger(&late, TD_TRIG_LAUNCH, &e, T0 + 30 * MIN, &persist) == TD_DECIDE_START);
}

TEST(anim_off_never_starts_anything) {
  TdAnim a;
  td_anim_init(&a, 0);
  TdAnimEnv e = env(TD_MOTION_OFF, 68, false, false);
  bool persist = true;
  CHECK(td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, &persist) == TD_DECIDE_SKIP_OFF);
  CHECK(!persist);
  CHECK(td_anim_trigger(&a, TD_TRIG_LINK_RESTORED, &e, T0 + HOUR, &persist) == TD_DECIDE_SKIP_OFF);
  CHECK(td_anim_trigger(&a, TD_TRIG_DATA_RECOVERED, &e, T0 + 2 * HOUR, &persist) == TD_DECIDE_SKIP_OFF);
  CHECK(a.kind == TD_ANIM_NONE);
  CHECK(!td_anim_step(&a, TD_FRAME_MS)); /* nothing to schedule */
  CHECK(a.last_full_s == 0);             /* Off does not consume the cooldown */
}

TEST(anim_toggle_off_during_effect_cancels) {
  TdAnim a;
  td_anim_init(&a, 0);
  TdAnimEnv e = env(TD_MOTION_FULL, 68, false, false);
  td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL);
  CHECK(td_anim_step(&a, TD_FRAME_MS));
  td_anim_cancel(&a); /* what the watchface does when ANIMATION changes */
  CHECK(a.kind == TD_ANIM_NONE);
  CHECK(!td_anim_step(&a, TD_FRAME_MS));
  e.motion = TD_MOTION_OFF;
  CHECK(td_anim_trigger(&a, TD_TRIG_DATA_RECOVERED, &e, T0 + HOUR, NULL) == TD_DECIDE_SKIP_OFF);
}

TEST(anim_subtle_quickview_and_low_battery_shorten_to_magi) {
  TdAnim a;
  TdAnimEnv e;
  td_anim_init(&a, 0);
  e = env(TD_MOTION_SUBTLE, 68, false, false);
  CHECK(td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL) == TD_DECIDE_START && a.kind == TD_ANIM_MAGI);
  td_anim_init(&a, 0);
  e = env(TD_MOTION_FULL, 68, false, true); /* Quick View showing at launch */
  CHECK(td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL) == TD_DECIDE_START && a.kind == TD_ANIM_MAGI);
  td_anim_init(&a, 0);
  e = env(TD_MOTION_FULL, 15, false, false);
  CHECK(td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL) == TD_DECIDE_START && a.kind == TD_ANIM_MAGI);
  td_anim_init(&a, 0);
  e = env(TD_MOTION_FULL, 15, true, false); /* charging: full sequence allowed */
  CHECK(td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL) == TD_DECIDE_START && a.kind == TD_ANIM_BOOT);
  td_anim_init(&a, 0);
  e = env(TD_MOTION_FULL, 8, false, false);
  CHECK(td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL) == TD_DECIDE_SKIP_BATTERY && a.kind == TD_ANIM_NONE);
  CHECK(a.last_full_s == 0);
  e = env(TD_MOTION_FULL, 8, true, false);
  CHECK(td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL) == TD_DECIDE_START && a.kind == TD_ANIM_BOOT);
}

TEST(anim_environment_changes_abort_mid_sequence) {
  TdAnim a;
  TdAnimEnv e = env(TD_MOTION_FULL, 68, false, false);
  td_anim_init(&a, 0);
  CHECK(!td_anim_should_abort(&a, &e)); /* idle: nothing to abort */
  td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL);
  td_anim_step(&a, TD_FRAME_MS);
  CHECK(!td_anim_should_abort(&a, &e));
  TdAnimEnv qv = env(TD_MOTION_FULL, 68, false, true); /* Quick View appears mid-boot */
  CHECK(td_anim_should_abort(&a, &qv));
  TdAnimEnv off = env(TD_MOTION_OFF, 68, false, false);
  CHECK(td_anim_should_abort(&a, &off));
  TdAnimEnv crit = env(TD_MOTION_FULL, 9, false, false);
  CHECK(td_anim_should_abort(&a, &crit));
  TdAnimEnv crit_chg = env(TD_MOTION_FULL, 9, true, false);
  CHECK(!td_anim_should_abort(&a, &crit_chg));

  /* Quick View cancels either animation, including the logo-only response. */
  td_anim_init(&a, 0);
  TdAnimEnv subtle = env(TD_MOTION_SUBTLE, 68, false, false);
  td_anim_trigger(&a, TD_TRIG_LAUNCH, &subtle, T0, NULL);
  CHECK(a.kind == TD_ANIM_MAGI);
  subtle.obstructed = true;
  CHECK(td_anim_should_abort(&a, &subtle));
}

TEST(anim_trigger_while_running_is_ignored) {
  TdAnim a;
  td_anim_init(&a, 0);
  TdAnimEnv e = env(TD_MOTION_FULL, 68, false, false);
  td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL);
  td_anim_step(&a, TD_FRAME_MS);
  int t = a.t_ms;
  CHECK(td_anim_trigger(&a, TD_TRIG_LINK_RESTORED, &e, T0 + 1, NULL) == TD_DECIDE_SKIP_BUSY);
  CHECK(a.kind == TD_ANIM_BOOT && a.t_ms == t);
}

TEST(anim_recovery_is_rate_limited) {
  TdAnim a;
  td_anim_init(&a, 0);
  TdAnimEnv e = env(TD_MOTION_FULL, 68, false, false);
  td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL);
  run_to_end(&a, TD_FRAME_MS);
  CHECK(td_anim_trigger(&a, TD_TRIG_DATA_RECOVERED, &e, T0 + 10, NULL) == TD_DECIDE_SKIP_COOLDOWN);
  CHECK(td_anim_trigger(&a, TD_TRIG_LINK_RESTORED, &e, T0 + 4 * MIN, NULL) == TD_DECIDE_SKIP_COOLDOWN);
  CHECK(td_anim_trigger(&a, TD_TRIG_LINK_RESTORED, &e, T0 + 6 * MIN, NULL) == TD_DECIDE_START);
  CHECK(a.kind == TD_ANIM_MAGI);
  run_to_end(&a, TD_FRAME_MS);
  CHECK(td_anim_trigger(&a, TD_TRIG_DATA_RECOVERED, &e, T0 + 7 * MIN, NULL) == TD_DECIDE_SKIP_COOLDOWN);
  CHECK(td_anim_trigger(&a, TD_TRIG_DATA_RECOVERED, &e, T0 + 12 * MIN, NULL) == TD_DECIDE_START);
}

TEST(anim_recovery_cooldown_survives_relaunch) {
  TdAnim a;
  td_anim_init(&a, T0); /* relaunched just after a persisted launch sequence */
  TdAnimEnv e = env(TD_MOTION_FULL, 68, false, false);
  CHECK(td_anim_trigger(&a, TD_TRIG_DATA_RECOVERED, &e, T0 + 30, NULL) == TD_DECIDE_SKIP_COOLDOWN);
  CHECK(td_anim_trigger(&a, TD_TRIG_DATA_RECOVERED, &e, T0 + 5 * MIN, NULL) == TD_DECIDE_START);
}

TEST(anim_clock_moved_back_does_not_block_forever) {
  TdAnim a;
  td_anim_init(&a, T0 + 10 * HOUR); /* persisted value from "the future" */
  TdAnimEnv e = env(TD_MOTION_FULL, 68, false, false);
  CHECK(td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL) == TD_DECIDE_START);
}

TEST(anim_timer_always_terminates) {
  TdAnim a;
  TdAnimEnv e = env(TD_MOTION_FULL, 68, false, false);
  td_anim_init(&a, 0);
  td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL);
  int steps = run_to_end(&a, TD_FRAME_MS);
  CHECK(steps == (td_magi_timeline(TD_ANIM_BOOT)->duration + TD_FRAME_MS - 1) / TD_FRAME_MS);
  CHECK(a.kind == TD_ANIM_NONE);
  CHECK(!td_anim_step(&a, TD_FRAME_MS));

  td_anim_init(&a, 0);
  td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL);
  steps = run_to_end(&a, 0); /* stuck clock */
  CHECK(steps == td_anim_max_steps(TD_ANIM_BOOT));
  CHECK(a.kind == TD_ANIM_NONE);

  td_anim_init(&a, 0);
  td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL);
  steps = run_to_end(&a, 5000); /* wall-clock jump: counts as one nominal frame */
  CHECK(steps == (td_magi_timeline(TD_ANIM_BOOT)->duration + TD_FRAME_MS - 1) / TD_FRAME_MS);
  td_anim_init(&a, 0);
  td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL);
  steps = run_to_end(&a, -1000);
  CHECK(a.kind == TD_ANIM_NONE && steps < td_anim_max_steps(TD_ANIM_BOOT));
  CHECK(td_anim_max_steps(TD_ANIM_BOOT) < 60);
}

TEST(anim_checks_resolve_in_module_order) {
  TdAnim a;
  td_anim_init(&a, 0);
  TdAnimEnv e = env(TD_MOTION_FULL, 68, false, false);
  td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, T0, NULL);
  const TdMagiTimeline *tl = td_magi_timeline(TD_ANIM_BOOT);
  CHECK(tl->vote_ms[TD_CHECK_PWR] < tl->vote_ms[TD_CHECK_LINK]);
  CHECK(tl->vote_ms[TD_CHECK_LINK] < tl->vote_ms[TD_CHECK_DATA]);
  CHECK(tl->vote_ms[TD_CHECK_DATA] < tl->close_start && tl->close_end < tl->duration);
  CHECK(td_anim_due_votes(&a) == 0);
  while (a.t_ms < tl->vote_ms[TD_CHECK_PWR]) td_anim_step(&a, TD_FRAME_MS);
  CHECK(td_anim_due_votes(&a) == (1u << TD_CHECK_PWR));
  a.votes[TD_CHECK_PWR] = TD_VOTE_PASS;
  while (a.t_ms < tl->vote_ms[TD_CHECK_DATA]) td_anim_step(&a, TD_FRAME_MS);
  CHECK(td_anim_due_votes(&a) == ((1u << TD_CHECK_LINK) | (1u << TD_CHECK_DATA)));
  a.votes[TD_CHECK_LINK] = TD_VOTE_LOST;
  a.votes[TD_CHECK_DATA] = TD_VOTE_OLD;
  CHECK(td_anim_pass_count(&a) == 1);
}

/* A busy simulated day: relaunch every 3 minutes for 12 hours, a weather reply every
 * 30 minutes (fresh -> fresh), and two real phone outages. */
TEST(anim_simulated_day_is_bounded) {
  TdAnim a;
  td_anim_init(&a, 0);
  TdAnimEnv e = env(TD_MOTION_FULL, 68, false, false);
  TdLink link = {true, 0};
  TdWeatherState wx = TD_WX_NONE;
  int boots = 0, magis = 0;
  int64_t persisted = 0;
  for (int64_t t = 0; t < 12 * HOUR; t += MIN) {
    int64_t now = T0 + t;
    TdDecision d = TD_DECIDE_SKIP_OFF;
    if (t % (3 * MIN) == 0) {
      td_anim_init(&a, persisted); /* relaunch */
      d = td_anim_trigger(&a, TD_TRIG_LAUNCH, &e, now, NULL);
      persisted = a.last_full_s;
    }
    bool connected = !((t >= 2 * HOUR && t < 3 * HOUR) || (t >= 7 * HOUR && t < 7 * HOUR + 10 * MIN));
    if (d != TD_DECIDE_START && td_link_update(&link, connected, now)) {
      d = td_anim_trigger(&a, TD_TRIG_LINK_RESTORED, &e, now, NULL);
    }
    if (d != TD_DECIDE_START && connected && t % (30 * MIN) == 0) {
      TdWeatherState after = TD_WX_FRESH;
      if (td_weather_is_recovery(wx, after)) d = td_anim_trigger(&a, TD_TRIG_DATA_RECOVERED, &e, now, NULL);
      wx = after;
    }
    if (t == 3 * HOUR) wx = TD_WX_STALE; /* went stale during the outage */
    if (d == TD_DECIDE_START) {
      if (a.kind == TD_ANIM_BOOT) boots++; else magis++;
      run_to_end(&a, TD_FRAME_MS);
    }
  }
  CHECK(boots == 24);  /* once per 30 minutes, not once per relaunch (240 relaunches) */
  CHECK(magis <= 2);   /* only the two real outages / stale->fresh recoveries can add any */
  printf("    simulated day: %d launch sequences, %d MAGI responses\n", boots, magis);
}

/* ---------- forecast and heart-rate history ---------- */

static TdForecast forecast(int64_t start, int64_t ts, int count) {
  TdForecast f;
  memset(&f, 0, sizeof(f));
  f.start = (int32_t)start;
  f.ts = (int32_t)ts;
  f.count = (uint8_t)count;
  for (int i = 0; i < TD_FC_SLOTS; i++) {
    f.temp_c[i] = (int8_t)(15 - i);
    f.code[i] = 3;
  }
  return f;
}

TEST(forecast_skips_past_hours_and_expires) {
  TdForecast f = forecast(T0, T0, 12);
  CHECK(td_forecast_first(&f, T0) == 0);
  CHECK(td_forecast_first(&f, T0 + 59 * MIN) == 0);
  CHECK(td_forecast_first(&f, T0 + HOUR) == 1);
  CHECK(td_forecast_shown(&f, 0) == TD_FC_SHOWN);
  /* stale but not expired: hours gone are dropped, fewer remain */
  CHECK(td_forecast_first(&f, T0 + 5 * HOUR + 10 * MIN) == 5);
  CHECK(td_forecast_shown(&f, 5) == TD_FC_SHOWN);
  CHECK(td_forecast_shown(&f, 9) == 3);
  /* expired with the weather (6 h): nothing, never a stale forecast as current */
  CHECK(td_forecast_first(&f, T0 + 6 * HOUR) == -1);
  CHECK(td_forecast_shown(&f, -1) == 0);
  /* slot 0 still ahead (phone clock slightly ahead): start at slot 0 */
  CHECK(td_forecast_first(&f, T0 - 2 * MIN) == 0);
}

TEST(forecast_missing_or_corrupt_shows_nothing) {
  TdForecast f;
  memset(&f, 0, sizeof(f));
  CHECK(td_forecast_first(&f, T0) == -1);
  f = forecast(T0, T0, 0);
  CHECK(td_forecast_first(&f, T0) == -1);
  f = forecast(T0, T0, TD_FC_SLOTS + 1);
  CHECK(td_forecast_first(&f, T0) == -1);
  f = forecast(T0 - 3 * HOUR, T0, 3); /* every hour already past */
  CHECK(td_forecast_first(&f, T0) == -1);
  CHECK(td_forecast_first(NULL, T0) == -1);
}

TEST(forecast_rain_callout_and_labels) {
  TdForecast f = forecast(T0, T0, 12);
  CHECK(td_forecast_rain_at(&f, 0) == -1);
  f.rain_pct[3] = 49;
  f.rain_pct[4] = 50;
  CHECK(td_forecast_rain_at(&f, 0) == 4);
  CHECK(td_forecast_rain_at(&f, 4) == 0);
  f.rain_pct[4] = 0;
  f.rain_pct[8] = 90; /* beyond the six shown hours from slot 0 */
  CHECK(td_forecast_rain_at(&f, 0) == -1);
  CHECK_STR(td_wmo_label(0), "CLEAR");
  CHECK_STR(td_wmo_label(3), "CLOUD");
  CHECK_STR(td_wmo_label(45), "FOG");
  CHECK_STR(td_wmo_label(61), "RAIN");
  CHECK_STR(td_wmo_label(81), "RAIN");
  CHECK_STR(td_wmo_label(73), "SNOW");
  CHECK_STR(td_wmo_label(95), "STORM");
  CHECK_STR(td_wmo_label(255), "--");
  CHECK(td_temp_in_unit(17, TD_TEMP_C) == 17);
  CHECK(td_temp_in_unit(17, TD_TEMP_F) == 63);
  CHECK(td_temp_in_unit(-5, TD_TEMP_F) == 23);
}

TEST(hr_stats_ignore_missing_buckets) {
  int16_t none[TD_HR_BUCKETS] = {0};
  TdHrStats st = td_hr_stats(none, TD_HR_BUCKETS);
  CHECK(st.count == 0);
  int16_t some[TD_HR_BUCKETS] = {0, 70, 0, 110, 64, 0};
  st = td_hr_stats(some, TD_HR_BUCKETS);
  CHECK(st.count == 3);
  CHECK(st.min == 64 && st.max == 110);
  CHECK(st.avg == 81);
  /* scale: higher bpm is higher on screen, inside a 10% margin, flat range centred */
  int top = td_scale_y(110, 64, 110, 66, 165), bottom = td_scale_y(64, 64, 110, 66, 165);
  CHECK(top < bottom);
  CHECK(top >= 66 + 9 && bottom <= 165 - 9);
  CHECK(td_scale_y(200, 64, 110, 66, 165) == top);
  CHECK(td_scale_y(70, 70, 70, 66, 165) == (66 + 165) / 2);
}

/* ---------- shake screen ---------- */

static TdPeekEnv peek_env(int shake, int fx, int motion, int batt) {
  TdPeekEnv e = {shake, fx, motion, batt, false};
  return e;
}

static int peek_run(TdPeek *p, int dt) {
  int n = 0;
  while (td_peek_step(p, dt) && n < 10000) n++;
  return n + 1;
}

TEST(peek_off_ignores_shakes) {
  TdPeek p = {0};
  TdPeekEnv e = peek_env(TD_SHAKE_OFF, TD_FX_SYNC_GRAPH, TD_MOTION_FULL, 80);
  CHECK(td_peek_tap(&p, &e, 0) == TD_PEEK_NOOP);
  CHECK(!td_peek_open(&p));
  CHECK(!td_peek_step(&p, TD_FRAME_MS));
}

TEST(peek_opens_with_chosen_transition_then_holds) {
  for (int fx = TD_FX_SYNC_GRAPH; fx <= TD_FX_EMERGENCY_HEX; fx++) {
    TdPeek p = {0};
    TdPeekEnv e = peek_env(TD_SHAKE_HEART, fx, TD_MOTION_SUBTLE, 80);
    CHECK(td_peek_tap(&p, &e, 1000) == TD_PEEK_OPEN_FX);
    CHECK(p.phase == TD_PEEK_IN && p.fx == fx && p.screen == TD_SHAKE_HEART);
    int steps = peek_run(&p, TD_FRAME_MS);
    CHECK(p.phase == TD_PEEK_HOLD);
    CHECK(steps == (td_fx_duration(fx) + TD_FRAME_MS - 1) / TD_FRAME_MS);
  }
}

TEST(peek_cuts_without_frames_when_motion_off_or_battery_low) {
  TdPeek p = {0};
  TdPeekEnv e = peek_env(TD_SHAKE_WEATHER, TD_FX_EMERGENCY_HEX, TD_MOTION_OFF, 80);
  CHECK(td_peek_tap(&p, &e, 0) == TD_PEEK_OPEN_CUT);
  CHECK(p.phase == TD_PEEK_HOLD);
  CHECK(!td_peek_step(&p, TD_FRAME_MS));
  td_peek_close(&p);
  e = peek_env(TD_SHAKE_WEATHER, TD_FX_SYNC_GRAPH, TD_MOTION_FULL, TD_BATT_LOW_PCT);
  CHECK(td_peek_tap(&p, &e, 0) == TD_PEEK_OPEN_CUT);
  td_peek_close(&p);
  e.charging = true;
  CHECK(td_peek_tap(&p, &e, 0) == TD_PEEK_OPEN_FX);
}

TEST(peek_one_shake_burst_does_not_close_it) {
  TdPeek p = {0};
  TdPeekEnv e = peek_env(TD_SHAKE_WEATHER, TD_FX_SYNC_GRAPH, TD_MOTION_FULL, 80);
  CHECK(td_peek_tap(&p, &e, 5000) == TD_PEEK_OPEN_FX);
  CHECK(td_peek_tap(&p, &e, 5200) == TD_PEEK_NOOP);
  CHECK(td_peek_tap(&p, &e, 5000 + TD_PEEK_DEBOUNCE_MS - 1) == TD_PEEK_NOOP);
  CHECK(td_peek_open(&p));
  CHECK(td_peek_tap(&p, &e, 5000 + TD_PEEK_DEBOUNCE_MS) == TD_PEEK_CLOSE);
  CHECK(!td_peek_open(&p));
  CHECK(!td_peek_step(&p, TD_FRAME_MS));
  /* a clock that jumped backwards cannot keep it open */
  CHECK(td_peek_tap(&p, &e, 9000) == TD_PEEK_OPEN_FX);
  CHECK(td_peek_tap(&p, &e, 1000) == TD_PEEK_CLOSE);
}

TEST(peek_transition_always_terminates) {
  int dts[] = {0, 1, -50, 100000, TD_FRAME_MS};
  for (unsigned i = 0; i < sizeof(dts) / sizeof(dts[0]); i++) {
    for (int fx = TD_FX_SYNC_GRAPH; fx <= TD_FX_EMERGENCY_HEX; fx++) {
      TdPeek p = {0};
      TdPeekEnv e = peek_env(TD_SHAKE_HEART, fx, TD_MOTION_FULL, 80);
      td_peek_tap(&p, &e, 0);
      CHECK(peek_run(&p, dts[i]) <= td_fx_max_steps(fx));
      CHECK(p.phase == TD_PEEK_HOLD);
    }
  }
}

int main(void) {
  RUN(settings_upgrade_from_1_0_16_pins_animation_default);
  RUN(settings_schema2_keeps_user_choice_and_adds_heart_rate_default);
  RUN(settings_schema3_keeps_shake_choice_and_writes_nothing);
  RUN(settings_schema3_default_off_migrates_to_heart_and_schema4_preserves_off_choice);
  RUN(settings_fresh_install_and_clamping);
  RUN(settings_identical_resend_needs_no_flash_write);
  RUN(weather_freshness_bands);
  RUN(weather_sync_rail_drains_over_30_minutes);
  RUN(weather_timestamp_acceptance);
  RUN(weather_recovery_is_edge_triggered);
  RUN(weather_fresh_cache_makes_no_request);
  RUN(weather_never_requests_while_disconnected);
  RUN(weather_unanswered_requests_back_off_and_stay_bounded);
  RUN(weather_success_and_reconnect_reset_backoff);
  RUN(weather_formatting);
  RUN(health_missing_data_never_shows_placeholder_numbers);
  RUN(power_states_and_labels);
  RUN(clock_12h_has_meridiem_24h_does_not);
  RUN(link_flaps_ignored_real_outage_reported_once);
  RUN(magi_votes_are_honest);
  RUN(anim_launch_then_rapid_relaunches_hit_persisted_cooldown);
  RUN(anim_off_never_starts_anything);
  RUN(anim_toggle_off_during_effect_cancels);
  RUN(anim_subtle_quickview_and_low_battery_shorten_to_magi);
  RUN(anim_environment_changes_abort_mid_sequence);
  RUN(anim_trigger_while_running_is_ignored);
  RUN(anim_recovery_is_rate_limited);
  RUN(anim_recovery_cooldown_survives_relaunch);
  RUN(anim_clock_moved_back_does_not_block_forever);
  RUN(anim_timer_always_terminates);
  RUN(anim_checks_resolve_in_module_order);
  RUN(anim_simulated_day_is_bounded);
  RUN(forecast_skips_past_hours_and_expires);
  RUN(forecast_missing_or_corrupt_shows_nothing);
  RUN(forecast_rain_callout_and_labels);
  RUN(hr_stats_ignore_missing_buckets);
  RUN(peek_off_ignores_shakes);
  RUN(peek_opens_with_chosen_transition_then_holds);
  RUN(peek_cuts_without_frames_when_motion_off_or_battery_low);
  RUN(peek_one_shake_burst_does_not_close_it);
  RUN(peek_transition_always_terminates);
  printf("C logic: %d checks, %d failures\n", s_checks, s_failures);
  return s_failures ? 1 : 0;
}
