#include <pebble.h>

#include "td_logic.h"

#define BOTTOM_Y 195
#define STEPS_GOAL 10000

/* Persist keys. 1001-1006 are the settings used since 1.0.x; 1002 is historical. */
#define PERSIST_SCHEMA 1010
#define PERSIST_WX_TEMP 1011
#define PERSIST_WX_TIME 1012
#define PERSIST_LAST_FULL 1013
#define PERSIST_FORECAST 1014
static const uint32_t s_setting_keys[TD_SETTING_COUNT] = {1001, 1003, 1004, 1005, 1006, 1007, 1008, 1009};

extern uint32_t MESSAGE_KEY_CLOCK_FORMAT;
extern uint32_t MESSAGE_KEY_BOTTOM_LAYOUT;
extern uint32_t MESSAGE_KEY_ACCENT_COLOR;
extern uint32_t MESSAGE_KEY_TEMP_UNIT;
extern uint32_t MESSAGE_KEY_SYNC_SOURCE;
extern uint32_t MESSAGE_KEY_TEMPERATURE;
extern uint32_t MESSAGE_KEY_REQUEST_WEATHER;
extern uint32_t MESSAGE_KEY_ANIMATION;
extern uint32_t MESSAGE_KEY_WEATHER_TIME;
extern uint32_t MESSAGE_KEY_REQUEST_SETTINGS;
extern uint32_t MESSAGE_KEY_FORECAST_START;
extern uint32_t MESSAGE_KEY_FORECAST_TEMPS;
extern uint32_t MESSAGE_KEY_FORECAST_RAIN;
extern uint32_t MESSAGE_KEY_FORECAST_CODES;
extern uint32_t MESSAGE_KEY_SHAKE_SCREEN;
extern uint32_t MESSAGE_KEY_SHAKE_FX;

/* Settings message keys, in TdSettingId order. */
#define SETTING_MESSAGE_KEYS { \
  MESSAGE_KEY_CLOCK_FORMAT, MESSAGE_KEY_BOTTOM_LAYOUT, MESSAGE_KEY_ACCENT_COLOR, \
  MESSAGE_KEY_TEMP_UNIT, MESSAGE_KEY_SYNC_SOURCE, MESSAGE_KEY_ANIMATION, \
  MESSAGE_KEY_SHAKE_SCREEN, MESSAGE_KEY_SHAKE_FX, \
}

static Window *s_main_window;
static Layer *s_window_layer;
static Layer *s_canvas_layer;
static GBitmap *s_logo_bitmap;

static GFont s_font_time;
static GFont s_font_chip;
static GFont s_font_chip_small;
static GFont s_font_big;
static GFont s_font_badge;

static TdSettings s_settings;

static char s_time_text[8];
static char s_meridiem[3];
static char s_date_text[16];
static char s_steps_text[16];
static char s_hr_text[8];
static char s_battery_text[8];
static char s_power_text[12];

static int s_battery_pct;
static bool s_battery_charging;
static bool s_battery_plugged;
static TdReading s_steps;
static TdReading s_hr;

static bool s_wx_have;
static int s_wx_temp;
static int64_t s_wx_ts;
static TdWeatherSched s_wx_sched;
static TdForecast s_fc;

static TdLink s_link;
static bool s_is_obstructed;
static bool s_quiet_time;

/* Watch -> phone messages, one in flight at a time. */
#define OUT_WEATHER 1
#define OUT_SETTINGS 2
#define SETTINGS_REPLY_TRIES 3
static uint8_t s_out_pending;
static uint8_t s_out_inflight;
static uint8_t s_settings_reply_tries;

static TdAnim s_anim;
static AppTimer *s_anim_timer;
static int64_t s_anim_last_ms;

static TdPeek s_peek;
static AppTimer *s_peek_timer;      /* transition frames */
static AppTimer *s_peek_hold_timer; /* return to the face */
static int64_t s_peek_last_ms;
static bool s_tap_subscribed;
/* Snapshot taken when the shake screen opens; drawing never queries Health. */
static int16_t s_hr_hist[TD_HR_BUCKETS];
static TdHrStats s_hr_stats;
static int s_fc_first;

static GColor color_red(void) { return GColorFromHEX(0xFF2424); }
static GColor color_amber(void) { return GColorFromHEX(0xFFAA00); }
static GColor color_green(void) { return GColorFromHEX(0x34E07A); }
static GColor color_cyan(void) { return GColorFromHEX(0x2FE6E6); }
static GColor color_yellow(void) { return GColorFromHEX(0xFFD400); }
static GColor color_pink(void) { return GColorFromHEX(0xFF8A9B); }
static GColor color_grey(void) { return GColorFromHEX(0x8C8C8C); }

static GColor accent_color(void) {
  switch (s_settings.v[TD_SET_ACCENT]) {
    case TD_ACCENT_AMBER: return color_amber();
    case TD_ACCENT_GREEN: return color_green();
    case TD_ACCENT_CYAN: return color_cyan();
    case TD_ACCENT_RED:
    default: return color_red();
  }
}

static int64_t now_ms(void) {
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  return (int64_t)s * 1000 + ms;
}

static TdWeatherState weather_state(void) {
  return td_weather_state(s_wx_have, s_wx_ts, time(NULL));
}

static TdBattState battery_state(void) {
  return td_battery_state(s_battery_pct, s_battery_charging, s_battery_plugged);
}

static GColor battery_color(TdBattState st) {
  switch (st) {
    case TD_BATT_CHARGING:
    case TD_BATT_PLUGGED: return color_green();
    case TD_BATT_LOW: return color_yellow();
    case TD_BATT_CRITICAL: return color_red();
    default: return accent_color();
  }
}

/* ---------- Launch sequence staging (TD_ANIM_BOOT) ---------- */
#define B_RULE_END 250
#define B_LABEL_STEP 20
#define B_DIVIDER_MS 150
#define B_DIVIDER_END 400
#define B_TAG_MAGI_MS 250
#define B_TAG_PWR_MS 300
#define B_TAG_STEP 50
#define B_SCALE_MS 400
#define B_SCALE_END 700
#define B_CHEV_MS 450
#define B_CHEV_STEP 50
#define B_LOWER_MS 350
#define B_LOWER_END 600
#define B_BIO_MS 600
#define B_BIO_VALUE_MS 750
#define B_SYNC_MS 650
#define B_SYNC_PAD_STEP 15
#define B_SYNC_FILL_MS 800
#define B_SYNC_FILL_STEP 20
#define B_PANEL_DIV_MS 700
#define B_COL_MS 800
#define B_COL_STEP 100
#define B_COL_VALUE_DELAY 80

static bool booting(void) { return s_anim.kind == TD_ANIM_BOOT; }

/* True once the boot clock passes `at`; always true outside the launch sequence. */
static bool shown(int at) { return !booting() || s_anim.t_ms >= at; }

/* 0..256 progress through [a, b); 256 outside the launch sequence. */
static int ramp(int a, int b) {
  if (!booting() || s_anim.t_ms >= b) return 256;
  if (s_anim.t_ms <= a) return 0;
  return ((s_anim.t_ms - a) * 256) / (b - a);
}

/* First characters of src, one more every `step` ms from `at`. */
static const char *typed(const char *src, char *buf, int len, int at, int step) {
  if (!booting()) return src;
  int n = s_anim.t_ms < at ? 0 : (s_anim.t_ms - at) / step + 1;
  int i = 0;
  for (; src[i] && i < n && i < len - 1; i++) buf[i] = src[i];
  buf[i] = '\0';
  return buf;
}

/* ---------- Drawing primitives ---------- */

static void draw_text_box(GContext *ctx, const char *text, GRect box, GFont font,
                          GColor color, GTextAlignment align) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

static uint8_t glyph_row(char c, int row) {
  static const uint8_t blank[5] = {0, 0, 0, 0, 0};
  static const uint8_t A[5] = {2, 5, 7, 5, 5};
  static const uint8_t B[5] = {6, 5, 6, 5, 6};
  static const uint8_t C[5] = {7, 4, 4, 4, 7};
  static const uint8_t D[5] = {6, 5, 5, 5, 6};
  static const uint8_t E[5] = {7, 4, 6, 4, 7};
  static const uint8_t F[5] = {7, 4, 6, 4, 4};
  static const uint8_t G[5] = {7, 4, 5, 5, 7};
  static const uint8_t H[5] = {5, 5, 7, 5, 5};
  static const uint8_t I[5] = {7, 2, 2, 2, 7};
  static const uint8_t K[5] = {5, 5, 6, 5, 5};
  static const uint8_t L[5] = {4, 4, 4, 4, 7};
  static const uint8_t M[5] = {5, 7, 7, 5, 5};
  static const uint8_t N[5] = {6, 5, 5, 5, 5};
  static const uint8_t O[5] = {7, 5, 5, 5, 7};
  static const uint8_t P[5] = {6, 5, 6, 4, 4};
  static const uint8_t Q[5] = {7, 5, 5, 7, 1};
  static const uint8_t R[5] = {6, 5, 6, 5, 5};
  static const uint8_t S[5] = {7, 4, 7, 1, 7};
  static const uint8_t T[5] = {7, 2, 2, 2, 2};
  static const uint8_t U[5] = {5, 5, 5, 5, 7};
  static const uint8_t V[5] = {5, 5, 5, 5, 2};
  static const uint8_t W[5] = {5, 5, 7, 7, 5};
  static const uint8_t Y[5] = {5, 5, 2, 2, 2};
  static const uint8_t D0[5] = {7, 5, 5, 5, 7};
  static const uint8_t D1[5] = {2, 6, 2, 2, 7};
  static const uint8_t D2[5] = {7, 1, 7, 4, 7};
  static const uint8_t D3[5] = {7, 1, 7, 1, 7};
  static const uint8_t D4[5] = {5, 5, 7, 1, 1};
  static const uint8_t D5[5] = {7, 4, 7, 1, 7};
  static const uint8_t D6[5] = {7, 4, 7, 5, 7};
  static const uint8_t D7[5] = {7, 1, 1, 1, 1};
  static const uint8_t D8[5] = {7, 5, 7, 5, 7};
  static const uint8_t D9[5] = {7, 5, 7, 1, 7};
  static const uint8_t DASH[5] = {0, 0, 7, 0, 0};
  static const uint8_t SLASH[5] = {1, 1, 2, 4, 4};

  const uint8_t *g = blank;
  switch (c) {
    case 'A': g = A; break; case 'B': g = B; break; case 'C': g = C; break;
    case 'D': g = D; break; case 'E': g = E; break; case 'F': g = F; break;
    case 'G': g = G; break; case 'H': g = H; break;
    case 'I': g = I; break; case 'K': g = K; break; case 'L': g = L; break;
    case 'M': g = M; break; case 'N': g = N; break; case 'O': g = O; break;
    case 'P': g = P; break; case 'Q': g = Q; break; case 'R': g = R; break;
    case 'S': g = S; break; case 'T': g = T; break; case 'U': g = U; break;
    case 'V': g = V; break; case 'W': g = W; break;
    case 'Y': g = Y; break;
    case '0': g = D0; break; case '1': g = D1; break; case '2': g = D2; break;
    case '3': g = D3; break; case '4': g = D4; break; case '5': g = D5; break;
    case '6': g = D6; break; case '7': g = D7; break; case '8': g = D8; break;
    case '9': g = D9; break; case '-': g = DASH; break; case '/': g = SLASH; break;
    default: break;
  }
  return g[row];
}

