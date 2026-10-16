#pragma once
/* Terminal Dogma: pure decision logic shared by the watchface and the host tests.
 * No pebble.h here. Times are UTC epoch seconds (int64_t), durations in ms. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------- Settings ---------- */
enum { TD_CLOCK_SYSTEM, TD_CLOCK_24H, TD_CLOCK_12H };
enum { TD_LAYOUT_STEPS_WEATHER, TD_LAYOUT_BATTERY_WEATHER, TD_LAYOUT_STEPS_BATTERY };
enum { TD_ACCENT_RED, TD_ACCENT_AMBER, TD_ACCENT_GREEN, TD_ACCENT_CYAN };
enum { TD_TEMP_C, TD_TEMP_F };
enum { TD_SYNC_FRESHNESS, TD_SYNC_STEPS, TD_SYNC_BATTERY, TD_SYNC_AESTHETIC };
enum { TD_MOTION_OFF, TD_MOTION_SUBTLE, TD_MOTION_FULL };
enum { TD_SHAKE_OFF, TD_SHAKE_WEATHER, TD_SHAKE_HEART };
enum { TD_FX_SYNC_GRAPH, TD_FX_EMERGENCY_HEX };

typedef enum {
  TD_SET_CLOCK, TD_SET_LAYOUT, TD_SET_ACCENT, TD_SET_TEMP_UNIT, TD_SET_SYNC, TD_SET_MOTION,
  TD_SET_SHAKE, TD_SET_SHAKE_FX,
  TD_SETTING_COUNT
} TdSettingId;

/* Schema 1: releases up to 1.0.16 (first five settings). Schema 2: adds TD_SET_MOTION.
 * Schema 3: adds TD_SET_SHAKE and TD_SET_SHAKE_FX. */
#define TD_SETTINGS_SCHEMA 3

typedef struct { int v[TD_SETTING_COUNT]; } TdSettings;

void td_settings_defaults(TdSettings *s);
int td_setting_default(int id);
int td_setting_clamp(int id, int value);
/* Loads stored values (stored[i] valid when bit i of present_mask is set), clamps them,
 * and applies explicit migrations from stored_schema (0 = nothing stored yet).
 * Returns the mask of settings that must be written now to pin migrated defaults. */
uint32_t td_settings_load(TdSettings *s, const int *stored, uint32_t present_mask, int stored_schema);
/* Mask of settings whose value differs between the two structs (what to persist). */
uint32_t td_settings_diff(const TdSettings *a, const TdSettings *b);

/* ---------- Weather ---------- */
#define TD_WEATHER_REFRESH_S (30 * 60)   /* ask the phone for new data after this age */
#define TD_WEATHER_STALE_S (60 * 60)     /* show as STALE (grey) from this age */
#define TD_WEATHER_EXPIRE_S (6 * 60 * 60) /* show as unavailable (--) from this age */
#define TD_WEATHER_SKEW_S (5 * 60)       /* tolerated phone/watch clock skew */
#define TD_SYNC_DRAIN_S (30 * 60)        /* SYNC freshness rail drains over this age */

typedef enum { TD_WX_NONE, TD_WX_FRESH, TD_WX_STALE } TdWeatherState;

TdWeatherState td_weather_state(bool have, int64_t ts, int64_t now);
int td_sync_freshness_pct(bool have, int64_t ts, int64_t now);
/* Observation time to store for a received temperature. A phone-supplied time is
 * clamped so it is never in the future and never older than the expiry age. */
int64_t td_weather_accept_ts(bool has_msg_ts, int64_t msg_ts, int64_t now);
/* Edge trigger for the MAGI response: only missing/stale -> fresh counts, never fresh -> fresh. */
bool td_weather_is_recovery(TdWeatherState before, TdWeatherState after);

typedef struct {
  int64_t last_attempt; /* when the last REQUEST_WEATHER went out */
  int attempts;         /* consecutive requests without a weather reply */
} TdWeatherSched;

int td_weather_backoff_s(int attempts);
bool td_weather_should_request(const TdWeatherSched *s, bool connected, bool have, int64_t ts,
                               int64_t now);
void td_weather_note_attempt(TdWeatherSched *s, int64_t now);
void td_weather_note_success(TdWeatherSched *s);
void td_weather_note_reconnect(TdWeatherSched *s);

/* ---------- Hourly forecast ---------- */
#define TD_FC_SLOTS 12 /* hours stored, starting at the hour of the fetch */
#define TD_FC_SHOWN 6  /* hours shown on the shake screen */
#define TD_FC_RAIN_PCT 50 /* first hour at or above this chance is called out */

