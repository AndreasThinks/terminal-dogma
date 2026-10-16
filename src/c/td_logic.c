#include "td_logic.h"

#include <stdio.h>
#include <string.h>

/* ---------- Settings ---------- */

static const int s_defaults[TD_SETTING_COUNT] = {
  TD_CLOCK_SYSTEM, TD_LAYOUT_STEPS_WEATHER, TD_ACCENT_RED, TD_TEMP_C, TD_SYNC_FRESHNESS,
  TD_MOTION_FULL, TD_SHAKE_OFF, TD_FX_SYNC_GRAPH,
};
static const int s_max[TD_SETTING_COUNT] = {
  TD_CLOCK_12H, TD_LAYOUT_STEPS_BATTERY, TD_ACCENT_CYAN, TD_TEMP_F, TD_SYNC_AESTHETIC,
  TD_MOTION_FULL, TD_SHAKE_HEART, TD_FX_EMERGENCY_HEX,
};

int td_setting_default(int id) {
  return (id >= 0 && id < TD_SETTING_COUNT) ? s_defaults[id] : 0;
}

void td_settings_defaults(TdSettings *s) {
  for (int i = 0; i < TD_SETTING_COUNT; i++) s->v[i] = s_defaults[i];
}

int td_setting_clamp(int id, int value) {
  if (id < 0 || id >= TD_SETTING_COUNT) return 0;
  return (value >= 0 && value <= s_max[id]) ? value : s_defaults[id];
}

uint32_t td_settings_load(TdSettings *s, const int *stored, uint32_t present_mask, int stored_schema) {
  uint32_t write_mask = 0;
  for (int i = 0; i < TD_SETTING_COUNT; i++) {
    s->v[i] = (present_mask & (1u << i)) ? td_setting_clamp(i, stored[i]) : s_defaults[i];
  }
  /* 1 -> 2: animations added. Pin the default explicitly so a later default change
   * cannot silently alter an upgraded install. */
  if (stored_schema < 2 && !(present_mask & (1u << TD_SET_MOTION))) {
    s->v[TD_SET_MOTION] = s_defaults[TD_SET_MOTION];
    write_mask |= 1u << TD_SET_MOTION;
  }
  /* 2 -> 3: shake screen added, off unless the user turns it on. */
  if (stored_schema < 3) {
    for (int i = TD_SET_SHAKE; i <= TD_SET_SHAKE_FX; i++) {
      if (present_mask & (1u << i)) continue;
      s->v[i] = s_defaults[i];
      write_mask |= 1u << i;
    }
  }
  return write_mask;
}

uint32_t td_settings_diff(const TdSettings *a, const TdSettings *b) {
  uint32_t mask = 0;
  for (int i = 0; i < TD_SETTING_COUNT; i++) {
    if (a->v[i] != b->v[i]) mask |= 1u << i;
  }
  return mask;
}

/* ---------- Weather ---------- */

TdWeatherState td_weather_state(bool have, int64_t ts, int64_t now) {
  if (!have || ts <= 0) return TD_WX_NONE;
  int64_t age = now - ts;
  if (age < -TD_WEATHER_SKEW_S) return TD_WX_STALE; /* clock moved back: age unknown */
  if (age >= TD_WEATHER_EXPIRE_S) return TD_WX_NONE;
  if (age >= TD_WEATHER_STALE_S) return TD_WX_STALE;
  return TD_WX_FRESH;
}

int td_sync_freshness_pct(bool have, int64_t ts, int64_t now) {
  if (!have || ts <= 0) return 0;
  int64_t age = now - ts;
  if (age < 0) age = 0;
  if (age >= TD_SYNC_DRAIN_S) return 0;
  return (int)(100 - (age * 100) / TD_SYNC_DRAIN_S);
}

int64_t td_weather_accept_ts(bool has_msg_ts, int64_t msg_ts, int64_t now) {
  if (!has_msg_ts || msg_ts <= 0 || msg_ts > now) return now;
  if (now - msg_ts > TD_WEATHER_EXPIRE_S) return now - TD_WEATHER_EXPIRE_S;
  return msg_ts;
}