static int tiny_text_width(const char *text, int scale) {
  int len = text ? (int)strlen(text) : 0;
  return len ? ((len * 4) - 1) * scale : 0;
}

static void draw_tiny_text(GContext *ctx, const char *text, int x, int y, int scale, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  int ps = scale > 0 ? scale : 1;
  for (int i = 0; text && text[i]; i++) {
    char c = text[i];
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int row = 0; row < 5; row++) {
      uint8_t bits = glyph_row(c, row);
      for (int col = 0; col < 3; col++) {
        if (bits & (1 << (2 - col))) {
          graphics_fill_rect(ctx, GRect(x + (i * 4 + col) * ps, y + row * ps, ps, ps), 0, GCornerNone);
        }
      }
    }
  }
}

static void draw_tiny_centered(GContext *ctx, const char *text, GRect r, int scale, GColor color) {
  int w = tiny_text_width(text, scale);
  int h = 5 * scale;
  draw_tiny_text(ctx, text, r.origin.x + (r.size.w - w) / 2, r.origin.y + (r.size.h - h) / 2, scale, color);
}

static void fill_polygon_at(GContext *ctx, GPoint origin, GPoint *points, int count, GColor color) {
  GPathInfo info = {.num_points = count, .points = points};
  GPath *path = gpath_create(&info);
  if (!path) return;
  gpath_move_to(path, origin);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

/* ---------- PWR chevrons ---------- */

static void draw_chevron(GContext *ctx, int x, int y, int w, int h, const char *label) {
  GPoint chev_points[] = {
    GPoint(0, 0),
    GPoint(w / 2, (h * 45) / 100),
    GPoint(w, 0),
    GPoint(w, (h * 55) / 100),
    GPoint(w / 2, h),
    GPoint(0, (h * 55) / 100)
  };
  fill_polygon_at(ctx, GPoint(x, y), chev_points, ARRAY_LENGTH(chev_points), accent_color());

  graphics_context_set_fill_color(ctx, color_yellow());
  graphics_fill_rect(ctx, GRect(x + 5, y + 2, 4, 4), 0, GCornerNone);

  GRect pill = GRect(x + 14, y + 1, 28, 8);
  graphics_fill_rect(ctx, pill, 2, GCornersAll);
  draw_tiny_centered(ctx, label, pill, 1, GColorBlack);
}

static void draw_l_series(GContext *ctx) {
  static const char *labels[] = {"L-01", "L-02", "L-03", "L-04", "L-05", "L-06", "L-07"};
  const int count = 7;
  const int lit_count = (s_battery_pct * count + 50) / 100;
  const int x = 118;
  const int y = 23;
  const int w = 50; /* 4 px narrower than 1.0.16 so L-01 no longer touches the "100" label */
  const int h = 10;
  const int gap = 2;

  for (int i = 0; i < count; i++) {
    bool lit = (count - i) <= lit_count && shown(B_CHEV_MS + (count - 1 - i) * B_CHEV_STEP);
    if (lit) draw_chevron(ctx, x, y + i * (h + gap), w, h, labels[i]);
  }

  const int sx = 194;
  const int sy = 23;
  const int sh = 82;
  GColor accent = accent_color();
  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, 2);
  if (shown(B_SCALE_MS)) {
    int grown = (sh * ramp(B_SCALE_MS, B_SCALE_END)) / 256;
    graphics_draw_line(ctx, GPoint(sx, sy + sh - grown), GPoint(sx, sy + sh));
  }

  for (int v = 0; v <= 100; v += 10) {
    if (!shown(B_SCALE_MS + (v * (B_SCALE_END - B_SCALE_MS)) / 100)) continue;
    int ty = sy + sh - (v * sh) / 100;
    bool major = (v % 20) == 0;
    graphics_draw_line(ctx, GPoint(sx - (major ? 9 : 5), ty), GPoint(sx, ty));
    if (major) {
      char buf[4];
      snprintf(buf, sizeof(buf), "%d", v);
      draw_tiny_text(ctx, buf, sx - 13 - tiny_text_width(buf, 1), ty - 2, 1, accent);
    }
  }
}

/* ---------- BIO monitor ---------- */

static void draw_heart_icon(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(c.x - 3, c.y - 1), 3);
  graphics_fill_circle(ctx, GPoint(c.x + 3, c.y - 1), 3);
  GPoint tri[] = {GPoint(c.x - 6, c.y + 1), GPoint(c.x + 6, c.y + 1), GPoint(c.x, c.y + 6)};
  fill_polygon_at(ctx, GPoint(0, 0), tri, 3, color);
}

static void draw_bio_mon(GContext *ctx, GRect box) {
  if (!shown(B_BIO_MS)) return;
  GColor accent = accent_color();
  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, box);

  char buf[4];
  draw_tiny_centered(ctx, typed("BIO", buf, sizeof(buf), B_BIO_MS + 50, 40),
                     GRect(box.origin.x, box.origin.y + 3, box.size.w, 7), 1, accent);
  if (!shown(B_BIO_VALUE_MS)) return;

  /* Grey when the firmware has no heart-rate reading from the last 15 minutes. */
  GColor value_color = s_hr.valid ? accent : color_grey();
  draw_heart_icon(ctx, GPoint(box.origin.x + box.size.w / 2, box.origin.y + 21), value_color);
  draw_text_box(ctx, s_hr_text, GRect(box.origin.x, box.origin.y + 28, box.size.w, 12),
                s_font_chip_small, value_color, GTextAlignmentCenter);
}

/* ---------- Bottom panel ---------- */

static void draw_hazard_corner(GContext *ctx, int x, int y, int s) {
  GColor accent = accent_color();
  for (int row = 0; row < s; row++) {
    for (int col = 0; col < s; col++) {
      graphics_context_set_fill_color(ctx, ((row + col) % 2) ? accent : GColorWhite);
      graphics_fill_rect(ctx, GRect(x + col, y + row, 1, 1), 0, GCornerNone);
    }
  }
}

static int s_col_index;

static void draw_panel_column(GContext *ctx, GRect r, const char *label, GColor label_color,
                              const char *value, GColor value_color) {
  int at = B_COL_MS + (s_col_index++) * B_COL_STEP;
  if (!shown(at)) return;
  graphics_context_set_stroke_color(ctx, accent_color());
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(r.origin.x, r.origin.y + 3), GPoint(r.origin.x + r.size.w - 1, r.origin.y + 3));

  draw_tiny_centered(ctx, label, GRect(r.origin.x, r.origin.y + 4, r.size.w, 7), 1, label_color);
  if (shown(at + B_COL_VALUE_DELAY)) {
    draw_text_box(ctx, value, GRect(r.origin.x + 1, r.origin.y + 12, r.size.w - 2, 12),
                  s_font_chip_small, value_color, GTextAlignmentCenter);
  }
}

static void draw_weather_column(GContext *ctx, GRect r) {
  char buf[16];
  TdWeatherState st = weather_state();
  td_format_weather(buf, sizeof(buf), st, s_wx_temp, s_settings.v[TD_SET_TEMP_UNIT]);
  switch (st) {
    case TD_WX_FRESH: draw_panel_column(ctx, r, "WEATHER", accent_color(), buf, color_cyan()); break;
    case TD_WX_STALE: draw_panel_column(ctx, r, "STALE", color_grey(), buf, color_grey()); break;
    default: draw_panel_column(ctx, r, "WEATHER", accent_color(), buf, color_grey()); break;
  }
}

static void draw_steps_column(GContext *ctx, GRect r) {
  draw_panel_column(ctx, r, "STEPS", accent_color(), s_steps_text, s_steps.valid ? color_yellow() : color_grey());
}

static void draw_battery_column(GContext *ctx, GRect r) {
  draw_panel_column(ctx, r, "BATT", accent_color(), s_battery_text, battery_color(battery_state()));
}

static void draw_bottom_panel(GContext *ctx) {
  const int px = 4, py = BOTTOM_Y, pw = 192, ph = 30;
  GColor accent = accent_color();

  s_col_index = 0;
  int lower = ramp(B_LOWER_MS, B_LOWER_END);
  if (lower == 0) return;

  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, 1);
  /* top border line only, no bottom border */
  int top_w = (pw * lower) / 256;
  graphics_draw_line(ctx, GPoint(px, py), GPoint(px + top_w - 1, py));
  graphics_draw_line(ctx, GPoint(px, py + 1), GPoint(px + top_w - 1, py + 1));
  if (lower < 256) return;

  draw_hazard_corner(ctx, px, py, 5);
  draw_hazard_corner(ctx, px + pw - 5, py, 5);
  if (!shown(B_PANEL_DIV_MS)) return;

  int d1x = px + 72, d2x = px + 138;
  graphics_context_set_stroke_color(ctx, accent);
  graphics_draw_line(ctx, GPoint(d1x, py + 2), GPoint(d1x, py + ph - 2));
  graphics_draw_line(ctx, GPoint(d2x, py + 2), GPoint(d2x, py + ph - 2));

  /* First column gets extra left inset: real Time2 rendering clipped DATE to ATE for one user. */
  GRect c1 = GRect(px + 6, py + 2, d1x - px - 6, ph - 4);
  GRect c2 = GRect(d1x + 2, py + 2, d2x - d1x - 2, ph - 4);
  GRect c3 = GRect(d2x + 2, py + 2, px + pw - d2x - 2, ph - 4);

  draw_panel_column(ctx, c1, "DATE", accent, s_date_text, color_pink());
  switch (s_settings.v[TD_SET_LAYOUT]) {
    case TD_LAYOUT_BATTERY_WEATHER:
      draw_battery_column(ctx, c2);
      draw_weather_column(ctx, c3);
      break;
    case TD_LAYOUT_STEPS_BATTERY:
      draw_steps_column(ctx, c2);
      draw_battery_column(ctx, c3);
      break;
    case TD_LAYOUT_STEPS_WEATHER:
    default:
      draw_steps_column(ctx, c2);
      draw_weather_column(ctx, c3);
      break;
  }
}

