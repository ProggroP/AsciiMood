#include <pebble.h>

// ---------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------

// The mood face is the centrepiece: it owns the upper part of the screen,
// the clock sits underneath it in a narrow band.
#define FACE_ZONE_PERCENT 70

// Horizontal split of the time zone into HOUR | COLON | MINUTE columns,
// so each part can carry its own theme color.
#define HOUR_COL_PERCENT   44
#define COLON_COL_PERCENT  12
// MINUTE_COL_PERCENT is the remainder (100 - HOUR - COLON).

// The face needs a custom font: the system fonts stop at 42 pt, far too
// small for the face to read as the main element. Both sizes come from the
// same TTF and are built per platform (see the resources block in
// package.json), so each build only carries the one it uses.
#ifdef PBL_PLATFORM_EMERY
  #define FACE_FONT_RESOURCE RESOURCE_ID_FONT_FACE_72
#else
  #define FACE_FONT_RESOURCE RESOURCE_ID_FONT_FACE_90
#endif

#define TIME_FONT FONT_KEY_LECO_42_NUMBERS

// Keeps the clock out of the narrowing bottom of the round Gabbro display;
// the rectangular Emery has usable pixels all the way down.
#ifdef PBL_PLATFORM_EMERY
  #define TIME_BOTTOM_MARGIN 0
#else
  #define TIME_BOTTOM_MARGIN 14
#endif

// Both blocks are centred in their zone from the measured text height, so
// these only fine-tune what the font metrics leave over.
#define FACE_Y_ADJUST 0
#define TIME_Y_ADJUST -4

#define MOOD_COUNT 8

// Every character used here must be listed in the characterRegex of the
// face font resources in package.json, otherwise it silently renders blank.
static const char *MOOD_FACES[MOOD_COUNT] = {
  "(^_^)",   // 0 Happy
  "(;_;)",   // 1 Sad
  "(>_<)",   // 2 Angry
  "(o_O)",   // 3 Surprised
  "(^_-)",   // 4 Wink
  "(*_*)",   // 5 Starstruck
  "(-_-)",   // 6 Bored
  "(u_u)",   // 7 Sleepy
};

// ---------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------

typedef struct ClaySettings {
  GColor bg_color;
  GColor hour_color;
  GColor colon_color;
  GColor minute_color;
  GColor face_color;
  int mood;
  // Settings are persisted as a raw byte dump of this struct, so field order
  // is part of the storage format: only append here, never insert above, or
  // every already-saved configuration is read back shifted.
  int shake_enabled;
} ClaySettings;

static ClaySettings settings;

#define SETTINGS_KEY 1

static void default_settings(void) {
  settings.bg_color     = GColorBlack;
  settings.hour_color   = GColorVividCerulean;
  settings.colon_color  = GColorWhite;
  settings.minute_color = GColorGreen;
  settings.face_color    = GColorWhite;
  settings.mood          = 0;
  settings.shake_enabled = 1;
}

static void load_settings(void) {
  default_settings();
  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
  if (settings.mood < 0 || settings.mood >= MOOD_COUNT) {
    settings.mood = 0;
  }
}

static void save_settings(void) {
  persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));
}

// ---------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------

static Window *s_window;
static Layer *s_canvas;
static GFont s_face_font;