typedef struct {
  int32_t start;               /* UTC start of slot 0; 0 = no forecast */
  int32_t ts;                  /* when it was fetched (same freshness rules as weather) */
  uint8_t count;               /* valid slots */
  int8_t temp_c[TD_FC_SLOTS];
  uint8_t rain_pct[TD_FC_SLOTS];
  uint8_t code[TD_FC_SLOTS];   /* WMO weather code */
} TdForecast;

/* Index of the slot containing `now`, or -1 when the forecast is missing, expired
 * or entirely in the past. Hours already gone are never shown. */
int td_forecast_first(const TdForecast *f, int64_t now);
/* Slots shown from `first` (at most TD_FC_SHOWN). */
int td_forecast_shown(const TdForecast *f, int first);
/* Offset (0-based, from `first`) of the first shown hour with rain >= TD_FC_RAIN_PCT, or -1. */
int td_forecast_rain_at(const TdForecast *f, int first);
/* Short label for a WMO weather code: CLEAR, CLOUD, FOG, RAIN, SNOW, STORM or "--". */
const char *td_wmo_label(int code);
/* Temperature in the display unit (no suffix). */
int td_temp_in_unit(int temp_c, int unit);

/* ---------- Heart-rate history ---------- */
#define TD_HR_BUCKETS 18      /* 10-minute averages over the last 3 hours */
#define TD_HR_BUCKET_S (10 * 60)

typedef struct { int count, min, max, avg; } TdHrStats;
/* Statistics over buckets with a reading (value > 0). count is 0 when there are none. */
TdHrStats td_hr_stats(const int16_t *bpm, int n);
/* Maps v in [lo, hi] onto [y_bottom, y_top] (screen y grows downwards), with a 10% margin. */
int td_scale_y(int v, int lo, int hi, int y_top, int y_bottom);

/* ---------- Health, power, clock, link ---------- */
typedef struct { bool valid; int32_t value; } TdReading;

void td_format_steps(char *buf, size_t n, TdReading steps);
void td_format_hr(char *buf, size_t n, TdReading hr);
int td_steps_pct(TdReading steps, int goal);
void td_format_weather(char *buf, size_t n, TdWeatherState st, int temp_c, int unit);

#define TD_BATT_LOW_PCT 20
#define TD_BATT_CRITICAL_PCT 10
typedef enum { TD_BATT_NORMAL, TD_BATT_LOW, TD_BATT_CRITICAL, TD_BATT_CHARGING, TD_BATT_PLUGGED } TdBattState;

TdBattState td_battery_state(int pct, bool charging, bool plugged);
void td_format_power(char *buf, size_t n, int pct, TdBattState st);

/* time_buf gets "H:MM"/"HH:MM"; meridiem gets "AM"/"PM", or "" in 24 h mode. */
void td_format_time(char *time_buf, size_t n, char meridiem[3], int hour, int min, bool use_24h);

#define TD_LINK_MEANINGFUL_OUTAGE_S 60
typedef struct { bool connected; int64_t lost_at; } TdLink;
/* Returns true only on a disconnected -> connected edge after an outage of at
 * least TD_LINK_MEANINGFUL_OUTAGE_S. Brief flaps are ignored. */
bool td_link_update(TdLink *l, bool connected, int64_t now);

/* ---------- MAGI checks ---------- */
typedef enum { TD_VOTE_PENDING, TD_VOTE_PASS, TD_VOTE_LOW, TD_VOTE_LOST, TD_VOTE_OLD, TD_VOTE_NONE } TdVote;
enum { TD_CHECK_PWR, TD_CHECK_LINK, TD_CHECK_DATA, TD_CHECK_COUNT };

TdVote td_vote_power(int pct, bool charging);
TdVote td_vote_link(bool connected);
TdVote td_vote_data(TdWeatherState st);
const char *td_vote_label(TdVote v);

/* ---------- Transition state machine ---------- */
#define TD_FRAME_MS 66                /* nominal step; ~15 fps */
#define TD_FULL_COOLDOWN_S (30 * 60)  /* launch sequence at most once per 30 min (persisted) */
#define TD_RECOVERY_COOLDOWN_S (5 * 60)
#define TD_MAX_DT_MS 250              /* larger or negative clock deltas count as one frame */