/* ---------- SYNC rail ---------- */

static int sync_source_percent(void) {
  switch (s_settings.v[TD_SET_SYNC]) {
    case TD_SYNC_STEPS: return td_steps_pct(s_steps, STEPS_GOAL);
    case TD_SYNC_BATTERY: return s_battery_pct;
    case TD_SYNC_AESTHETIC: return 85;
    case TD_SYNC_FRESHNESS:
    default: return td_sync_freshness_pct(s_wx_have, s_wx_ts, time(NULL));
  }
}

static void draw_sync_ratio(GContext *ctx, int y) {
  if (!shown(B_SYNC_MS)) return;
  GColor accent = accent_color();
  int sync_pct = sync_source_percent();

  char buf[5];
  draw_text_box(ctx, typed("SYNC", buf, sizeof(buf), B_SYNC_MS, 40), GRect(6, y, 38, 14),
                s_font_chip_small, accent, GTextAlignmentLeft);

  /* 25-degree slanted pads: PCB-terminal texture without becoming hazard stripes. */
  const int bar_x = 48;
  const int bar_y = y + 5;
  const int block_w = 6;
  const int block_h = 6;
  const int slant = 3; /* tan(25deg) * 6px ~= 2.8px */
  const int gap = 2;
  const int blocks = 18;
  const int lit_blocks = (sync_pct * blocks + 50) / 100;

  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, accent);

  for (int i = 0; i < blocks; i++) {
    if (!shown(B_SYNC_MS + i * B_SYNC_PAD_STEP)) continue;
    int bx = bar_x + i * (block_w + gap);
    bool lit = i < lit_blocks && shown(B_SYNC_FILL_MS + i * B_SYNC_FILL_STEP);

    GPoint top_left = GPoint(bx + slant, bar_y);
    GPoint top_right = GPoint(bx + block_w + slant, bar_y);
    GPoint bottom_right = GPoint(bx + block_w, bar_y + block_h);
    GPoint bottom_left = GPoint(bx, bar_y + block_h);

    graphics_draw_line(ctx, top_left, top_right);
    graphics_draw_line(ctx, top_right, bottom_right);
    graphics_draw_line(ctx, bottom_right, bottom_left);
    graphics_draw_line(ctx, bottom_left, top_left);

    if (lit) {
      for (int row = 1; row < block_h; row++) {
        int offset = ((block_h - row) * slant + block_h / 2) / block_h;
        graphics_draw_line(ctx,
                           GPoint(bx + 1 + offset, bar_y + row),
                           GPoint(bx + block_w - 1 + offset, bar_y + row));
      }
    }
  }
}

/* ---------- MAGI triad (logo area during a sequence) ---------- */

#define LOGO_RECT GRect(8, 12, 88, 104)
#define MAGI_CX 56
#define MAGI_CY 59
#define CLIP_BL 1
#define CLIP_BR 2
#define CLIP_TL 4
#define CLIP_TR 8

typedef struct {
  const char *name;
  const char *topic;
  int x, y, w, h;
  int clip; /* chamfered corner(s) facing the triad centre */
  int check;
} MagiUnit;

static const MagiUnit s_magi[3] = {
  {"BALTHASAR-2", "LINK", 26, 17, 60, 38, CLIP_BL | CLIP_BR, TD_CHECK_LINK},
  {"CASPER-3", "DATA", 6, 64, 48, 48, CLIP_TR, TD_CHECK_DATA},
  {"MELCHIOR-1", "PWR", 58, 64, 48, 48, CLIP_TL, TD_CHECK_PWR},
};

static void draw_logo(GContext *ctx, int keep_every) {
  if (!s_logo_bitmap) {
    draw_text_box(ctx, "NERV", GRect(8, 38, 88, 28), s_font_chip, color_red(), GTextAlignmentCenter);
    return;
  }
  GRect r = LOGO_RECT;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_logo_bitmap, r);
  if (keep_every <= 1) return;
  /* Scanline dim / warm-up: no alpha on Pebble, so black out rows. */
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  for (int y = r.origin.y; y < r.origin.y + r.size.h; y++) {
    if ((y - r.origin.y) % keep_every) graphics_draw_line(ctx, GPoint(r.origin.x, y), GPoint(r.origin.x + r.size.w - 1, y));
  }
}

static void draw_magi_unit(GContext *ctx, const MagiUnit *u, const TdMagiTimeline *tl) {
  const int c = 7;
  int x0 = u->x, y0 = u->y, x1 = u->x + u->w - 1, y1 = u->y + u->h - 1;
  GPoint pts[8];
  int n = 0;
  if (u->clip & CLIP_TL) { pts[n++] = GPoint(x0, y0 + c); pts[n++] = GPoint(x0 + c, y0); } else pts[n++] = GPoint(x0, y0);
  if (u->clip & CLIP_TR) { pts[n++] = GPoint(x1 - c, y0); pts[n++] = GPoint(x1, y0 + c); } else pts[n++] = GPoint(x1, y0);
  if (u->clip & CLIP_BR) { pts[n++] = GPoint(x1, y1 - c); pts[n++] = GPoint(x1 - c, y1); } else pts[n++] = GPoint(x1, y1);
  if (u->clip & CLIP_BL) { pts[n++] = GPoint(x0 + c, y1); pts[n++] = GPoint(x0, y1 - c); } else pts[n++] = GPoint(x0, y1);

  TdVote vote = s_anim.votes[u->check];
  bool pass = vote == TD_VOTE_PASS;
  GColor accent = accent_color();
  GColor ink = pass ? GColorBlack : accent;

  if (pass) fill_polygon_at(ctx, GPoint(0, 0), pts, n, color_yellow());
  graphics_context_set_stroke_color(ctx, pass ? color_yellow() : accent);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < n; i++) graphics_draw_line(ctx, pts[i], pts[(i + 1) % n]);

  draw_tiny_centered(ctx, u->name, GRect(u->x, u->y + 5, u->w, 7), 1, ink);
  GRect mid = GRect(u->x + 4, u->y + u->h / 2 - 6, u->w - 8, 12);
  if (vote != TD_VOTE_PENDING) {
    draw_tiny_centered(ctx, td_vote_label(vote), mid, 2, ink);
  } else {
    /* deliberation: a thin bar that fills until this unit resolves */
    int bw = mid.size.w - 8;
    int span = tl->vote_ms[u->check] - tl->open_end;
    int fill = span > 0 ? (bw * (s_anim.t_ms - tl->open_end)) / span : bw;
    if (fill < 0) fill = 0;
    if (fill > bw) fill = bw;
    graphics_context_set_stroke_color(ctx, accent);
    graphics_draw_rect(ctx, GRect(mid.origin.x + 4, mid.origin.y + 4, bw, 4));
    graphics_context_set_fill_color(ctx, accent);
    if (fill > 0) graphics_fill_rect(ctx, GRect(mid.origin.x + 4, mid.origin.y + 4, fill, 4), 0, GCornerNone);
  }
  draw_tiny_centered(ctx, u->topic, GRect(u->x, u->y + u->h - 11, u->w, 7), 1, pass ? GColorBlack : color_grey());
}

static void draw_logo_area(GContext *ctx) {
  const TdMagiTimeline *tl = td_magi_timeline(s_anim.kind);
  if (!tl) {
    draw_logo(ctx, 1);
    return;
  }
  int t = s_anim.t_ms;
  if (t < tl->open_start) {
    if (!booting()) draw_logo(ctx, 2); /* the boot starts with the logo unpowered */
    return;
  }
  if (t >= tl->close_end) {
    /* logo returns: every 4th scanline, then every 2nd */
    int half = tl->close_end + (tl->duration - tl->close_end) / 2;
    draw_logo(ctx, booting() && t < half ? 4 : 2);
    return;
  }
  int scale = 256;
  if (t < tl->open_end) {
    scale = 32 + (224 * (t - tl->open_start)) / (tl->open_end - tl->open_start);
  } else if (t >= tl->close_start) {
    scale = 256 - (224 * (t - tl->close_start)) / (tl->close_end - tl->close_start);
  }
  if (scale < 256) {
    graphics_context_set_stroke_color(ctx, accent_color());
    graphics_context_set_stroke_width(ctx, 1);
    for (int i = 0; i < 3; i++) {
      const MagiUnit *u = &s_magi[i];
      int x = MAGI_CX + ((u->x - MAGI_CX) * scale) / 256;
      int y = MAGI_CY + ((u->y - MAGI_CY) * scale) / 256;
      int w = (u->w * scale) / 256, h = (u->h * scale) / 256;
      if (w > 1 && h > 1) graphics_draw_rect(ctx, GRect(x, y, w, h));
    }
    return;
  }
  for (int i = 0; i < 3; i++) draw_magi_unit(ctx, &s_magi[i], tl);
}

/* Verdict window: all checks resolved, triad not yet closing. */
static bool magi_verdict_showing(void) {
  const TdMagiTimeline *tl = td_magi_timeline(s_anim.kind);
  if (!tl) return false;
  int last = tl->vote_ms[0];
  for (int i = 1; i < TD_CHECK_COUNT; i++) if (tl->vote_ms[i] > last) last = tl->vote_ms[i];
  return s_anim.t_ms >= last && s_anim.t_ms < tl->close_start;
}

/* ---------- Corner tags ---------- */