// graphics_draw_text anchors text at the top of its box, so a box that just
// spans the zone leaves the text sitting high. Measuring first and shifting
// the box down by half the leftover height centres it for any font size.
static GRect centered_box(const char *text, GFont font, GRect zone, int adjust) {
  GSize size = graphics_text_layout_get_content_size(
      text, font, zone, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);
  int y = zone.origin.y + (zone.size.h - size.h) / 2 + adjust;
  return GRect(zone.origin.x, y, zone.size.w, size.h);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, settings.bg_color);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int face_h = bounds.size.h * FACE_ZONE_PERCENT / 100;

  const char *face = MOOD_FACES[settings.mood];
  GRect face_zone = GRect(0, 0, bounds.size.w, face_h);
  GRect face_rect = centered_box(face, s_face_font, face_zone, FACE_Y_ADJUST);

  graphics_context_set_text_color(ctx, settings.face_color);
  graphics_draw_text(ctx, face, s_face_font, face_rect,
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  int hour = t->tm_hour;
  if (!clock_is_24h_style()) {
    hour = hour % 12;
    if (hour == 0) { hour = 12; }
  }

  static char hour_buf[4];
  static char min_buf[4];
  snprintf(hour_buf, sizeof(hour_buf), "%02d", hour);
  snprintf(min_buf, sizeof(min_buf), "%02d", t->tm_min);

  GFont time_font = fonts_get_system_font(TIME_FONT);

  GRect time_zone = GRect(0, face_h, bounds.size.w,
                           bounds.size.h - face_h - TIME_BOTTOM_MARGIN);
  GRect time_rect = centered_box(hour_buf, time_font, time_zone, TIME_Y_ADJUST);

  int hour_w  = bounds.size.w * HOUR_COL_PERCENT / 100;
  int colon_w = bounds.size.w * COLON_COL_PERCENT / 100;
  int min_w   = bounds.size.w - hour_w - colon_w;

  GRect hour_rect  = GRect(0, time_rect.origin.y, hour_w, time_rect.size.h);
  GRect colon_rect = GRect(hour_w, time_rect.origin.y, colon_w, time_rect.size.h);
  GRect min_rect   = GRect(hour_w + colon_w, time_rect.origin.y, min_w, time_rect.size.h);

  graphics_context_set_text_color(ctx, settings.hour_color);
  graphics_draw_text(ctx, hour_buf, time_font, hour_rect,
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  graphics_context_set_text_color(ctx, settings.colon_color);
  graphics_draw_text(ctx, ":", time_font, colon_rect,
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, settings.minute_color);
  graphics_draw_text(ctx, min_buf, time_font, min_rect,
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// ---------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  layer_mark_dirty(s_canvas);
}

// A watchface never receives touch or button events (see project notes),
// so a shake (accelerometer tap) is the closest stand-in for "tap to
// change your mood" -- it also works as a quick override on top of
// whatever mood was chosen on the Clay config page.
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  settings.mood = (settings.mood + 1) % MOOD_COUNT;
  save_settings();
  vibes_short_pulse();
  layer_mark_dirty(s_canvas);
}

// Tracked rather than just subscribing again on every config change: the
// setting arrives whenever the user saves the page, not only when it
// actually changed.
static bool s_shake_subscribed = false;

static void apply_shake_setting(void) {
  if (settings.shake_enabled && !s_shake_subscribed) {
    accel_tap_service_subscribe(accel_tap_handler);
    s_shake_subscribed = true;
  } else if (!settings.shake_enabled && s_shake_subscribed) {
    accel_tap_service_unsubscribe();
    s_shake_subscribed = false;
  }
}

static void in_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;

  t = dict_find(iter, MESSAGE_KEY_BGCOLOR);
  if (t) { settings.bg_color = GColorFromHEX(t->value->int32); }

  t = dict_find(iter, MESSAGE_KEY_HOURCOLOR);
  if (t) { settings.hour_color = GColorFromHEX(t->value->int32); }

  t = dict_find(iter, MESSAGE_KEY_MINUTECOLOR);
  if (t) { settings.minute_color = GColorFromHEX(t->value->int32); }

  t = dict_find(iter, MESSAGE_KEY_COLONCOLOR);
  if (t) { settings.colon_color = GColorFromHEX(t->value->int32); }

  t = dict_find(iter, MESSAGE_KEY_FACECOLOR);
  if (t) { settings.face_color = GColorFromHEX(t->value->int32); }

  t = dict_find(iter, MESSAGE_KEY_MOOD);
  if (t) {
    int mood = atoi(t->value->cstring);
    if (mood >= 0 && mood < MOOD_COUNT) { settings.mood = mood; }
  }

  t = dict_find(iter, MESSAGE_KEY_SHAKE);
  if (t) { settings.shake_enabled = t->value->int32; }

  save_settings();
  apply_shake_setting();
  layer_mark_dirty(s_canvas);
}

// ---------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

static void init(void) {
  load_settings();

  s_face_font = fonts_load_custom_font(resource_get_handle(FACE_FONT_RESOURCE));

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  apply_shake_setting();

  app_message_register_inbox_received(in_received_handler);
  // Clay sends every setting in one dictionary (~80 bytes at seven keys).
  // An inbox even slightly too small makes the firmware drop the whole
  // message rather than the last few values, so keep real headroom here.
  app_message_open(256, 16);
}

static void deinit(void) {
  if (s_shake_subscribed) { accel_tap_service_unsubscribe(); }
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
  fonts_unload_custom_font(s_face_font);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
