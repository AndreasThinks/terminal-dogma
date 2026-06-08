#include <pebble.h>

#define SCREEN_W 200
#define SCREEN_H 228
#define BOTTOM_Y 195
#define SETTINGS_KEY_CLOCK_FORMAT 1001
#define SETTINGS_KEY_BOTTOM_LAYOUT 1003
#define SETTINGS_KEY_ACCENT_COLOR 1004
#define SETTINGS_KEY_TEMP_UNIT 1005
#define SETTINGS_KEY_SYNC_SOURCE 1006

#define CLOCK_SYSTEM 0
#define CLOCK_24H 1
#define CLOCK_12H 2

#define LAYOUT_DATE_STEPS_WEATHER 0
#define LAYOUT_DATE_BATTERY_WEATHER 1
#define LAYOUT_DATE_STEPS_BATTERY 2

#define ACCENT_RED 0
#define ACCENT_AMBER 1
#define ACCENT_GREEN 2
#define ACCENT_CYAN 3

#define TEMP_CELSIUS 0
#define TEMP_FAHRENHEIT 1

#define SYNC_SOURCE_FRESHNESS 0
#define SYNC_SOURCE_STEPS 1
#define SYNC_SOURCE_BATTERY 2
#define SYNC_SOURCE_AESTHETIC 3

#define STEPS_GOAL 10000

static Window *s_main_window;
static Layer *s_canvas_layer;
static GBitmap *s_logo_bitmap;

static GFont s_font_time;
static GFont s_font_chip;
static GFont s_font_chip_small;

extern uint32_t MESSAGE_KEY_CLOCK_FORMAT;
extern uint32_t MESSAGE_KEY_BOTTOM_LAYOUT;
extern uint32_t MESSAGE_KEY_ACCENT_COLOR;
extern uint32_t MESSAGE_KEY_TEMP_UNIT;
extern uint32_t MESSAGE_KEY_SYNC_SOURCE;
extern uint32_t MESSAGE_KEY_TEMPERATURE;
extern uint32_t MESSAGE_KEY_REQUEST_WEATHER;

static char s_time_text[8];
static char s_date_text[16];
static char s_steps_text[16];
static char s_battery_text[8];
static int s_battery_pct = 72;
static int s_steps_count = 7061;
static int s_heart_rate = 72;

static int s_temperature = 18;
static bool s_weather_valid = false;
static time_t s_last_weather_time = 0;

static int s_clock_format = CLOCK_SYSTEM;
static int s_bottom_layout = LAYOUT_DATE_STEPS_WEATHER;
static int s_accent_color = ACCENT_RED;
static int s_temp_unit = TEMP_CELSIUS;
static int s_sync_source = SYNC_SOURCE_FRESHNESS;
static Layer *s_window_layer;
static bool s_is_obstructed;

static GColor color_red(void) { return GColorFromHEX(0xFF2424); }
static GColor color_amber(void) { return GColorFromHEX(0xFFAA00); }
static GColor color_green(void) { return GColorFromHEX(0x34E07A); }
static GColor color_cyan(void) { return GColorFromHEX(0x2FE6E6); }
static GColor color_yellow(void) { return GColorFromHEX(0xFFD400); }
static GColor color_pink(void) { return GColorFromHEX(0xFF8A9B); }
static GColor color_grey(void) { return GColorFromHEX(0x8C8C8C); }

static GColor accent_color(void) {
  switch (s_accent_color) {
    case ACCENT_AMBER: return color_amber();
    case ACCENT_GREEN: return color_green();
    case ACCENT_CYAN: return color_cyan();
    case ACCENT_RED:
    default: return color_red();
  }
}

static void draw_text_box(GContext *ctx, const char *text, GRect box, GFont font,
                          GColor color, GTextAlignment align) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis,
                     align, NULL);
}