static void draw_status_tag(GContext *ctx) {
  char buf[12];
  if (magi_verdict_showing()) {
    snprintf(buf, sizeof(buf), "MAGI %d/3", td_anim_pass_count(&s_anim));
    draw_text_box(ctx, buf, GRect(4, 1, 80, 12), s_font_chip_small, accent_color(), GTextAlignmentLeft);
  } else if (!s_link.connected) {
    draw_text_box(ctx, typed("LINK LOST", buf, sizeof(buf), B_TAG_MAGI_MS, B_TAG_STEP),
                  GRect(4, 1, 80, 12), s_font_chip_small, color_yellow(), GTextAlignmentLeft);
  } else {
    draw_text_box(ctx, typed("MAGI", buf, sizeof(buf), B_TAG_MAGI_MS, B_TAG_STEP),
                  GRect(4, 1, 80, 12), s_font_chip_small, accent_color(), GTextAlignmentLeft);
  }
}

static void draw_power_tag(GContext *ctx) {
  if (!shown(B_TAG_PWR_MS)) return;
  char buf[12];
  const char *text = typed(s_power_text, buf, sizeof(buf), B_TAG_PWR_MS, B_TAG_STEP);
  GRect box = GRect(100, 1, 96, 12);
  TdBattState st = battery_state();
  if (st == TD_BATT_CRITICAL) {
    /* Inverted plate so a critical battery reads at a glance. */
    GSize sz = graphics_text_layout_get_content_size(text, s_font_chip_small, box,
                                                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight);
    graphics_context_set_fill_color(ctx, color_red());
    graphics_fill_rect(ctx, GRect(box.origin.x + box.size.w - sz.w - 3, 2, sz.w + 4, 13), 2, GCornersAll);
    draw_text_box(ctx, text, GRect(box.origin.x, box.origin.y, box.size.w - 1, box.size.h),
                  s_font_chip_small, GColorBlack, GTextAlignmentRight);
  } else {
    draw_text_box(ctx, text, box, s_font_chip_small, battery_color(st), GTextAlignmentRight);
  }
}

/* Crescent moon and QUIET between the corner tags while Quiet Time is on. */
static void draw_quiet_tag(GContext *ctx) {
  if (!s_quiet_time || !shown(B_TAG_PWR_MS)) return;
  char buf[8];
  GColor ink = color_cyan();
  int x = 84, y = 6;
  graphics_context_set_fill_color(ctx, ink);
  graphics_fill_circle(ctx, GPoint(x + 3, y + 3), 3);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(x + 5, y + 2), 3);
  draw_tiny_text(ctx, typed("QUIET", buf, sizeof(buf), B_TAG_PWR_MS, B_TAG_STEP), x + 8, y + 1, 1, ink);
}

/* ---------- Face ---------- */

static void draw_face(GContext *ctx, GRect bounds) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  draw_status_tag(ctx);
  draw_logo_area(ctx);
  draw_power_tag(ctx);
  draw_quiet_tag(ctx);

  GColor accent = accent_color();
  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, 1);
  if (shown(B_DIVIDER_MS)) {
    int div_len = (101 * ramp(B_DIVIDER_MS, B_DIVIDER_END)) / 256;
    if (div_len > 0) graphics_draw_line(ctx, GPoint(110, 17), GPoint(110, 17 + div_len));
  }

  draw_l_series(ctx);

  /* stroke colour inherited from draw_l_series (accent), width 2 as in 1.0.16 */
  int rule_w = (192 * ramp(0, B_RULE_END)) / 256;
  if (rule_w > 0) graphics_draw_line(ctx, GPoint(4, 118), GPoint(4 + rule_w, 118));

  /* Time digits are drawn on every frame, including the first frame of a sequence. */
  int label_y = s_is_obstructed ? 120 : 122;
  char label[24];
  snprintf(label, sizeof(label), "LOCAL TIME // %s", s_meridiem[0] ? s_meridiem : "ACTIVE");
  char label_buf[24];
  const char *shown_label = typed(label, label_buf, sizeof(label_buf), 0, B_LABEL_STEP);
  draw_tiny_text(ctx, shown_label, 9, label_y, 1, color_grey());
  if (s_meridiem[0] && shown_label == label) {
    draw_tiny_text(ctx, s_meridiem, 9 + tiny_text_width("LOCAL TIME // ", 1) + 1, label_y, 1, GColorWhite);
  }
  draw_text_box(ctx, s_time_text, GRect(0, s_is_obstructed ? 124 : 127, 150, 52),
                s_font_time, GColorWhite, GTextAlignmentCenter);

  draw_bio_mon(ctx, s_is_obstructed ? GRect(152, 118, 44, 44) : GRect(152, 126, 44, 48));

  if (!s_is_obstructed) {
    draw_sync_ratio(ctx, 178);
    graphics_context_set_stroke_color(ctx, accent);
    int lower_w = ((bounds.size.w - 8) * ramp(B_LOWER_MS, B_LOWER_END)) / 256;
    if (lower_w > 0) graphics_draw_line(ctx, GPoint(4, 194), GPoint(4 + lower_w, 194));
    draw_bottom_panel(ctx);
  }
}

/* ---------- Shake screen: weather and heart rate ---------- */

#define PK_GRAPH GRect(10, 66, 181, 100)

static void fill_black(GContext *ctx, GRect r) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
}

static void draw_peek_header(GContext *ctx, const char *title, const char *sub, GColor sub_color) {
  draw_text_box(ctx, title, GRect(4, -2, 130, 18), s_font_chip, accent_color(), GTextAlignmentLeft);
  draw_text_box(ctx, sub, GRect(100, -1, 96, 18), s_font_chip_small, sub_color, GTextAlignmentRight);
  graphics_context_set_stroke_color(ctx, accent_color());
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(0, 18), GPoint(199, 18));
}

static bool use_24h(void) {
  if (s_settings.v[TD_SET_CLOCK] == TD_CLOCK_24H) return true;
  if (s_settings.v[TD_SET_CLOCK] == TD_CLOCK_12H) return false;
  return clock_is_24h_style();
}

/* "19" under a bar and "19:00" in the rain call-out; "7PM" for both in 12 h mode. */
static void format_hour(char *buf, size_t n, int32_t utc, bool full) {
  time_t t = utc;
  struct tm *lt = localtime(&t);
  int h = lt->tm_hour;
  if (use_24h()) {
    snprintf(buf, n, full ? "%02d:00" : "%02d", h);
  } else {
    int h12 = h % 12 ? h % 12 : 12;
    snprintf(buf, n, "%d%s", h12, h < 12 ? "AM" : "PM");
  }
}

/* Weather bars: column geometry shared with the Sync Graph trace. */
#define WX_X0 10
#define WX_COL 30
#define WX_BASE 146
#define WX_MIN_H 12
#define WX_MAX_H 56

static int weather_bar_top(int i, int lo, int hi) {
  int t = s_fc.temp_c[s_fc_first + i];
  int h = hi > lo ? WX_MIN_H + ((t - lo) * (WX_MAX_H - WX_MIN_H)) / (hi - lo) : (WX_MIN_H + WX_MAX_H) / 2;
  return WX_BASE - h;
}

static void forecast_range(int n, int *lo, int *hi) {
  *lo = *hi = s_fc.temp_c[s_fc_first];
  for (int i = 1; i < n; i++) {
    int t = s_fc.temp_c[s_fc_first + i];
    if (t < *lo) *lo = t;
    if (t > *hi) *hi = t;
  }
}

/* parts: 1 header, 2 headline values, 3 everything. Used to type the screen in. */
static void draw_weather_screen(GContext *ctx, int parts) {
  int unit = s_settings.v[TD_SET_TEMP_UNIT];
  int n = td_forecast_shown(&s_fc, s_fc_first);
  TdWeatherState fc_st = td_weather_state(s_fc.ts > 0, s_fc.ts, time(NULL));
  char buf[24];

  if (n > 0) snprintf(buf, sizeof(buf), "NEXT %dH", n);
  else snprintf(buf, sizeof(buf), "NO DATA");
  if (n > 0 && fc_st == TD_WX_STALE) snprintf(buf, sizeof(buf), "STALE");
  draw_peek_header(ctx, "FORECAST", buf, fc_st == TD_WX_FRESH && n > 0 ? GColorWhite : color_grey());
  if (parts < 2) return;

  TdWeatherState st = weather_state();
  td_format_weather(buf, sizeof(buf), st, s_wx_temp, unit);
  draw_text_box(ctx, buf, GRect(6, 14, 110, 34), s_font_big, st == TD_WX_FRESH ? GColorWhite : color_grey(),
                GTextAlignmentLeft);
  if (n > 0) {
    draw_text_box(ctx, td_wmo_label(s_fc.code[s_fc_first]), GRect(96, 20, 98, 20), s_font_badge,
                  GColorChromeYellow, GTextAlignmentRight);
    int rain = td_forecast_rain_at(&s_fc, s_fc_first);
    char hour[8];
    if (rain >= 0) {
      format_hour(hour, sizeof(hour), s_fc.start + (s_fc_first + rain) * 3600, true);
      snprintf(buf, sizeof(buf), rain == 0 ? "RAIN NOW" : "RAIN %s", hour);
    } else {
      snprintf(buf, sizeof(buf), "DRY %dH", n);
    }
    draw_text_box(ctx, buf, GRect(96, 40, 98, 16), s_font_chip_small, GColorVividCerulean, GTextAlignmentRight);
  }
  graphics_context_set_stroke_color(ctx, accent_color());
  graphics_draw_line(ctx, GPoint(0, 62), GPoint(199, 62));
  if (parts < 3) return;

  if (n <= 0) {
    draw_text_box(ctx, "NO FORECAST", GRect(0, 100, 200, 20), s_font_chip, color_grey(), GTextAlignmentCenter);
    return;
  }
  int lo, hi;
  forecast_range(n, &lo, &hi);
  for (int i = 0; i < n; i++) {
    int x = WX_X0 + i * WX_COL;
    int top = weather_bar_top(i, lo, hi);
    graphics_context_set_fill_color(ctx, i == 0 ? accent_color() : GColorDarkCandyAppleRed);
    graphics_fill_rect(ctx, GRect(x + 4, top, WX_COL - 8, WX_BASE - top), 0, GCornerNone);
    snprintf(buf, sizeof(buf), "%d°", td_temp_in_unit(s_fc.temp_c[s_fc_first + i], unit));
    draw_text_box(ctx, buf, GRect(x - 2, top - 18, WX_COL + 4, 16), s_font_chip, GColorWhite, GTextAlignmentCenter);
    format_hour(buf, sizeof(buf), s_fc.start + (s_fc_first + i) * 3600, false);
    draw_text_box(ctx, buf, GRect(x, WX_BASE + 1, WX_COL, 16), s_font_chip_small, color_grey(), GTextAlignmentCenter);

    int pct = s_fc.rain_pct[s_fc_first + i];
    int rh = (pct * 20) / 100;
    graphics_context_set_fill_color(ctx, pct ? GColorVividCerulean : GColorDarkGray);
    graphics_fill_rect(ctx, GRect(x + 4, 199 - (rh > 0 ? rh : 1), WX_COL - 8, rh > 0 ? rh : 1), 0, GCornerNone);
    snprintf(buf, sizeof(buf), "%d%%", pct);
    draw_text_box(ctx, buf, GRect(x - 2, 200, WX_COL + 4, 16), s_font_chip_small, color_grey(), GTextAlignmentCenter);
  }
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_draw_line(ctx, GPoint(0, 166), GPoint(199, 166));
  draw_tiny_text(ctx, "PRECIP", 6, 170, 1, GColorVividCerulean);
}

