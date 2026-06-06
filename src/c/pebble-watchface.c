#include <pebble.h>

#define SCREEN_W 200
#define SCREEN_H 228
#define BOTTOM_Y 192

static Window *s_main_window;
static Layer *s_canvas_layer;
static GBitmap *s_logo_bitmap;

static GFont s_font_time;
static GFont s_font_chip;
static GFont s_font_chip_small;

static char s_time_text[8];
static char s_date_text[16];
static char s_steps_text[16];
static int s_battery_pct = 72;

static GColor nerv_red(void) { return GColorFromHEX(0xFF2424); }
static GColor nerv_yellow(void) { return GColorFromHEX(0xFFD400); }
static GColor nerv_cyan(void) { return GColorFromHEX(0x2FE6E6); }
static GColor nerv_pink(void) { return GColorFromHEX(0xFF8A9B); }
static GColor nerv_grey(void) { return GColorFromHEX(0x8C8C8C); }

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
  static const uint8_t C[5] = {7, 4, 4, 4, 7};
  static const uint8_t E[5] = {7, 4, 6, 4, 7};
  static const uint8_t I[5] = {7, 2, 2, 2, 7};
  static const uint8_t L[5] = {4, 4, 4, 4, 7};
  static const uint8_t M[5] = {5, 7, 7, 5, 5};
  static const uint8_t O[5] = {7, 5, 5, 5, 7};
  static const uint8_t P[5] = {6, 5, 6, 4, 4};
  static const uint8_t S[5] = {7, 4, 7, 1, 7};
  static const uint8_t T[5] = {7, 2, 2, 2, 2};
  static const uint8_t V[5] = {5, 5, 5, 5, 2};
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
    case 'A': g = A; break; case 'C': g = C; break; case 'E': g = E; break;
    case 'I': g = I; break; case 'L': g = L; break; case 'M': g = M; break;
    case 'O': g = O; break; case 'P': g = P; break; case 'S': g = S; break;
    case 'T': g = T; break; case 'V': g = V; break;
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
  fill_polygon_at(ctx, GPoint(x, y), chev_points, ARRAY_LENGTH(chev_points), nerv_red());

  graphics_context_set_fill_color(ctx, nerv_yellow());
  graphics_fill_rect(ctx, GRect(x + 5, y + 2, 4, 4), 0, GCornerNone);

  GRect pill = GRect(x + 14, y + 1, 28, 8);
  graphics_context_set_fill_color(ctx, nerv_yellow());
  graphics_fill_rect(ctx, pill, 2, GCornersAll);
  draw_tiny_centered(ctx, label, pill, 1, GColorBlack);
}

static void draw_l_series(GContext *ctx) {
  const char *labels[] = {"L-01", "L-02", "L-03", "L-04", "L-05", "L-06", "L-07"};
  const int count = 7;
  const int lit_count = (s_battery_pct * count + 50) / 100;
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
  graphics_context_set_stroke_color(ctx, nerv_red());
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(sx, sy), GPoint(sx, sy + sh));

  for (int v = 0; v <= 100; v += 20) {
    int ty = sy + sh - (v * sh) / 100;
    graphics_draw_line(ctx, GPoint(sx - 9, ty), GPoint(sx, ty));
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", v);
    draw_tiny_text(ctx, buf, sx - 13 - tiny_text_width(buf, 1), ty - 2, 1, nerv_red());
  }
  for (int v = 10; v < 100; v += 20) {
    int ty = sy + sh - (v * sh) / 100;
    graphics_draw_line(ctx, GPoint(sx - 5, ty), GPoint(sx, ty));
  }
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

static void draw_bottom_chips(GContext *ctx) {
  GRect date = GRect(7, BOTTOM_Y, 67, 24);
  GRect steps = GRect(78, BOTTOM_Y, 64, 24);
  GRect weather = GRect(146, BOTTOM_Y, 47, 24);

  draw_outline_chip(ctx, date, nerv_pink());
  draw_text_box(ctx, s_date_text, GRect(date.origin.x + 2, date.origin.y + 4, date.size.w - 4, 16),
                s_font_chip_small, nerv_pink(), GTextAlignmentCenter);

  draw_outline_chip(ctx, steps, nerv_yellow());
  draw_tiny_centered(ctx, "STEPS", GRect(steps.origin.x, steps.origin.y + 3, steps.size.w, 6), 1, nerv_yellow());
  draw_text_box(ctx, s_steps_text, GRect(steps.origin.x + 1, steps.origin.y + 8, steps.size.w - 2, 16),
                s_font_chip_small, nerv_yellow(), GTextAlignmentCenter);

  draw_outline_chip(ctx, weather, nerv_cyan());
  draw_sun_icon(ctx, GPoint(weather.origin.x + 14, weather.origin.y + 12), 3, nerv_cyan());
  draw_text_box(ctx, "18", GRect(weather.origin.x + 19, weather.origin.y + 4, 19, 16),
                s_font_chip, nerv_cyan(), GTextAlignmentCenter);
  graphics_context_set_stroke_color(ctx, nerv_cyan());
  graphics_draw_circle(ctx, GPoint(weather.origin.x + 38, weather.origin.y + 7), 2);
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_logo_bitmap) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_logo_bitmap, GRect(8, 12, 88, 104));
  } else {
    draw_text_box(ctx, "NERV", GRect(8, 38, 88, 28), s_font_chip, nerv_red(), GTextAlignmentCenter);
  }

  draw_l_series(ctx);

  draw_tiny_text(ctx, "LOCAL TIME // ACTIVE", 9, 120, 1, nerv_grey());
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
    int steps = (int)health_service_sum(HealthMetricStepCount, start, end);
    if (steps >= 1000) {
      snprintf(s_steps_text, sizeof(s_steps_text), "%d,%03d", steps / 1000, steps % 1000);
    } else {
      snprintf(s_steps_text, sizeof(s_steps_text), "%d", steps);
    }
    return;
  }
#endif
  snprintf(s_steps_text, sizeof(s_steps_text), "7,061");
}

static void update_time(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  strftime(s_time_text, sizeof(s_time_text), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  strftime(s_date_text, sizeof(s_date_text), "%a %d %b", tick_time);
  update_steps();
}

static void battery_handler(BatteryChargeState state) {
  s_battery_pct = state.charge_percent;
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
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  window_stack_push(s_main_window, true);
}

static void deinit(void) {
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  if (s_main_window) window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