static uint8_t glyph_row(char c, int row) {
  const uint8_t blank[5] = {0, 0, 0, 0, 0};
  const uint8_t *g = blank;
  static const uint8_t A[5] = {2, 5, 7, 5, 5};
  static const uint8_t B[5] = {6, 5, 6, 5, 6};
  static const uint8_t C[5] = {7, 4, 4, 4, 7};
  static const uint8_t D[5] = {6, 5, 5, 5, 6};
  static const uint8_t E[5] = {7, 4, 6, 4, 7};
  static const uint8_t H[5] = {5, 5, 7, 5, 5};
  static const uint8_t I[5] = {7, 2, 2, 2, 7};
  static const uint8_t L[5] = {4, 4, 4, 4, 7};
  static const uint8_t M[5] = {5, 7, 7, 5, 5};
  static const uint8_t O[5] = {7, 5, 5, 5, 7};
  static const uint8_t P[5] = {6, 5, 6, 4, 4};
  static const uint8_t R[5] = {6, 5, 6, 5, 5};
  static const uint8_t S[5] = {7, 4, 7, 1, 7};
  static const uint8_t T[5] = {7, 2, 2, 2, 2};
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

  switch (c) {
    case 'A': g = A; break; case 'B': g = B; break; case 'C': g = C; break;
    case 'D': g = D; break; case 'E': g = E; break; case 'H': g = H; break; case 'I': g = I; break;
    case 'L': g = L; break; case 'M': g = M; break; case 'O': g = O; break;
    case 'P': g = P; break; case 'R': g = R; break; case 'S': g = S; break;
    case 'T': g = T; break; case 'V': g = V; break; case 'W': g = W; break;
    case 'Y': g = Y; break;
    case '0': g = D0; break; case '1': g = D1; break; case '2': g = D2; break;
    case '3': g = D3; break; case '4': g = D4; break; case '5': g = D5; break;
    case '6': g = D6; break; case '7': g = D7; break; case '8': g = D8; break;
    case '9': g = D9; break; case '-': g = DASH; break; case '/': g = SLASH; break;
    default: g = blank; break;
  }
  return g[row];
}

static int tiny_text_width(const char *text, int scale) {
  int len = 0;
  while (text && text[len]) len++;
  if (len == 0) return 0;
  return ((len * 4) - 1) * scale;
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
  GPathInfo info = {
    .num_points = count,
    .points = points
  };
  GPath *path = gpath_create(&info);
  if (!path) return;
  gpath_move_to(path, origin);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

static int chevron_percent(void) {
  return s_battery_pct;
}

static void draw_chevron(GContext *ctx, int x, int y, int w, int h, bool lit, const char *label) {
  if (!lit) return;

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
  graphics_context_set_fill_color(ctx, color_yellow());
  graphics_fill_rect(ctx, pill, 2, GCornersAll);
  draw_tiny_centered(ctx, label, pill, 1, GColorBlack);
}

static void draw_l_series(GContext *ctx) {
  const char *labels[] = {"L-01", "L-02", "L-03", "L-04", "L-05", "L-06", "L-07"};
  const int count = 7;
  const int value = chevron_percent();
  const int lit_count = (value * count + 50) / 100;
  const int x = 118;
  const int y = 23;
  const int w = 54;
  const int h = 10;
  const int gap = 2;

  for (int i = 0; i < count; i++) {
    bool lit = (count - i) <= lit_count;
    draw_chevron(ctx, x, y + i * (h + gap), w, h, lit, labels[i]);
  }

  const int sx = 194;
  const int sy = 23;
  const int sh = 82;
  GColor accent = accent_color();
  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(sx, sy), GPoint(sx, sy + sh));

  for (int v = 0; v <= 100; v += 20) {
    int ty = sy + sh - (v * sh) / 100;
    graphics_draw_line(ctx, GPoint(sx - 9, ty), GPoint(sx, ty));
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", v);
    draw_tiny_text(ctx, buf, sx - 13 - tiny_text_width(buf, 1), ty - 2, 1, accent);
  }
  for (int v = 10; v < 100; v += 20) {
    int ty = sy + sh - (v * sh) / 100;
    graphics_draw_line(ctx, GPoint(sx - 5, ty), GPoint(sx, ty));
  }
}

static void draw_heart_icon(GContext *ctx, GPoint c, int r, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(c.x - 3, c.y - 1), 3);
  graphics_fill_circle(ctx, GPoint(c.x + 3, c.y - 1), 3);
  GPoint tri[] = {GPoint(c.x - 6, c.y + 1), GPoint(c.x + 6, c.y + 1), GPoint(c.x, c.y + 6)};
  GPathInfo info = {.num_points = 3, .points = tri};
  GPath *path = gpath_create(&info);
  if (path) { gpath_move_to(path, GPoint(0, 0)); gpath_draw_filled(ctx, path); gpath_destroy(path); }
}