static int hr_bucket_x(int i) {
  GRect g = PK_GRAPH;
  return g.origin.x + 4 + (i * (g.size.w - 9)) / (TD_HR_BUCKETS - 1);
}

static int hr_bucket_y(int i) {
  GRect g = PK_GRAPH;
  return td_scale_y(s_hr_hist[i], s_hr_stats.min - 5, s_hr_stats.max + 5, g.origin.y, g.origin.y + g.size.h - 1);
}

static void draw_heart_screen(GContext *ctx, int parts) {
  GRect g = PK_GRAPH;
  char buf[16];
  draw_peek_header(ctx, "PILOT VITALS", "HR // 3H", GColorWhite);
  if (parts >= 2) {
    GColor v = s_hr.valid ? GColorWhite : color_grey();
    draw_text_box(ctx, s_hr_text, GRect(6, 14, 70, 34), s_font_big, v, GTextAlignmentLeft);
    GSize sz = graphics_text_layout_get_content_size(s_hr_text, s_font_big, GRect(6, 14, 70, 34),
                                                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    draw_text_box(ctx, "BPM", GRect(10 + sz.w, 30, 40, 16), s_font_chip, accent_color(), GTextAlignmentLeft);
    if (s_hr_stats.count) snprintf(buf, sizeof(buf), "MAX %d", s_hr_stats.max);
    else snprintf(buf, sizeof(buf), "MAX --");
    draw_text_box(ctx, buf, GRect(100, 20, 94, 16), s_font_chip_small, GColorLightGray, GTextAlignmentRight);
    if (s_hr_stats.count) snprintf(buf, sizeof(buf), "MIN %d", s_hr_stats.min);
    else snprintf(buf, sizeof(buf), "MIN --");
    draw_text_box(ctx, buf, GRect(100, 36, 94, 16), s_font_chip_small, GColorLightGray, GTextAlignmentRight);
  }

  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_rect(ctx, g);
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  for (int gy = g.origin.y + 25; gy < g.origin.y + g.size.h - 1; gy += 25) {
    for (int gx = g.origin.x + 2; gx < g.origin.x + g.size.w - 1; gx += 4) {
      graphics_fill_rect(ctx, GRect(gx, gy, 1, 1), 0, GCornerNone);
    }
  }
  if (!s_hr_stats.count) {
    draw_text_box(ctx, "NO HEART RATE DATA", GRect(g.origin.x, g.origin.y + 40, g.size.w, 18), s_font_chip_small,
                  color_grey(), GTextAlignmentCenter);
  } else {
    /* Buckets without a reading break the line: gaps stay gaps. */
    graphics_context_set_stroke_color(ctx, GColorOrange);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_context_set_fill_color(ctx, GColorOrange);
    for (int i = 0; i < TD_HR_BUCKETS; i++) {
      if (s_hr_hist[i] <= 0) continue;
      GPoint p = GPoint(hr_bucket_x(i), hr_bucket_y(i));
      bool prev = i > 0 && s_hr_hist[i - 1] > 0;
      bool next = i + 1 < TD_HR_BUCKETS && s_hr_hist[i + 1] > 0;
      if (next) graphics_draw_line(ctx, p, GPoint(hr_bucket_x(i + 1), hr_bucket_y(i + 1)));
      else if (!prev) graphics_fill_circle(ctx, p, 2);
    }
    graphics_context_set_stroke_width(ctx, 1);
  }
  if (parts < 3) return;

  static const char *labels[] = {"-3H", "-2H", "-1H", "NOW"};
  for (int i = 0; i < 4; i++) {
    int x = g.origin.x + (i * (g.size.w - 1)) / 3;
    GRect r = GRect(x - 20, g.origin.y + g.size.h, 40, 16);
    GTextAlignment a = GTextAlignmentCenter;
    if (i == 0) { r = GRect(x, r.origin.y, 40, 16); a = GTextAlignmentLeft; }
    if (i == 3) { r = GRect(x - 40, r.origin.y, 40, 16); a = GTextAlignmentRight; }
    draw_text_box(ctx, labels[i], r, s_font_chip_small, color_grey(), a);
  }
  graphics_context_set_stroke_color(ctx, accent_color());
  graphics_draw_line(ctx, GPoint(0, 186), GPoint(199, 186));
  draw_text_box(ctx, "AVERAGE", GRect(8, 188, 100, 16), s_font_chip_small, GColorWhite, GTextAlignmentLeft);
  if (s_hr_stats.count) snprintf(buf, sizeof(buf), "%d BPM", s_hr_stats.avg);
  else snprintf(buf, sizeof(buf), "--");
  draw_text_box(ctx, buf, GRect(100, 188, 92, 16), s_font_chip, GColorOrange, GTextAlignmentRight);
  draw_text_box(ctx, "READINGS", GRect(8, 205, 100, 16), s_font_chip_small, color_grey(), GTextAlignmentLeft);
  snprintf(buf, sizeof(buf), "%d/%d", s_hr_stats.count, TD_HR_BUCKETS);
  draw_text_box(ctx, buf, GRect(100, 205, 92, 16), s_font_chip_small, color_grey(), GTextAlignmentRight);
}

static void draw_peek_screen(GContext *ctx, int parts) {
  fill_black(ctx, GRect(0, 0, 200, 228));
  if (s_peek.screen == TD_SHAKE_HEART) draw_heart_screen(ctx, parts);
  else draw_weather_screen(ctx, parts);
}

/* The Sync Graph's waves collapse onto the target screen's own line: the heart-rate
 * trace, or the tops of the forecast bars. Missing data collapses to the centre line. */
#define TRACE_MAX TD_HR_BUCKETS
static int peek_trace(GPoint *pts) {
  int n = 0;
  if (s_peek.screen == TD_SHAKE_HEART) {
    if (!s_hr_stats.count) return 0;
    for (int i = 0; i < TD_HR_BUCKETS; i++) {
      if (s_hr_hist[i] > 0) pts[n++] = GPoint(hr_bucket_x(i), hr_bucket_y(i));
    }
    return n;
  }
  int shown = td_forecast_shown(&s_fc, s_fc_first);
  if (shown <= 0) return 0;
  int lo, hi;
  forecast_range(shown, &lo, &hi);
  for (int i = 0; i < shown; i++) pts[n++] = GPoint(WX_X0 + i * WX_COL + WX_COL / 2, weather_bar_top(i, lo, hi));
  return n;
}

static int trace_y_at(const GPoint *pts, int n, int x, int fallback) {
  if (n <= 0) return fallback;
  if (x <= pts[0].x) return pts[0].y;
  for (int i = 1; i < n; i++) {
    if (x <= pts[i].x) {
      int dx = pts[i].x - pts[i - 1].x;
      return dx ? pts[i - 1].y + ((pts[i].y - pts[i - 1].y) * (x - pts[i - 1].x)) / dx : pts[i].y;
    }
  }
  return pts[n - 1].y;
}

/* ---------- Transition 1: Sync Graph ---------- */
#define SG_OPEN_END 300
#define SG_RULER_END 650
#define SG_BADGE_MS 450
#define SG_CONVERGE_MS 900
#define SG_CONVERGE_END 1300
#define SG_REVEAL_MS 1350
#define SG_REVEAL_STEP 100
#define SG_STRANDS 9

static int smoothstep256(int a, int b, int t) {
  if (t <= a) return 0;
  if (t >= b) return 256;
  int x = ((t - a) * 256) / (b - a);
  return (x * x * (768 - 2 * x)) >> 16;
}

static void draw_cross(GContext *ctx, int x, int y) {
  graphics_draw_line(ctx, GPoint(x - 3, y), GPoint(x + 3, y));
  graphics_draw_line(ctx, GPoint(x, y - 3), GPoint(x, y + 3));
}

static void draw_fx_sync(GContext *ctx, GRect bounds, int t) {
  GRect g = PK_GRAPH;
  int gx1 = g.origin.x + g.size.w - 1, gy1 = g.origin.y + g.size.h - 1;
  int cy = g.origin.y + g.size.h / 2;

  if (t < SG_OPEN_END) {
    /* The graph frame opens out of the face from its centre line. */
    draw_face(ctx, bounds);
    int half = ((g.size.h / 2) * t) / SG_OPEN_END;
    fill_black(ctx, GRect(0, cy - half - 12, 200, 2 * half + 24));
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(g.origin.x, cy - half), GPoint(gx1, cy - half));
    graphics_draw_line(ctx, GPoint(g.origin.x, cy + half), GPoint(gx1, cy + half));
    graphics_context_set_stroke_width(ctx, 1);
    return;
  }
  if (t >= SG_REVEAL_MS) {
    draw_peek_screen(ctx, 1 + (t - SG_REVEAL_MS) / SG_REVEAL_STEP);
    return;
  }

  fill_black(ctx, bounds);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  /* rulers draw in left to right */
  int upto = g.origin.x + ((g.size.w - 2) * smoothstep256(SG_OPEN_END, SG_RULER_END, t)) / 256;
  for (int x = g.origin.x + 2, i = 0; x < upto; x += 3, i++) {
    int len = i % 10 == 0 ? 9 : i % 5 == 0 ? 6 : 3;
    graphics_draw_line(ctx, GPoint(x, g.origin.y - 3), GPoint(x, g.origin.y - 3 - len));
    graphics_draw_line(ctx, GPoint(x, gy1 + 3), GPoint(x, gy1 + 3 + len));
  }
  for (int y = g.origin.y + 4, i = 0; y < gy1 - 1; y += 4, i++) {
    graphics_draw_line(ctx, GPoint(g.origin.x - (i % 5 == 0 ? 6 : 3), y), GPoint(g.origin.x - 1, y));
  }
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_rect(ctx, g);
  graphics_context_set_stroke_width(ctx, 1);

  graphics_context_set_stroke_color(ctx, GColorOrange);
  for (int x = g.origin.x + 20; x < gx1 - 4; x += 40) {
    draw_cross(ctx, x, g.origin.y + 10);
    draw_cross(ctx, x, cy);
    draw_cross(ctx, x, gy1 - 10);
  }

  /* Phase-shifted strands, as in the sync-ratio plots; then collapse onto the trace. */
  GPoint trace[TRACE_MAX];
  int tn = peek_trace(trace);
  int c = smoothstep256(SG_CONVERGE_MS, SG_CONVERGE_END, t);
  int amp = g.size.h / 2 - 8;
  int32_t phase = (t * TRIG_MAX_ANGLE) / 1400;
  for (int s = 0; s < SG_STRANDS; s++) {
    graphics_context_set_stroke_color(ctx, (c > 160 && s == SG_STRANDS / 2) ? GColorOrange : GColorWhite);
    GPoint prev = GPoint(0, 0);
    for (int x = g.origin.x + 2; x <= gx1 - 2; x += 4) {
      int32_t ang = ((x - g.origin.x) * TRIG_MAX_ANGLE) / 190 - phase - s * (TRIG_MAX_ANGLE / 28);
      int yw = cy + (amp * sin_lookup(ang)) / TRIG_MAX_RATIO;
      int yt = trace_y_at(trace, tn, x, cy);
      GPoint p = GPoint(x, yw + ((yt - yw) * c) / 256);
      if (x > g.origin.x + 2) graphics_draw_line(ctx, prev, p);
      prev = p;
    }
  }

  draw_text_box(ctx, "SYNC GRAPH", GRect(10, 14, 100, 18), s_font_chip, GColorWhite, GTextAlignmentLeft);
  draw_tiny_text(ctx, "PATTERN ANALYSIS", 10, 34, 1, color_grey());

  /* EVA 01 badge, with one flicker as it powers on */
  bool flicker = t >= SG_BADGE_MS + 66 && t < SG_BADGE_MS + 133;
  if (t >= SG_BADGE_MS && !flicker) {
    GColor col = t < SG_BADGE_MS + 66 ? GColorChromeYellow : GColorOrange;
    GRect b = GRect(110, 14, 84, 38);
    fill_black(ctx, b);
    graphics_context_set_stroke_color(ctx, col);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, b, 6);
    graphics_draw_line(ctx, GPoint(115, 36), GPoint(189, 36));
    graphics_context_set_stroke_width(ctx, 1);
    draw_text_box(ctx, "EVA 01", GRect(115, 11, 76, 22), s_font_badge, col, GTextAlignmentLeft);
    draw_tiny_text(ctx, "SUBJECT", 115, 41, 1, col);
    draw_text_box(ctx, s_peek.screen == TD_SHAKE_HEART ? "HEART" : "WEATHER", GRect(144, 35, 48, 16), s_font_chip,
                  col, GTextAlignmentLeft);
  }
}

