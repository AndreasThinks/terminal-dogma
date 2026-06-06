#include <pebble.h>

#define SCREEN_W 200
#define SCREEN_H 228
#define BOTTOM_Y 192
#define STEPS_GOAL 10000

#define SETTINGS_KEY_CLOCK_FORMAT 1001
#define SETTINGS_KEY_CHEVRON_SOURCE 1002
#define SETTINGS_KEY_BOTTOM_LAYOUT 1003
#define SETTINGS_KEY_ACCENT_COLOR 1004

#define CLOCK_SYSTEM 0
#define CLOCK_24H 1
#define CLOCK_12H 2

#define CHEVRON_BATTERY 0
#define CHEVRON_STEPS 1

#define LAYOUT_DATE_STEPS_WEATHER 0
#define LAYOUT_DATE_BATTERY_WEATHER 1
#define LAYOUT_DATE_STEPS_BATTERY 2

#define ACCENT_RED 0
#define ACCENT_AMBER 1
#define ACCENT_GREEN 2
#define ACCENT_CYAN 3

static Window *s_main_window;
static Layer *s_canvas_layer;
static GBitmap *s_logo_bitmap;

static GFont s_font_time;
static GFont s_font_chip;
static GFont s_font_chip_small;

extern uint32_t MESSAGE_KEY_CLOCK_FORMAT;
extern uint32_t MESSAGE_KEY_CHEVRON_SOURCE;
extern uint32_t MESSAGE_KEY_BOTTOM_LAYOUT;
extern uint32_t MESSAGE_KEY_ACCENT_COLOR;

static char s_time_text[8];
static char s_date_text[16];
static char s_steps_text[16];
static char s_battery_text[8];
static int s_battery_pct = 72;
static int s_steps_count = 7061;

static int s_clock_format = CLOCK_SYSTEM;
static int s_chevron_source = CHEVRON_BATTERY;
static int s_bottom_layout = LAYOUT_DATE_STEPS_WEATHER;
static int s_accent_color = ACCENT_RED;

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

static void draw_outline_chip(GContext *ctx, GRect r, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, r, 5);
}