static void draw_bio_mon(GContext *ctx, GRect box) {
  GColor accent = accent_color();
  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, box);

  draw_tiny_centered(ctx, "BIO", GRect(box.origin.x, box.origin.y + 3, box.size.w, 7), 1, accent);

  draw_heart_icon(ctx, GPoint(box.origin.x + box.size.w / 2, box.origin.y + 21), 4, accent);

  char hr_buf[4];
  snprintf(hr_buf, sizeof(hr_buf), "%d", s_heart_rate);
  draw_text_box(ctx, hr_buf, GRect(box.origin.x, box.origin.y + 28, box.size.w, 12),
                s_font_chip_small, accent, GTextAlignmentCenter);

}

static void draw_hazard_corner(GContext *ctx, int x, int y, int s, bool tl) {
  GColor accent = accent_color();
  for (int row = 0; row < s; row++) {
    for (int col = 0; col < s; col++) {
      bool red_px = ((row + col) % 2) == (tl ? 0 : 1);
      if (red_px) {
        graphics_context_set_fill_color(ctx, accent);
      } else {
        graphics_context_set_fill_color(ctx, GColorWhite);
      }
      graphics_fill_rect(ctx, GRect(x + col, y + row, 1, 1), 0, GCornerNone);
    }
  }
}

static void draw_panel_column(GContext *ctx, GRect r, const char *label, const char *sub, GColor lc, bool has_stripe) {
  GColor accent = accent_color();
  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(r.origin.x, r.origin.y + 3), GPoint(r.origin.x + r.size.w - 1, r.origin.y + 3));

  draw_tiny_centered(ctx, label, GRect(r.origin.x, r.origin.y + 4, r.size.w, 7), 1, accent);

  if (sub) {
    draw_text_box(ctx, sub, GRect(r.origin.x + 1, r.origin.y + 12, r.size.w - 2, 12),
                  s_font_chip_small, lc, GTextAlignmentCenter);
  }

  if (has_stripe) {
    for (int row = 0; row < 4; row++) {
      for (int col = 4; col < r.size.w - 3; col++) {
        bool red_px = ((row + col) % 2) == 0;
        graphics_context_set_fill_color(ctx, red_px ? accent : GColorBlack);
        graphics_fill_rect(ctx, GRect(r.origin.x + col, r.origin.y + r.size.h - 4 + row, 1, 1), 0, GCornerNone);
      }
    }
  }
}

static void format_weather_text(char *buf, size_t buf_len) {
  if (!s_weather_valid) {
    snprintf(buf, buf_len, "--\u00B0");
    return;
  }

  int temp = s_temperature;
  if (s_temp_unit == TEMP_FAHRENHEIT) {
    temp = ((s_temperature * 9) + (s_temperature >= 0 ? 2 : -2)) / 5 + 32;
    snprintf(buf, buf_len, "%d\u00B0F", temp);
  } else {
    snprintf(buf, buf_len, "%d\u00B0C", temp);
  }
}