/* ---------- Transition 2: Emergency Hex ---------- */
#define HX_R 22
#define HX_DX 33        /* 1.5 R */
#define HX_DY 38        /* sqrt(3) R */
#define HX_GROW_MS 130
#define HX_BLACK_MS 300 /* face hidden behind the panels from here */
#define HX_STROBE_MS 650
#define HX_STROBE_STEP 100
#define HX_COLLAPSE_MS 950
#define HX_SHRINK_MS 110

static void hex_points(GPoint *p, int cx, int cy, int r) {
  int h = (r * 222) / 256; /* r * sin(60) */
  p[0] = GPoint(cx + r, cy);
  p[1] = GPoint(cx + r / 2, cy + h);
  p[2] = GPoint(cx - r / 2, cy + h);
  p[3] = GPoint(cx - r, cy);
  p[4] = GPoint(cx - r / 2, cy - h);
  p[5] = GPoint(cx + r / 2, cy - h);
}

enum { HX_PLAIN, HX_TIME, HX_WARN, HX_NERV };

static int hex_kind(int col, int row) {
  if (col == 3 && row == 3) return HX_TIME;
  if (col == 2 && row == 2) return HX_WARN;
  if (col == 4 && row == 4) return HX_NERV;
  return HX_PLAIN;
}

static void draw_hex_cell(GContext *ctx, int cx, int cy, int r, int kind, bool lit) {
  if (r < 2) return;
  GPoint p[6];
  hex_points(p, cx, cy, r);
  fill_polygon_at(ctx, GPoint(0, 0), p, 6, lit ? GColorRed : GColorBulgarianRose);
  if (r < HX_R - 6) return;
  GColor ink = lit ? GColorBlack : GColorDarkCandyAppleRed;
  switch (kind) {
    case HX_TIME:
      draw_text_box(ctx, s_time_text, GRect(cx - r, cy - 10, 2 * r, 18), s_font_chip, ink, GTextAlignmentCenter);
      return;
    case HX_NERV:
      draw_text_box(ctx, "NERV", GRect(cx - r, cy - 10, 2 * r, 18), s_font_chip, ink, GTextAlignmentCenter);
      return;
    case HX_WARN:
      draw_tiny_centered(ctx, "WARNING", GRect(cx - r, cy - 3, 2 * r, 7), 1, ink);
      break;
    default:
      draw_tiny_centered(ctx, "EMERGENCY", GRect(cx - r, cy - 3, 2 * r, 7), 1, ink);
      break;
  }
  int s = (r * 5) / 22, ty = (r * 14) / 22;
  GPoint up[] = {GPoint(cx, cy - ty - s), GPoint(cx - s, cy - ty + s / 2), GPoint(cx + s, cy - ty + s / 2)};
  GPoint dn[] = {GPoint(cx, cy + ty + s), GPoint(cx - s, cy + ty - s / 2), GPoint(cx + s, cy + ty - s / 2)};
  fill_polygon_at(ctx, GPoint(0, 0), up, 3, ink);
  fill_polygon_at(ctx, GPoint(0, 0), dn, 3, ink);
}

static void draw_fx_hex(GContext *ctx, GRect bounds, int t) {
  if (t < HX_BLACK_MS) draw_face(ctx, bounds);
  else if (t < HX_COLLAPSE_MS) fill_black(ctx, bounds);
  else draw_peek_screen(ctx, 99);

  bool strobe_dim = t >= HX_STROBE_MS && t < HX_COLLAPSE_MS && ((t - HX_STROBE_MS) / HX_STROBE_STEP) % 2 == 0;
  for (int col = 0; col * HX_DX - 4 < 200 + HX_R; col++) {
    int cx = col * HX_DX - 4;
    for (int row = 0;; row++) {
      int cy = row * HX_DY - 6 + (col % 2 ? HX_DY / 2 : 0);
      if (cy > 228 + HX_R) break;
      int kind = hex_kind(col, row);
      int r;
      if (t < HX_COLLAPSE_MS) {
        /* cascade in from the right, with a fixed per-cell stagger */
        int delay = ((200 - cx) * 2) + ((col * 7 + row * 13) % 5) * 30;
        if (delay < 0) delay = 0;
        if (t < delay) continue;
        r = t - delay >= HX_GROW_MS ? HX_R : (HX_R * (t - delay)) / HX_GROW_MS;
      } else {
        /* panels drop away left to right over the new screen */
        int at = HX_COLLAPSE_MS + (cx > 0 ? cx * 2 : 0);
        if (t >= at + HX_SHRINK_MS) continue;
        r = t <= at ? HX_R : (HX_R * (at + HX_SHRINK_MS - t)) / HX_SHRINK_MS;
        GPoint gap[6];
        hex_points(gap, cx, cy, r + 1);
        fill_polygon_at(ctx, GPoint(0, 0), gap, 6, GColorBlack);
      }
      draw_hex_cell(ctx, cx, cy, r - 2, kind, !strobe_dim || kind != HX_PLAIN);
    }
  }
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  switch (s_peek.phase) {
    case TD_PEEK_HOLD: draw_peek_screen(ctx, 99); return;
    case TD_PEEK_IN:
    case TD_PEEK_OUT: /* the return plays the opening backwards */
      if (s_peek.fx == TD_FX_EMERGENCY_HEX) draw_fx_hex(ctx, bounds, td_peek_frame_ms(&s_peek));
      else draw_fx_sync(ctx, bounds, td_peek_frame_ms(&s_peek));
      return;
    default: draw_face(ctx, bounds); return;
  }
}

