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

typedef enum {
  TD_SET_CLOCK, TD_SET_LAYOUT, TD_SET_ACCENT, TD_SET_TEMP_UNIT, TD_SET_SYNC, TD_SET_MOTION,
  TD_SETTING_COUNT
} TdSettingId;

/* Schema 1: releases up to 1.0.16 (first five settings). Schema 2: adds TD_SET_MOTION. */
#define TD_SETTINGS_SCHEMA 2

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