static void draw_bottom_panel(GContext *ctx) {
  const int px = 4, py = BOTTOM_Y, pw = 192, ph = 30;
  GColor accent = accent_color();

  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, 1);
  /* top border line only, no bottom border */
  graphics_draw_line(ctx, GPoint(px, py), GPoint(px + pw - 1, py));
  graphics_draw_line(ctx, GPoint(px, py + 1), GPoint(px + pw - 1, py + 1));

  draw_hazard_corner(ctx, px, py, 5, false);
  draw_hazard_corner(ctx, px + pw - 5, py, 5, false);
  

  int d1x = px + 72, d2x = px + 138;
  graphics_draw_line(ctx, GPoint(d1x, py + 2), GPoint(d1x, py + ph - 2));
  graphics_draw_line(ctx, GPoint(d2x, py + 2), GPoint(d2x, py + ph - 2));

  /* First column gets extra left inset: real Time2 rendering clipped DATE to ATE for one user. */
  GRect c1 = GRect(px + 6, py + 2, d1x - px - 6, ph - 4);
  GRect c2 = GRect(d1x + 2, py + 2, d2x - d1x - 2, ph - 4);
  GRect c3 = GRect(d2x + 2, py + 2, px + pw - d2x - 2, ph - 4);

  switch (s_bottom_layout) {
    case LAYOUT_DATE_BATTERY_WEATHER:
      draw_panel_column(ctx, c1, "DATE", s_date_text, color_pink(), false);
      draw_panel_column(ctx, c2, "BATT", s_battery_text, accent, false);
      {
        char weather_buf[16];
        format_weather_text(weather_buf, sizeof(weather_buf));
        draw_panel_column(ctx, c3, "WEATHER", weather_buf, color_cyan(), false);
      }
      break;
    case LAYOUT_DATE_STEPS_BATTERY:
      draw_panel_column(ctx, c1, "DATE", s_date_text, color_pink(), false);
      draw_panel_column(ctx, c2, "STEPS", s_steps_text, color_yellow(), false);
      draw_panel_column(ctx, c3, "BATT", s_battery_text, accent, false);
      break;
    case LAYOUT_DATE_STEPS_WEATHER:
    default:
      draw_panel_column(ctx, c1, "DATE", s_date_text, color_pink(), false);
      draw_panel_column(ctx, c2, "STEPS", s_steps_text, color_yellow(), false);
      {
        char weather_buf[16];
        format_weather_text(weather_buf, sizeof(weather_buf));
        draw_panel_column(ctx, c3, "WEATHER", weather_buf, color_cyan(), false);
      }
      break;
  }
}

#define SYNC_RATIO_MAX_AGE_SECS 1800

static int sync_source_percent(void) {
  switch (s_sync_source) {
    case SYNC_SOURCE_STEPS:
      return s_steps_count >= STEPS_GOAL ? 100 : (s_steps_count * 100) / STEPS_GOAL;
    case SYNC_SOURCE_BATTERY:
      return s_battery_pct;
    case SYNC_SOURCE_AESTHETIC:
      return 85;
    case SYNC_SOURCE_FRESHNESS:
    default:
      if (s_last_weather_time > 0) {
        int elapsed = (int)(time(NULL) - s_last_weather_time);
        if (elapsed < SYNC_RATIO_MAX_AGE_SECS) {
          int pct = 100 - (elapsed * 100 / SYNC_RATIO_MAX_AGE_SECS);
          if (pct > 100) return 100;
          if (pct < 0) return 0;
          return pct;
        }
      }
      return 0;
  }
}

static void draw_sync_ratio(GContext *ctx, int y) {
  GColor accent = accent_color();

  int sync_pct = sync_source_percent();

  /* System font labels: custom 1px pixel text can disappear in QEMU. */
  draw_text_box(ctx, "SYNC", GRect(6, y, 38, 14), s_font_chip_small, accent, GTextAlignmentLeft);

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
  graphics_context_set_fill_color(ctx, accent);

  for (int i = 0; i < blocks; i++) {
    int bx = bar_x + i * (block_w + gap);
    bool lit = i < lit_blocks;

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

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  /* compact corner tags, kept subordinate to the logo and metrics */
  draw_text_box(ctx, "MAGI", GRect(4, 1, 44, 12), s_font_chip_small, accent_color(), GTextAlignmentLeft);

  if (s_logo_bitmap) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_logo_bitmap, GRect(8, 12, 88, 104));
  } else {
    draw_text_box(ctx, "NERV", GRect(8, 38, 88, 28), s_font_chip, color_red(), GTextAlignmentCenter);
  }

  draw_text_box(ctx, "PWR", GRect(154, 1, 42, 12), s_font_chip_small, accent_color(), GTextAlignmentRight);

  GColor accent = accent_color();
  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(110, 17), GPoint(110, 118));

  draw_l_series(ctx);

  graphics_draw_line(ctx, GPoint(4, 118), GPoint(196, 118));

  draw_tiny_text(ctx, "LOCAL TIME // ACTIVE", 9, s_is_obstructed ? 116 : 118, 1, color_grey());
  draw_text_box(ctx, s_time_text, GRect(0, s_is_obstructed ? 120 : 123, 150, 52),
                s_font_time, GColorWhite, GTextAlignmentCenter);

  draw_bio_mon(ctx, s_is_obstructed ? GRect(152, 118, 44, 44) : GRect(152, 126, 44, 48));

  if (!s_is_obstructed) {
    draw_sync_ratio(ctx, 178);
    graphics_context_set_stroke_color(ctx, accent);
    graphics_draw_line(ctx, GPoint(4, 194), GPoint(bounds.size.w - 4, 194));
    draw_bottom_panel(ctx);
  }

}