bool td_weather_is_recovery(TdWeatherState before, TdWeatherState after) {
  return before != TD_WX_FRESH && after == TD_WX_FRESH;
}

int td_weather_backoff_s(int attempts) {
  /* 2, 4, 8, 16, then every 30 minutes. */
  if (attempts <= 0) return 0;
  if (attempts >= 5) return 30 * 60;
  return (60 << attempts);
}

bool td_weather_should_request(const TdWeatherSched *s, bool connected, bool have, int64_t ts,
                               int64_t now) {
  if (!connected) return false;
  bool need = td_weather_state(have, ts, now) != TD_WX_FRESH || (now - ts) >= TD_WEATHER_REFRESH_S;
  if (!need) return false;
  if (s->attempts <= 0 || s->last_attempt > now) return true;
  return (now - s->last_attempt) >= td_weather_backoff_s(s->attempts);
}

void td_weather_note_attempt(TdWeatherSched *s, int64_t now) {
  s->last_attempt = now;
  if (s->attempts < 100) s->attempts++;
}

void td_weather_note_success(TdWeatherSched *s) {
  s->attempts = 0;
}

void td_weather_note_reconnect(TdWeatherSched *s) {
  s->attempts = 0;
}

/* ---------- Hourly forecast ---------- */

int td_forecast_first(const TdForecast *f, int64_t now) {
  if (!f || f->start <= 0 || f->count == 0 || f->count > TD_FC_SLOTS) return -1;
  if (td_weather_state(true, f->ts, now) == TD_WX_NONE) return -1;
  int64_t since = now - f->start;
  int first = since < 0 ? 0 : (int)(since / 3600);
  return first < f->count ? first : -1;
}

int td_forecast_shown(const TdForecast *f, int first) {
  if (first < 0) return 0;
  int n = f->count - first;
  return n > TD_FC_SHOWN ? TD_FC_SHOWN : n;
}

int td_forecast_rain_at(const TdForecast *f, int first) {
  int n = td_forecast_shown(f, first);
  for (int i = 0; i < n; i++) {
    if (f->rain_pct[first + i] >= TD_FC_RAIN_PCT) return i;
  }
  return -1;
}

const char *td_wmo_label(int code) {
  if (code < 0) return "--";
  if (code <= 1) return "CLEAR";
  if (code <= 3) return "CLOUD";
  if (code == 45 || code == 48) return "FOG";
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "RAIN";
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "SNOW";
  if (code >= 95 && code <= 99) return "STORM";
  return "--";
}

int td_temp_in_unit(int temp_c, int unit) {
  if (unit != TD_TEMP_F) return temp_c;
  return ((temp_c * 9) + (temp_c >= 0 ? 2 : -2)) / 5 + 32;
}

/* ---------- Heart-rate history ---------- */

TdHrStats td_hr_stats(const int16_t *bpm, int n) {
  TdHrStats st = {0, 0, 0, 0};
  int sum = 0;
  for (int i = 0; i < n; i++) {
    int v = bpm[i];
    if (v <= 0) continue;
    if (!st.count || v < st.min) st.min = v;
    if (!st.count || v > st.max) st.max = v;
    sum += v;
    st.count++;
  }
  if (st.count) st.avg = (sum + st.count / 2) / st.count;
  return st;
}

int td_scale_y(int v, int lo, int hi, int y_top, int y_bottom) {
  int span = y_bottom - y_top;
  int margin = span / 10;
  if (hi <= lo) return (y_top + y_bottom) / 2;
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return y_bottom - margin - ((v - lo) * (span - 2 * margin)) / (hi - lo);
}

/* ---------- Health, power, clock, link ---------- */