static uint8_t glyph_row(char c, int row) {
  const uint8_t blank[5] = {0, 0, 0, 0, 0};
  const uint8_t *g = blank;
  static const uint8_t A[5] = {2, 5, 7, 5, 5};
  static const uint8_t B[5] = {6, 5, 6, 5, 6};
  static const uint8_t C[5] = {7, 4, 4, 4, 7};
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
    case 'E': g = E; break; case 'H': g = H; break; case 'I': g = I; break;
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
  for (int i = 0; text && text[i]; i++) {
    char c = text[i];
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int row = 0; row < 5; row++) {
      uint8_t bits = glyph_row(c, row);
      for (int col = 0; col < 3; col++) {
        if (bits & (1 << (2 - col))) {
          graphics_fill_rect(ctx, GRect(x + (i * 4 + col) * scale, y + row * scale, scale, scale), 0, GCornerNone);
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
  if (s_chevron_source == CHEVRON_STEPS) {
    int pct = (s_steps_count * 100) / STEPS_GOAL;
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    return pct;
  }
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

  draw_tiny_centered(ctx, s_chevron_source == CHEVRON_STEPS ? "STEPS" : "BATT",
                     GRect(x + 2, sy + sh + 4, 50, 7), 1, color_grey());
}

static void draw_sun_icon(GContext *ctx, GPoint c, int r, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_color(ctx, color);
  graphics_fill_circle(ctx, c, r);
  for (int i = 0; i < 8; i++) {
    int32_t angle = DEG_TO_TRIGANGLE(i * 45);
    int x1 = c.x + (cos_lookup(angle) * (r + 2)) / TRIG_MAX_RATIO;
    int y1 = c.y + (sin_lookup(angle) * (r + 2)) / TRIG_MAX_RATIO;
    int x2 = c.x + (cos_lookup(angle) * (r + 6)) / TRIG_MAX_RATIO;
    int y2 = c.y + (sin_lookup(angle) * (r + 6)) / TRIG_MAX_RATIO;
    graphics_draw_line(ctx, GPoint(x1, y1), GPoint(x2, y2));
  }
}

typedef enum {
  CHIP_DATE = 0,
  CHIP_STEPS = 1,
  CHIP_WEATHER = 2,
  CHIP_BATTERY = 3
} ChipKind;

static void chip_kv(GContext *ctx, GRect r, const char *label, const char *value, GColor color) {
  draw_outline_chip(ctx, r, color);
  draw_tiny_centered(ctx, label, GRect(r.origin.x, r.origin.y + 3, r.size.w, 6), 1, color);
  draw_text_box(ctx, value, GRect(r.origin.x + 1, r.origin.y + 8, r.size.w - 2, 16),
                s_font_chip_small, color, GTextAlignmentCenter);
}

static void draw_chip(GContext *ctx, GRect r, ChipKind kind) {
  switch (kind) {
    case CHIP_DATE:
      draw_outline_chip(ctx, r, color_pink());
      draw_text_box(ctx, s_date_text, GRect(r.origin.x + 2, r.origin.y + 4, r.size.w - 4, 16),
                    s_font_chip_small, color_pink(), GTextAlignmentCenter);
      break;
    case CHIP_STEPS:
      chip_kv(ctx, r, "STEPS", s_steps_text, color_yellow());
      break;
    case CHIP_BATTERY:
      chip_kv(ctx, r, "BATT", s_battery_text, accent_color());
      break;
    case CHIP_WEATHER:
    default:
      draw_outline_chip(ctx, r, color_cyan());
      draw_sun_icon(ctx, GPoint(r.origin.x + 14, r.origin.y + 12), 3, color_cyan());
      draw_text_box(ctx, "18", GRect(r.origin.x + 19, r.origin.y + 4, 19, 16),
                    s_font_chip, color_cyan(), GTextAlignmentCenter);
      graphics_context_set_stroke_color(ctx, color_cyan());
      graphics_draw_circle(ctx, GPoint(r.origin.x + 38, r.origin.y + 7), 2);
      break;
  }
}

static void draw_bottom_chips(GContext *ctx) {
  GRect left = GRect(7, BOTTOM_Y, 67, 24);
  GRect mid = GRect(78, BOTTOM_Y, 64, 24);
  GRect right = GRect(146, BOTTOM_Y, 47, 24);

  switch (s_bottom_layout) {
    case LAYOUT_DATE_BATTERY_WEATHER:
      draw_chip(ctx, left, CHIP_DATE);
      draw_chip(ctx, mid, CHIP_BATTERY);
      draw_chip(ctx, right, CHIP_WEATHER);
      break;
    case LAYOUT_DATE_STEPS_BATTERY:
      draw_chip(ctx, left, CHIP_DATE);
      draw_chip(ctx, mid, CHIP_STEPS);
      draw_chip(ctx, right, CHIP_BATTERY);
      break;
    case LAYOUT_DATE_STEPS_WEATHER:
    default:
      draw_chip(ctx, left, CHIP_DATE);
      draw_chip(ctx, mid, CHIP_STEPS);
      draw_chip(ctx, right, CHIP_WEATHER);
      break;
  }
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_logo_bitmap) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_logo_bitmap, GRect(8, 12, 88, 104));
  } else {
    draw_text_box(ctx, "NERV", GRect(8, 38, 88, 28), s_font_chip, color_red(), GTextAlignmentCenter);
  }

  draw_l_series(ctx);

  draw_tiny_text(ctx, "LOCAL TIME // ACTIVE", 9, 120, 1, color_grey());
  draw_text_box(ctx, s_time_text, GRect(0, 130, SCREEN_W, 58),
                s_font_time, GColorWhite, GTextAlignmentCenter);

  draw_bottom_chips(ctx);
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

  strftime(s_date_text, sizeof(s_date_text), "%a %d %b", tick_time);
  update_steps();
}