static void update_steps(void) {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t end = time(NULL);
  HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricStepCount, start, end);
  if (mask & HealthServiceAccessibilityMaskAvailable) {
    s_steps_count = (int)health_service_sum(HealthMetricStepCount, start, end);
  } else {
    s_steps_count = 7061;
  }
#else
  s_steps_count = 7061;
#endif

  if (s_steps_count >= 1000) {
    snprintf(s_steps_text, sizeof(s_steps_text), "%d,%03d", s_steps_count / 1000, s_steps_count % 1000);
  } else {
    snprintf(s_steps_text, sizeof(s_steps_text), "%d", s_steps_count);
  }
}

static void update_time(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  bool use_24 = clock_is_24h_style();
  if (s_clock_format == CLOCK_24H) use_24 = true;
  if (s_clock_format == CLOCK_12H) use_24 = false;

  if (use_24) {
    strftime(s_time_text, sizeof(s_time_text), "%H:%M", tick_time);
  } else {
    int hour = tick_time->tm_hour % 12;
    if (hour == 0) hour = 12;
    snprintf(s_time_text, sizeof(s_time_text), "%d:%02d", hour, tick_time->tm_min);
  }

  strftime(s_date_text, sizeof(s_date_text), "%b %d", tick_time);
  for (int i = 0; s_date_text[i]; i++) {
    if (s_date_text[i] >= 'a' && s_date_text[i] <= 'z') {
      s_date_text[i] -= 32;
    }
  }
  update_steps();
}

static void update_battery_text(void) {
  snprintf(s_battery_text, sizeof(s_battery_text), "%d%%", s_battery_pct);
}

static void update_heart_rate(void) {
#if defined(PBL_HEALTH)
  HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
  if (hr > 0) s_heart_rate = (int)hr;
#endif
}

static void save_settings(void) {
  persist_write_int(SETTINGS_KEY_CLOCK_FORMAT, s_clock_format);
  persist_write_int(SETTINGS_KEY_BOTTOM_LAYOUT, s_bottom_layout);
  persist_write_int(SETTINGS_KEY_ACCENT_COLOR, s_accent_color);
  persist_write_int(SETTINGS_KEY_TEMP_UNIT, s_temp_unit);
  persist_write_int(SETTINGS_KEY_SYNC_SOURCE, s_sync_source);
}

static int clamp_setting(int value, int min, int max, int fallback) {
  return (value >= min && value <= max) ? value : fallback;
}