void td_format_steps(char *buf, size_t n, TdReading steps) {
  if (!steps.valid || steps.value < 0) {
    snprintf(buf, n, "--");
  } else if (steps.value >= 1000) {
    snprintf(buf, n, "%d,%03d", (int)(steps.value / 1000), (int)(steps.value % 1000));
  } else {
    snprintf(buf, n, "%d", (int)steps.value);
  }
}

void td_format_hr(char *buf, size_t n, TdReading hr) {
  if (!hr.valid || hr.value <= 0) {
    snprintf(buf, n, "--");
  } else {
    snprintf(buf, n, "%d", (int)hr.value);
  }
}

int td_steps_pct(TdReading steps, int goal) {
  if (!steps.valid || steps.value <= 0 || goal <= 0) return 0;
  if (steps.value >= goal) return 100;
  return (int)((steps.value * 100) / goal);
}

void td_format_weather(char *buf, size_t n, TdWeatherState st, int temp_c, int unit) {
  if (st == TD_WX_NONE) {
    snprintf(buf, n, "--°");
  } else if (unit == TD_TEMP_F) {
    int f = ((temp_c * 9) + (temp_c >= 0 ? 2 : -2)) / 5 + 32;
    snprintf(buf, n, "%d°F", f);
  } else {
    snprintf(buf, n, "%d°C", temp_c);
  }
}

TdBattState td_battery_state(int pct, bool charging, bool plugged) {
  if (charging) return TD_BATT_CHARGING;
  if (plugged) return TD_BATT_PLUGGED;
  if (pct <= TD_BATT_CRITICAL_PCT) return TD_BATT_CRITICAL;
  if (pct <= TD_BATT_LOW_PCT) return TD_BATT_LOW;
  return TD_BATT_NORMAL;
}

void td_format_power(char *buf, size_t n, int pct, TdBattState st) {
  const char *tag = "PWR";
  switch (st) {
    case TD_BATT_CHARGING: tag = "CHG"; break;
    case TD_BATT_PLUGGED: tag = "EXT"; break;
    case TD_BATT_LOW:
    case TD_BATT_CRITICAL: tag = "LOW"; break;
    default: break;
  }
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  snprintf(buf, n, "%s %d%%", tag, pct);
}

void td_format_time(char *time_buf, size_t n, char meridiem[3], int hour, int min, bool use_24h) {
  if (use_24h) {
    snprintf(time_buf, n, "%02d:%02d", hour, min);
    meridiem[0] = '\0';
    return;
  }
  int h12 = hour % 12;
  if (h12 == 0) h12 = 12;
  snprintf(time_buf, n, "%d:%02d", h12, min);
  meridiem[0] = hour < 12 ? 'A' : 'P';
  meridiem[1] = 'M';
  meridiem[2] = '\0';
}

bool td_link_update(TdLink *l, bool connected, int64_t now) {
  bool meaningful = false;
  if (connected && !l->connected) {
    meaningful = l->lost_at > 0 && (now - l->lost_at) >= TD_LINK_MEANINGFUL_OUTAGE_S;
    l->lost_at = 0;
  } else if (!connected && l->connected) {
    l->lost_at = now;
  }
  l->connected = connected;
  return meaningful;
}

/* ---------- MAGI checks ---------- */

TdVote td_vote_power(int pct, bool charging) {
  return (charging || pct > TD_BATT_LOW_PCT) ? TD_VOTE_PASS : TD_VOTE_LOW;
}

TdVote td_vote_link(bool connected) {
  return connected ? TD_VOTE_PASS : TD_VOTE_LOST;
}

TdVote td_vote_data(TdWeatherState st) {
  switch (st) {
    case TD_WX_FRESH: return TD_VOTE_PASS;
    case TD_WX_STALE: return TD_VOTE_OLD;
    default: return TD_VOTE_NONE;
  }
}

const char *td_vote_label(TdVote v) {
  switch (v) {
    case TD_VOTE_PASS: return "PASS";
    case TD_VOTE_LOW: return "LOW";
    case TD_VOTE_LOST: return "LOST";
    case TD_VOTE_OLD: return "OLD";
    case TD_VOTE_NONE: return "NONE";
    default: return "";
  }
}