static void mark_dirty(void) {
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

/* ---------- Data ---------- */

static void update_health(void) {
  s_steps = (TdReading){false, 0};
  s_hr = (TdReading){false, 0};
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t now = time(NULL);
  if (health_service_metric_accessible(HealthMetricStepCount, start, now) & HealthServiceAccessibilityMaskAvailable) {
    s_steps = (TdReading){true, health_service_sum(HealthMetricStepCount, start, now)};
  }
  /* HeartRateBPM is the firmware's filtered value, at most 15 minutes old, and 0
   * when there is none. Re-read every minute and never hold a previous value. */
  if (health_service_metric_aggregate_averaged_accessible(HealthMetricHeartRateBPM, now, now,
        HealthAggregationAvg, HealthServiceTimeScopeOnce) & HealthServiceAccessibilityMaskAvailable) {
    HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
    s_hr = (TdReading){hr > 0, hr};
  }
#endif
  td_format_steps(s_steps_text, sizeof(s_steps_text), s_steps);
  td_format_hr(s_hr_text, sizeof(s_hr_text), s_hr);
}

static void update_time(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  bool use_24 = clock_is_24h_style();
  if (s_settings.v[TD_SET_CLOCK] == TD_CLOCK_24H) use_24 = true;
  if (s_settings.v[TD_SET_CLOCK] == TD_CLOCK_12H) use_24 = false;
  td_format_time(s_time_text, sizeof(s_time_text), s_meridiem, tick_time->tm_hour, tick_time->tm_min, use_24);

  strftime(s_date_text, sizeof(s_date_text), "%b %d", tick_time);
  for (int i = 0; s_date_text[i]; i++) {
    if (s_date_text[i] >= 'a' && s_date_text[i] <= 'z') s_date_text[i] -= 32;
  }
}

/* Quiet Time has no change event, so it is polled with the minute tick. */
static void update_quiet_time(void) {
  s_quiet_time = quiet_time_is_active();
}

static void update_battery(BatteryChargeState state) {
  s_battery_pct = state.charge_percent;
  s_battery_charging = state.is_charging;
  s_battery_plugged = state.is_plugged;
  snprintf(s_battery_text, sizeof(s_battery_text), "%d%%", s_battery_pct);
  td_format_power(s_power_text, sizeof(s_power_text), s_battery_pct, battery_state());
}

/* ---------- Settings ---------- */

static void load_settings(void) {
  int stored[TD_SETTING_COUNT];
  uint32_t present = 0;
  for (int i = 0; i < TD_SETTING_COUNT; i++) {
    stored[i] = 0;
    if (persist_exists(s_setting_keys[i])) {
      stored[i] = persist_read_int(s_setting_keys[i]);
      present |= 1u << i;
    }
  }
  int schema = persist_exists(PERSIST_SCHEMA) ? persist_read_int(PERSIST_SCHEMA) : (present ? 1 : 0);
  uint32_t write = td_settings_load(&s_settings, stored, present, schema);
  for (int i = 0; i < TD_SETTING_COUNT; i++) {
    if (write & (1u << i)) persist_write_int(s_setting_keys[i], s_settings.v[i]);
  }
  if (schema != TD_SETTINGS_SCHEMA) persist_write_int(PERSIST_SCHEMA, TD_SETTINGS_SCHEMA);
}

static void load_weather_cache(void) {
  if (persist_exists(PERSIST_WX_TEMP) && persist_exists(PERSIST_WX_TIME)) {
    s_wx_temp = persist_read_int(PERSIST_WX_TEMP);
    s_wx_ts = persist_read_int(PERSIST_WX_TIME);
    s_wx_have = s_wx_ts > 0;
  }
}

/* ---------- Watch -> phone ---------- */

static void outbox_pump(void) {
  if (s_out_inflight || !s_out_pending) return;
  /* Nothing is retried while the phone is away; reconnecting pumps again. */
  if (!connection_service_peek_pebble_app_connection()) return;
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return; /* retried on the next event */
  uint8_t which = (s_out_pending & OUT_SETTINGS) ? OUT_SETTINGS : OUT_WEATHER;
  if (which == OUT_SETTINGS) {
    const uint32_t keys[TD_SETTING_COUNT] = SETTING_MESSAGE_KEYS;
    for (int i = 0; i < TD_SETTING_COUNT; i++) dict_write_int32(iter, keys[i], s_settings.v[i]);
  } else {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
    td_weather_note_attempt(&s_wx_sched, time(NULL));
  }
  if (app_message_outbox_send() == APP_MSG_OK) {
    s_out_inflight = which;
  } else if (which == OUT_WEATHER) {
    s_out_pending &= ~OUT_WEATHER; /* counted as an attempt: the backoff schedule asks again */
  }
}

static void outbox_sent_callback(DictionaryIterator *iter, void *context) {
  s_out_pending &= ~s_out_inflight;
  s_out_inflight = 0;
  outbox_pump();
}

static void outbox_failed_callback(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  /* Weather: the backoff schedule decides when to ask again. Settings: a few tries. */
  if (s_out_inflight == OUT_WEATHER ||
      (s_out_inflight == OUT_SETTINGS && ++s_settings_reply_tries >= SETTINGS_REPLY_TRIES)) {
    s_out_pending &= ~s_out_inflight;
  }
  s_out_inflight = 0;
  outbox_pump();
}

static void maybe_request_weather(void) {
  if (td_weather_should_request(&s_wx_sched, s_link.connected, s_wx_have, s_wx_ts, time(NULL))) {
    s_out_pending |= OUT_WEATHER;
    outbox_pump();
  }
}

/* ---------- Transitions ---------- */

static void anim_resolve_votes(void) {
  uint32_t due = td_anim_due_votes(&s_anim);
  if (due & (1u << TD_CHECK_PWR)) s_anim.votes[TD_CHECK_PWR] = td_vote_power(s_battery_pct, s_battery_charging);
  if (due & (1u << TD_CHECK_LINK)) s_anim.votes[TD_CHECK_LINK] = td_vote_link(s_link.connected);
  if (due & (1u << TD_CHECK_DATA)) s_anim.votes[TD_CHECK_DATA] = td_vote_data(weather_state());
}

static void anim_step_callback(void *context) {
  s_anim_timer = NULL;
  int64_t now = now_ms();
  int dt = (int)(now - s_anim_last_ms);
  s_anim_last_ms = now;
  if (td_anim_step(&s_anim, dt)) {
    anim_resolve_votes();
    s_anim_timer = app_timer_register(TD_FRAME_MS, anim_step_callback, NULL);
  } else {
    APP_LOG(APP_LOG_LEVEL_INFO, "TD anim end steps=%d t=%d;", s_anim.steps, s_anim.t_ms);
  }
  mark_dirty();
}

static void anim_cancel(const char *why) {
  if (s_anim_timer) {
    app_timer_cancel(s_anim_timer);
    s_anim_timer = NULL;
  }
  if (s_anim.kind != TD_ANIM_NONE) {
    APP_LOG(APP_LOG_LEVEL_INFO, "TD anim cancel reason=%s;", why);
    td_anim_cancel(&s_anim);
    mark_dirty();
  }
}

static TdAnimEnv anim_env(void) {
  return (TdAnimEnv){
    .motion = s_settings.v[TD_SET_MOTION],
    .battery_pct = s_battery_pct,
    .charging = s_battery_charging,
    .obstructed = s_is_obstructed,
  };
}

static void anim_check_abort(const char *why) {
  TdAnimEnv env = anim_env();
  if (td_anim_should_abort(&s_anim, &env)) anim_cancel(why);
}

static void anim_trigger(TdTrigger trig) {
  if (!s_canvas_layer) return;
  if (td_peek_open(&s_peek)) {
    APP_LOG(APP_LOG_LEVEL_INFO, "TD anim trigger=%d decision=peek;", (int)trig);
    return;
  }
  TdAnimEnv env = anim_env();
  bool persist_full = false;
  TdDecision d = td_anim_trigger(&s_anim, trig, &env, time(NULL), &persist_full);
  APP_LOG(APP_LOG_LEVEL_INFO, "TD anim trigger=%d decision=%s kind=%d;", (int)trig, td_decision_name(d),
          (int)s_anim.kind);
  if (persist_full) persist_write_int(PERSIST_LAST_FULL, (int32_t)s_anim.last_full_s);
  if (d != TD_DECIDE_START) return;
  s_anim_last_ms = now_ms();
  s_anim_timer = app_timer_register(TD_FRAME_MS, anim_step_callback, NULL);
  mark_dirty();
}

static int32_t tuple_int(const Tuple *t) {
  if (t->type == TUPLE_UINT) {
    return t->length == 1 ? t->value->uint8 : t->length == 2 ? t->value->uint16 : (int32_t)t->value->uint32;
  }
  return t->length == 1 ? t->value->int8 : t->length == 2 ? t->value->int16 : t->value->int32;
}

/* ---------- Shake screen ---------- */

/* Health is read once, when the screen opens: 10-minute averages over the last 3 hours.
 * A bucket the firmware has no heart rate for stays 0 and is drawn as a gap. */
static void peek_read_heart(void) {
  memset(s_hr_hist, 0, sizeof(s_hr_hist));
#if defined(PBL_HEALTH)
  time_t end = time(NULL);
  time_t start = end - TD_HR_BUCKETS * TD_HR_BUCKET_S;
  for (int i = 0; i < TD_HR_BUCKETS; i++) {
    time_t a = start + i * TD_HR_BUCKET_S, b = a + TD_HR_BUCKET_S;
    if (health_service_metric_aggregate_averaged_accessible(HealthMetricHeartRateBPM, a, b, HealthAggregationAvg,
          HealthServiceTimeScopeOnce) & HealthServiceAccessibilityMaskAvailable) {
      HealthValue v = health_service_aggregate_averaged(HealthMetricHeartRateBPM, a, b, HealthAggregationAvg,
                                                        HealthServiceTimeScopeOnce);
      s_hr_hist[i] = (v > 0 && v < 255) ? (int16_t)v : 0;
    }
  }
#endif
  s_hr_stats = td_hr_stats(s_hr_hist, TD_HR_BUCKETS);
}

static void peek_prepare(void) {
  update_health();
  if (s_peek.screen == TD_SHAKE_HEART) peek_read_heart();
  s_fc_first = td_forecast_first(&s_fc, time(NULL));
}

static void peek_cancel_timers(void) {
  if (s_peek_timer) {
    app_timer_cancel(s_peek_timer);
    s_peek_timer = NULL;
  }
  if (s_peek_hold_timer) {
    app_timer_cancel(s_peek_hold_timer);
    s_peek_hold_timer = NULL;
  }
}

static void peek_close(const char *why) {
  peek_cancel_timers();
  if (td_peek_open(&s_peek)) {
    APP_LOG(APP_LOG_LEVEL_INFO, "TD peek close reason=%s;", why);
    td_peek_close(&s_peek);
    mark_dirty();
  }
}

static void peek_enter_hold(void);
static void peek_step_callback(void *context);

static TdPeekEnv peek_env(void) {
  return (TdPeekEnv){
    .shake = s_settings.v[TD_SET_SHAKE],
    .fx = s_settings.v[TD_SET_SHAKE_FX],
    .motion = s_settings.v[TD_SET_MOTION],
    .battery_pct = s_battery_pct,
    .charging = s_battery_charging,
  };
}

/* Applies a return decision from td_peek_dismiss or td_peek_tap. */
static void peek_apply_close(TdPeekEvent ev, const char *why) {
  if (ev == TD_PEEK_CLOSE) {
    APP_LOG(APP_LOG_LEVEL_INFO, "TD peek close reason=%s cut;", why);
    peek_cancel_timers();
    mark_dirty();
  } else if (ev == TD_PEEK_CLOSE_FX) {
    APP_LOG(APP_LOG_LEVEL_INFO, "TD peek close reason=%s fx=%d;", why, s_peek.fx);
    if (s_peek_hold_timer) {
      app_timer_cancel(s_peek_hold_timer);
      s_peek_hold_timer = NULL;
    }
    /* Reversing mid-opening reuses the running frame timer. */
    if (!s_peek_timer) {
      s_peek_last_ms = now_ms();
      s_peek_timer = app_timer_register(TD_FRAME_MS, peek_step_callback, NULL);
    }
    mark_dirty();
  }
}

static void peek_hold_expired(void *context) {
  s_peek_hold_timer = NULL;
  TdPeekEnv env = peek_env();
  peek_apply_close(td_peek_dismiss(&s_peek, &env), "timeout");
}

static void peek_enter_hold(void) {
  s_peek.phase = TD_PEEK_HOLD;
  if (s_peek_timer) {
    app_timer_cancel(s_peek_timer);
    s_peek_timer = NULL;
  }
  if (!s_peek_hold_timer) s_peek_hold_timer = app_timer_register(TD_PEEK_HOLD_MS, peek_hold_expired, NULL);
}

static void peek_step_callback(void *context) {
  s_peek_timer = NULL;
  int64_t now = now_ms();
  int dt = (int)(now - s_peek_last_ms);
  s_peek_last_ms = now;
  if (td_peek_step(&s_peek, dt)) {
    s_peek_timer = app_timer_register(TD_FRAME_MS, peek_step_callback, NULL);
  } else if (s_peek.phase == TD_PEEK_HOLD) {
    peek_enter_hold();
  } else {
    APP_LOG(APP_LOG_LEVEL_INFO, "TD peek closed steps=%d;", s_peek.steps);
  }
  mark_dirty();
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  if (!s_canvas_layer) return;
  TdPeekEnv env = peek_env();
  TdPeekEvent ev = td_peek_tap(&s_peek, &env, now_ms());
  APP_LOG(APP_LOG_LEVEL_INFO, "TD peek tap event=%d screen=%d fx=%d;", (int)ev, s_peek.screen, s_peek.fx);
  switch (ev) {
    case TD_PEEK_OPEN_FX:
    case TD_PEEK_OPEN_CUT:
      anim_cancel("peek");
      peek_prepare();
      if (ev == TD_PEEK_OPEN_FX) {
        s_peek_last_ms = now_ms();
        s_peek_timer = app_timer_register(TD_FRAME_MS, peek_step_callback, NULL);
      } else {
        peek_enter_hold();
      }
      mark_dirty();
      break;
    case TD_PEEK_CLOSE:
    case TD_PEEK_CLOSE_FX:
      peek_apply_close(ev, "shake");
      break;
    default:
      break;
  }
}

/* The accelerometer tap service is only subscribed while the shake screen is on. */
static void peek_sync_subscription(void) {
  bool want = s_settings.v[TD_SET_SHAKE] != TD_SHAKE_OFF;
  if (want && !s_tap_subscribed) {
    accel_tap_service_subscribe(tap_handler);
    s_tap_subscribed = true;
  } else if (!want && s_tap_subscribed) {
    accel_tap_service_unsubscribe();
    s_tap_subscribed = false;
  }
}

static void load_forecast_cache(void) {
  if (persist_exists(PERSIST_FORECAST) && persist_get_size(PERSIST_FORECAST) == (int)sizeof(s_fc)) {
    persist_read_data(PERSIST_FORECAST, &s_fc, sizeof(s_fc));
    if (s_fc.count > TD_FC_SLOTS) memset(&s_fc, 0, sizeof(s_fc));
  }
}

/* The hourly forecast arrives with the current temperature and shares its observation time. */
static void receive_forecast(DictionaryIterator *iterator, int64_t ts) {
  Tuple *start = dict_find(iterator, MESSAGE_KEY_FORECAST_START);
  Tuple *temps = dict_find(iterator, MESSAGE_KEY_FORECAST_TEMPS);
  if (!start || !temps || temps->type != TUPLE_BYTE_ARRAY || temps->length == 0) return;
  Tuple *rain = dict_find(iterator, MESSAGE_KEY_FORECAST_RAIN);
  Tuple *codes = dict_find(iterator, MESSAGE_KEY_FORECAST_CODES);
  TdForecast f;
  memset(&f, 0, sizeof(f));
  f.start = tuple_int(start);
  f.ts = (int32_t)ts;
  f.count = temps->length < TD_FC_SLOTS ? temps->length : TD_FC_SLOTS;
  for (int i = 0; i < f.count; i++) {
    f.temp_c[i] = (int8_t)temps->value->data[i];
    f.rain_pct[i] = (rain && rain->type == TUPLE_BYTE_ARRAY && i < rain->length) ? rain->value->data[i] : 0;
    f.code[i] = (codes && codes->type == TUPLE_BYTE_ARRAY && i < codes->length) ? codes->value->data[i] : 255;
    if (f.rain_pct[i] > 100) f.rain_pct[i] = 100;
  }
  s_fc = f;
  persist_write_data(PERSIST_FORECAST, &s_fc, sizeof(s_fc));
}

/* ---------- Handlers ---------- */

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  const uint32_t keys[TD_SETTING_COUNT] = SETTING_MESSAGE_KEYS;
  TdSettings next = s_settings;
  for (int i = 0; i < TD_SETTING_COUNT; i++) {
    Tuple *t = dict_find(iterator, keys[i]);
    if (t) next.v[i] = td_setting_clamp(i, (int)tuple_int(t));
  }
  uint32_t changed = td_settings_diff(&next, &s_settings);
  for (int i = 0; i < TD_SETTING_COUNT; i++) {
    if (changed & (1u << i)) persist_write_int(s_setting_keys[i], next.v[i]);
  }
  s_settings = next;
  if (changed & (1u << TD_SET_MOTION)) anim_cancel("setting");
  if (changed & ((1u << TD_SET_MOTION) | (1u << TD_SET_SHAKE) | (1u << TD_SET_SHAKE_FX))) peek_close("setting");
  peek_sync_subscription();

  Tuple *temp = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
  if (temp && (temp->type == TUPLE_INT || temp->type == TUPLE_UINT)) {
    time_t now = time(NULL);
    Tuple *wtime = dict_find(iterator, MESSAGE_KEY_WEATHER_TIME);
    TdWeatherState before = weather_state();
    s_wx_temp = (int)tuple_int(temp);
    s_wx_ts = td_weather_accept_ts(wtime != NULL, wtime ? tuple_int(wtime) : 0, now);
    s_wx_have = true;
    persist_write_int(PERSIST_WX_TEMP, s_wx_temp);
    persist_write_int(PERSIST_WX_TIME, (int32_t)s_wx_ts);
    td_weather_note_success(&s_wx_sched);
    receive_forecast(iterator, s_wx_ts);
    if (td_weather_is_recovery(before, weather_state())) anim_trigger(TD_TRIG_DATA_RECOVERED);
  }

  if (dict_find(iterator, MESSAGE_KEY_REQUEST_SETTINGS)) {
    s_settings_reply_tries = 0;
    s_out_pending |= OUT_SETTINGS;
    outbox_pump();
  }

  if (changed) update_time();
  mark_dirty();
}