static void update_battery_text(void) {
  snprintf(s_battery_text, sizeof(s_battery_text), "%d%%", s_battery_pct);
}

static void save_settings(void) {
  persist_write_int(SETTINGS_KEY_CLOCK_FORMAT, s_clock_format);
  persist_write_int(SETTINGS_KEY_CHEVRON_SOURCE, s_chevron_source);
  persist_write_int(SETTINGS_KEY_BOTTOM_LAYOUT, s_bottom_layout);
  persist_write_int(SETTINGS_KEY_ACCENT_COLOR, s_accent_color);
}

static int clamp_setting(int value, int min, int max, int fallback) {
  return (value >= min && value <= max) ? value : fallback;
}

static void load_settings(void) {
  s_clock_format = persist_exists(SETTINGS_KEY_CLOCK_FORMAT) ? persist_read_int(SETTINGS_KEY_CLOCK_FORMAT) : CLOCK_SYSTEM;
  s_chevron_source = persist_exists(SETTINGS_KEY_CHEVRON_SOURCE) ? persist_read_int(SETTINGS_KEY_CHEVRON_SOURCE) : CHEVRON_BATTERY;
  s_bottom_layout = persist_exists(SETTINGS_KEY_BOTTOM_LAYOUT) ? persist_read_int(SETTINGS_KEY_BOTTOM_LAYOUT) : LAYOUT_DATE_STEPS_WEATHER;
  s_accent_color = persist_exists(SETTINGS_KEY_ACCENT_COLOR) ? persist_read_int(SETTINGS_KEY_ACCENT_COLOR) : ACCENT_RED;

  s_clock_format = clamp_setting(s_clock_format, CLOCK_SYSTEM, CLOCK_12H, CLOCK_SYSTEM);
  s_chevron_source = clamp_setting(s_chevron_source, CHEVRON_BATTERY, CHEVRON_STEPS, CHEVRON_BATTERY);
  s_bottom_layout = clamp_setting(s_bottom_layout, LAYOUT_DATE_STEPS_WEATHER, LAYOUT_DATE_STEPS_BATTERY, LAYOUT_DATE_STEPS_WEATHER);
  s_accent_color = clamp_setting(s_accent_color, ACCENT_RED, ACCENT_CYAN, ACCENT_RED);
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *clock = dict_find(iterator, MESSAGE_KEY_CLOCK_FORMAT);
  Tuple *chevrons = dict_find(iterator, MESSAGE_KEY_CHEVRON_SOURCE);
  Tuple *layout = dict_find(iterator, MESSAGE_KEY_BOTTOM_LAYOUT);
  Tuple *accent = dict_find(iterator, MESSAGE_KEY_ACCENT_COLOR);

  if (clock) s_clock_format = clamp_setting((int)clock->value->int32, CLOCK_SYSTEM, CLOCK_12H, CLOCK_SYSTEM);
  if (chevrons) s_chevron_source = clamp_setting((int)chevrons->value->int32, CHEVRON_BATTERY, CHEVRON_STEPS, CHEVRON_BATTERY);
  if (layout) s_bottom_layout = clamp_setting((int)layout->value->int32, LAYOUT_DATE_STEPS_WEATHER, LAYOUT_DATE_STEPS_BATTERY, LAYOUT_DATE_STEPS_WEATHER);
  if (accent) s_accent_color = clamp_setting((int)accent->value->int32, ACCENT_RED, ACCENT_CYAN, ACCENT_RED);

  save_settings();
  update_time();
  update_battery_text();
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void battery_handler(BatteryChargeState state) {
  s_battery_pct = state.charge_percent;
  update_battery_text();
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

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
  window_stack_push(s_main_window, true);
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