/* ---------- Transition state machine ---------- */

/* BOOT: the HUD builds in stages (drawn by the watchface); the triad occupies the
 * logo area and each check resolves just after its module is up: PWR after the
 * chevrons, LINK after the BIO/SYNC rows, DATA after the weather column. */
static const TdMagiTimeline s_boot_timeline = {
  .open_start = 100, .open_end = 300,
  .vote_ms = {800, 950, 1150},
  .close_start = 1450, .close_end = 1600,
  .duration = 1800,
};
/* MAGI: the rest of the face stays as is; only the logo area changes. */
static const TdMagiTimeline s_magi_timeline = {
  .open_start = 100, .open_end = 250,
  .vote_ms = {450, 550, 650},
  .close_start = 950, .close_end = 1100,
  .duration = 1250,
};

const TdMagiTimeline *td_magi_timeline(TdAnimKind kind) {
  if (kind == TD_ANIM_BOOT) return &s_boot_timeline;
  if (kind == TD_ANIM_MAGI) return &s_magi_timeline;
  return NULL;
}

int td_anim_max_steps(TdAnimKind kind) {
  const TdMagiTimeline *tl = td_magi_timeline(kind);
  /* Hard bound even if every clock delta were tiny: twice the nominal frame count. */
  return tl ? (2 * tl->duration) / TD_FRAME_MS + 4 : 0;
}

void td_anim_init(TdAnim *a, int64_t persisted_last_full_s) {
  memset(a, 0, sizeof(*a));
  a->last_full_s = persisted_last_full_s;
  a->last_any_s = persisted_last_full_s;
}

static bool within(int64_t last, int64_t now, int64_t window) {
  /* A timestamp in the future means the clock moved back: treat as expired. */
  return last > 0 && last <= now + 60 && (now - last) < window;
}

TdDecision td_anim_trigger(TdAnim *a, TdTrigger trig, const TdAnimEnv *env, int64_t now,
                           bool *persist_full) {
  if (persist_full) *persist_full = false;
  if (env->motion == TD_MOTION_OFF) return TD_DECIDE_SKIP_OFF;
  if (a->kind != TD_ANIM_NONE) return TD_DECIDE_SKIP_BUSY;
  if (!env->charging && env->battery_pct <= TD_BATT_CRITICAL_PCT) return TD_DECIDE_SKIP_BATTERY;

  TdAnimKind kind;
  if (trig == TD_TRIG_LAUNCH) {
    if (within(a->last_full_s, now, TD_FULL_COOLDOWN_S)) return TD_DECIDE_SKIP_COOLDOWN;
    bool low = !env->charging && env->battery_pct <= TD_BATT_LOW_PCT;
    kind = (env->motion == TD_MOTION_FULL && !env->obstructed && !low) ? TD_ANIM_BOOT : TD_ANIM_MAGI;
    a->last_full_s = now;
    if (persist_full) *persist_full = true;
  } else {
    if (within(a->last_any_s, now, TD_RECOVERY_COOLDOWN_S)) return TD_DECIDE_SKIP_COOLDOWN;
    kind = TD_ANIM_MAGI;
  }
  a->kind = kind;
  a->t_ms = 0;
  a->steps = 0;
  for (int i = 0; i < TD_CHECK_COUNT; i++) a->votes[i] = TD_VOTE_PENDING;
  a->last_any_s = now;
  return TD_DECIDE_START;
}

bool td_anim_step(TdAnim *a, int dt_ms) {
  if (a->kind == TD_ANIM_NONE) return false;
  if (dt_ms < 0 || dt_ms > TD_MAX_DT_MS) dt_ms = TD_FRAME_MS;
  a->t_ms += dt_ms;
  a->steps++;
  if (a->t_ms >= td_magi_timeline(a->kind)->duration || a->steps >= td_anim_max_steps(a->kind)) {
    a->kind = TD_ANIM_NONE;
    return false;
  }
  return true;
}