static void battery_handler(BatteryChargeState state) {
  update_battery(state);
  anim_check_abort("battery");
  /* Same rule as the face animations: no transition frames at critical battery. */
  if (battery_state() == TD_BATT_CRITICAL) {
    if (s_peek.phase == TD_PEEK_IN) peek_enter_hold();
    else if (s_peek.phase == TD_PEEK_OUT) peek_close("battery");
  }
  mark_dirty();
}

static void app_connection_handler(bool connected) {
  bool restored = td_link_update(&s_link, connected, time(NULL));
  if (connected) {
    td_weather_note_reconnect(&s_wx_sched);
    maybe_request_weather();
    outbox_pump();
  }
  if (restored) anim_trigger(TD_TRIG_LINK_RESTORED);
  mark_dirty();
}

static void set_obstructed(bool obstructed) {
  s_is_obstructed = obstructed;
  anim_check_abort("obstructed");
}

static void prv_unobstructed_will_change(GRect final_unobstructed_screen_area, void *context) {
  GRect full_bounds = layer_get_bounds(s_window_layer);
  set_obstructed(!grect_equal(&full_bounds, &final_unobstructed_screen_area));
}

static void prv_unobstructed_change(AnimationProgress progress, void *context) {
  mark_dirty();
}

static void prv_unobstructed_did_change(void *context) {
  GRect full_bounds = layer_get_bounds(s_window_layer);
  GRect unobstructed = layer_get_unobstructed_bounds(s_window_layer);
  set_obstructed(!grect_equal(&full_bounds, &unobstructed));
  mark_dirty();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
  update_health();
  update_quiet_time();
  maybe_request_weather();
  mark_dirty();
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  s_window_layer = window_layer;

  s_font_time = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  s_font_chip = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_font_chip_small = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  s_font_big = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_font_badge = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  s_logo_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_NERV_LOGO);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  update_battery(battery_state_service_peek());
  update_time();
  update_health();
  update_quiet_time();

  GRect unobstructed = layer_get_unobstructed_bounds(s_window_layer);
  s_is_obstructed = !grect_equal(&bounds, &unobstructed);
  UnobstructedAreaHandlers handlers = {
    .will_change = prv_unobstructed_will_change,
    .change = prv_unobstructed_change,
    .did_change = prv_unobstructed_did_change
  };
  unobstructed_area_service_subscribe(handlers, NULL);

  anim_trigger(TD_TRIG_LAUNCH);
}

static void main_window_unload(Window *window) {
  anim_cancel("unload");
  peek_close("unload");
  unobstructed_area_service_unsubscribe();
  if (s_logo_bitmap) {
    gbitmap_destroy(s_logo_bitmap);
    s_logo_bitmap = NULL;
  }
  if (s_canvas_layer) {
    layer_destroy(s_canvas_layer);
    s_canvas_layer = NULL;
  }
}

static void init(void) {
  load_settings();
  load_weather_cache();
  load_forecast_cache();
  td_anim_init(&s_anim, persist_exists(PERSIST_LAST_FULL) ? persist_read_int(PERSIST_LAST_FULL) : 0);
  bool connected = connection_service_peek_pebble_app_connection();
  s_link = (TdLink){connected, connected ? 0 : time(NULL)};

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_open(256, 128);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = app_connection_handler,
  });
  peek_sync_subscription();
  window_stack_push(s_main_window, false);
}

static void deinit(void) {
  if (s_tap_subscribed) accel_tap_service_unsubscribe();
  connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  app_message_deregister_callbacks();
  if (s_main_window) window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