typedef enum { TD_ANIM_NONE, TD_ANIM_BOOT, TD_ANIM_MAGI } TdAnimKind;
typedef enum { TD_TRIG_LAUNCH, TD_TRIG_LINK_RESTORED, TD_TRIG_DATA_RECOVERED } TdTrigger;
typedef enum {
  TD_DECIDE_START, TD_DECIDE_SKIP_OFF, TD_DECIDE_SKIP_BUSY, TD_DECIDE_SKIP_COOLDOWN,
  TD_DECIDE_SKIP_BATTERY
} TdDecision;

typedef struct {
  int open_start, open_end;      /* triad opens */
  int vote_ms[TD_CHECK_COUNT];   /* when each check resolves */
  int close_start, close_end;    /* triad closes */
  int duration;                  /* logo restored, sequence over */
} TdMagiTimeline;

typedef struct {
  TdAnimKind kind;
  int t_ms;
  int steps;
  TdVote votes[TD_CHECK_COUNT];
  int64_t last_full_s; /* persisted: when a launch sequence last started */
  int64_t last_any_s;  /* when any sequence last started */
} TdAnim;

typedef struct {
  int motion;
  int battery_pct;
  bool charging;
  bool obstructed;
} TdAnimEnv;

void td_anim_init(TdAnim *a, int64_t persisted_last_full_s);
/* Decides whether a trigger starts a sequence and which one. On TD_DECIDE_START with
 * a launch trigger, *persist_full is set and the caller stores a->last_full_s. */
TdDecision td_anim_trigger(TdAnim *a, TdTrigger trig, const TdAnimEnv *env, int64_t now,
                           bool *persist_full);
/* Advances by dt_ms (guarded). Returns true while the caller should schedule another step. */
bool td_anim_step(TdAnim *a, int dt_ms);
void td_anim_cancel(TdAnim *a);
/* True when a running sequence must stop now because the environment changed:
 * animations turned Off, battery fell to critical while unplugged, or Quick View
 * appeared during the launch sequence (the MAGI-only response stays in the logo area). */
bool td_anim_should_abort(const TdAnim *a, const TdAnimEnv *env);
const TdMagiTimeline *td_magi_timeline(TdAnimKind kind);
int td_anim_max_steps(TdAnimKind kind);
/* Checks that resolved by now and are still pending; caller fills votes[] for these. */
uint32_t td_anim_due_votes(const TdAnim *a);
int td_anim_pass_count(const TdAnim *a);
const char *td_decision_name(TdDecision d);

/* ---------- Shake screen ----------
 * A wrist shake opens the chosen screen (weather or heart rate) through one of two
 * transitions, holds it, then returns to the face. A second shake dismisses it. */
#define TD_PEEK_HOLD_MS 10000   /* screen returns to the face after this */
#define TD_PEEK_DEBOUNCE_MS 1500 /* one shake fires several taps; ignore them this long */
#define TD_FX_SYNC_MS 1650
#define TD_FX_HEX_MS 1450

typedef enum { TD_PEEK_IDLE, TD_PEEK_IN, TD_PEEK_HOLD } TdPeekPhase;
typedef enum { TD_PEEK_NOOP, TD_PEEK_OPEN_FX, TD_PEEK_OPEN_CUT, TD_PEEK_CLOSE } TdPeekEvent;

typedef struct {
  TdPeekPhase phase;
  int screen;          /* TD_SHAKE_WEATHER or TD_SHAKE_HEART while open */
  int fx;              /* TD_FX_* while in TD_PEEK_IN */
  int t_ms;            /* transition clock */
  int steps;
  int64_t opened_ms;   /* wall clock at opening, for the debounce */
} TdPeek;

typedef struct {
  int shake;           /* TD_SET_SHAKE */
  int fx;              /* TD_SET_SHAKE_FX */
  int motion;          /* TD_SET_MOTION: Off means a hard cut */
  int battery_pct;
  bool charging;
} TdPeekEnv;

/* A tap arrived at wall-clock now_ms. Opens (with or without the transition) or closes. */
TdPeekEvent td_peek_tap(TdPeek *p, const TdPeekEnv *env, int64_t now_ms);
/* Advances the transition by dt_ms (guarded like td_anim_step). Returns true while
 * another frame is needed; on false the screen is in TD_PEEK_HOLD. */
bool td_peek_step(TdPeek *p, int dt_ms);
void td_peek_close(TdPeek *p);
int td_fx_duration(int fx);
int td_fx_max_steps(int fx);
bool td_peek_open(const TdPeek *p);