void td_anim_cancel(TdAnim *a) {
  a->kind = TD_ANIM_NONE;
}

bool td_anim_should_abort(const TdAnim *a, const TdAnimEnv *env) {
  if (a->kind == TD_ANIM_NONE) return false;
  if (env->motion == TD_MOTION_OFF) return true;
  if (!env->charging && env->battery_pct <= TD_BATT_CRITICAL_PCT) return true;
  return env->obstructed;
}

uint32_t td_anim_due_votes(const TdAnim *a) {
  const TdMagiTimeline *tl = td_magi_timeline(a->kind);
  uint32_t due = 0;
  if (!tl) return 0;
  for (int i = 0; i < TD_CHECK_COUNT; i++) {
    if (a->t_ms >= tl->vote_ms[i] && a->votes[i] == TD_VOTE_PENDING) due |= 1u << i;
  }
  return due;
}

int td_anim_pass_count(const TdAnim *a) {
  int n = 0;
  for (int i = 0; i < TD_CHECK_COUNT; i++) n += a->votes[i] == TD_VOTE_PASS;
  return n;
}

const char *td_decision_name(TdDecision d) {
  switch (d) {
    case TD_DECIDE_START: return "start";
    case TD_DECIDE_SKIP_OFF: return "off";
    case TD_DECIDE_SKIP_BUSY: return "busy";
    case TD_DECIDE_SKIP_COOLDOWN: return "cooldown";
    case TD_DECIDE_SKIP_BATTERY: return "battery";
  }
  return "?";
}

/* ---------- Shake screen ---------- */

int td_fx_duration(int fx) {
  return fx == TD_FX_EMERGENCY_HEX ? TD_FX_HEX_MS : TD_FX_SYNC_MS;
}

int td_fx_max_steps(int fx) {
  return (2 * td_fx_duration(fx)) / TD_FRAME_MS + 4;
}

bool td_peek_open(const TdPeek *p) {
  return p->phase != TD_PEEK_IDLE;
}

void td_peek_close(TdPeek *p) {
  p->phase = TD_PEEK_IDLE;
  p->t_ms = 0;
  p->steps = 0;
}

TdPeekEvent td_peek_tap(TdPeek *p, const TdPeekEnv *env, int64_t now_ms) {
  if (td_peek_open(p)) {
    int64_t since = now_ms - p->opened_ms;
    /* A clock that moved backwards cannot hold the screen open: treat as elapsed. */
    if (since >= 0 && since < TD_PEEK_DEBOUNCE_MS) return TD_PEEK_NOOP;
    td_peek_close(p);
    return TD_PEEK_CLOSE;
  }
  if (env->shake != TD_SHAKE_WEATHER && env->shake != TD_SHAKE_HEART) return TD_PEEK_NOOP;
  p->screen = env->shake;
  p->fx = env->fx == TD_FX_EMERGENCY_HEX ? TD_FX_EMERGENCY_HEX : TD_FX_SYNC_GRAPH;
  p->t_ms = 0;
  p->steps = 0;
  p->opened_ms = now_ms;
  bool low = !env->charging && env->battery_pct <= TD_BATT_LOW_PCT;
  if (env->motion == TD_MOTION_OFF || low) {
    p->phase = TD_PEEK_HOLD;
    return TD_PEEK_OPEN_CUT;
  }
  p->phase = TD_PEEK_IN;
  return TD_PEEK_OPEN_FX;
}

bool td_peek_step(TdPeek *p, int dt_ms) {
  if (p->phase != TD_PEEK_IN) return false;
  if (dt_ms < 0 || dt_ms > TD_MAX_DT_MS) dt_ms = TD_FRAME_MS;
  p->t_ms += dt_ms;
  p->steps++;
  if (p->t_ms >= td_fx_duration(p->fx) || p->steps >= td_fx_max_steps(p->fx)) {
    p->phase = TD_PEEK_HOLD;
    return false;
  }
  return true;
}