static void load_settings(void) {
  s_clock_format = persist_exists(SETTINGS_KEY_CLOCK_FORMAT) ? persist_read_int(SETTINGS_KEY_CLOCK_FORMAT) : CLOCK_SYSTEM;
  s_bottom_layout = persist_exists(SETTINGS_KEY_BOTTOM_LAYOUT) ? persist_read_int(SETTINGS_KEY_BOTTOM_LAYOUT) : LAYOUT_DATE_STEPS_WEATHER;
  s_accent_color = persist_exists(SETTINGS_KEY_ACCENT_COLOR) ? persist_read_int(SETTINGS_KEY_ACCENT_COLOR) : ACCENT_RED;
  s_temp_unit = persist_exists(SETTINGS_KEY_TEMP_UNIT) ? persist_read_int(SETTINGS_KEY_TEMP_UNIT) : TEMP_CELSIUS;
  s_sync_source = persist_exists(SETTINGS_KEY_SYNC_SOURCE) ? persist_read_int(SETTINGS_KEY_SYNC_SOURCE) : SYNC_SOURCE_FRESHNESS;

  s_clock_format = clamp_setting(s_clock_format, CLOCK_SYSTEM, CLOCK_12H, CLOCK_SYSTEM);
  s_bottom_layout = clamp_setting(s_bottom_layout, LAYOUT_DATE_STEPS_WEATHER, LAYOUT_DATE_STEPS_BATTERY, LAYOUT_DATE_STEPS_WEATHER);
  s_accent_color = clamp_setting(s_accent_color, ACCENT_RED, ACCENT_CYAN, ACCENT_RED);
  s_temp_unit = clamp_setting(s_temp_unit, TEMP_CELSIUS, TEMP_FAHRENHEIT, TEMP_CELSIUS);
  s_sync_source = clamp_setting(s_sync_source, SYNC_SOURCE_FRESHNESS, SYNC_SOURCE_AESTHETIC, SYNC_SOURCE_FRESHNESS);
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *clock = dict_find(iterator, MESSAGE_KEY_CLOCK_FORMAT);
  Tuple *layout = dict_find(iterator, MESSAGE_KEY_BOTTOM_LAYOUT);
  Tuple *accent = dict_find(iterator, MESSAGE_KEY_ACCENT_COLOR);
  Tuple *temp_unit = dict_find(iterator, MESSAGE_KEY_TEMP_UNIT);
  Tuple *sync_source = dict_find(iterator, MESSAGE_KEY_SYNC_SOURCE);
  Tuple *temp = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);

  bool redraw = false;

  if (clock) { s_clock_format = clamp_setting((int)clock->value->int32, CLOCK_SYSTEM, CLOCK_12H, CLOCK_SYSTEM); redraw = true; }
  if (layout) { s_bottom_layout = clamp_setting((int)layout->value->int32, LAYOUT_DATE_STEPS_WEATHER, LAYOUT_DATE_STEPS_BATTERY, LAYOUT_DATE_STEPS_WEATHER); redraw = true; }
  if (accent) { s_accent_color = clamp_setting((int)accent->value->int32, ACCENT_RED, ACCENT_CYAN, ACCENT_RED); redraw = true; }
  if (temp_unit) { s_temp_unit = clamp_setting((int)temp_unit->value->int32, TEMP_CELSIUS, TEMP_FAHRENHEIT, TEMP_CELSIUS); redraw = true; }
  if (sync_source) { s_sync_source = clamp_setting((int)sync_source->value->int32, SYNC_SOURCE_FRESHNESS, SYNC_SOURCE_AESTHETIC, SYNC_SOURCE_FRESHNESS); redraw = true; }

  if (temp && temp->type == TUPLE_INT) {
    s_temperature = (int)temp->value->int32;
    s_weather_valid = true;
    s_last_weather_time = time(NULL);
    redraw = true;
  }
  if (clock || layout || accent || temp_unit || sync_source) {
    save_settings();
  }

  if (redraw) {
    update_time();
    update_battery_text();
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
  }
}

static void battery_handler(BatteryChargeState state) {
  s_battery_pct = state.charge_percent;
  update_battery_text();
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void prv_unobstructed_will_change(GRect final_unobstructed_screen_area, void *context) {
  GRect full_bounds = layer_get_bounds(s_window_layer);
  s_is_obstructed = !grect_equal(&full_bounds, &final_unobstructed_screen_area);
}

static void prv_unobstructed_change(AnimationProgress progress, void *context) {
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void prv_unobstructed_did_change(void *context) {
  GRect full_bounds = layer_get_bounds(s_window_layer);
  GRect unobstructed = layer_get_unobstructed_bounds(s_window_layer);
  s_is_obstructed = !grect_equal(&full_bounds, &unobstructed);
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
  update_heart_rate();
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);

  if (tick_time->tm_min % 30 == 0) {
    DictionaryIterator *iter;
    AppMessageResult result = app_message_outbox_begin(&iter);
    if (result == APP_MSG_OK) {
      dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
      app_message_outbox_send();
    }
  }
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  s_window_layer = window_layer;

  s_font_time = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  s_font_chip = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_font_chip_small = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  s_logo_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_NERV_LOGO);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  s_battery_pct = battery_state_service_peek().charge_percent;
  update_battery_text();
  update_time();

  /* Check initial obstruction state and subscribe to UnobstructedArea */
  GRect full = layer_get_bounds(s_window_layer);
  GRect unobstructed = layer_get_unobstructed_bounds(s_window_layer);
  s_is_obstructed = !grect_equal(&full, &unobstructed);
  UnobstructedAreaHandlers handlers = {
    .will_change = prv_unobstructed_will_change,
    .change = prv_unobstructed_change,
    .did_change = prv_unobstructed_did_change
  };
  unobstructed_area_service_subscribe(handlers, NULL);
}

static void main_window_unload(Window *window) {
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

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });

  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(128, 128);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  window_stack_push(s_main_window, false);
}

static void deinit(void) {
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
