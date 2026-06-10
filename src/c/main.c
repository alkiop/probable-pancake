#include <pebble.h>

// Message keys (must match appinfo.json messageKeys)
#define KEY_USE_NEAREST   10001
#define KEY_BUNDLE        10003
#define KEY_ERROR_CODE    10004
#define KEY_REQUEST_NEXT  10005
#define KEY_REQUEST_PREV  10006
#define KEY_STATION_INDEX 10007
#define KEY_STATION_COUNT 10008
#define KEY_WARNING_MSG   10009

static Window *s_main_window;
static TextLayer *s_index_layer;    // "1/4" top-left
static TextLayer *s_mode_layer;     // "BIKES" / "DOCKS" small grey caps
static TextLayer *s_station_layer;  // station name, bold white
static Layer     *s_circle_layer;   // color-coded circle + big count
static TextLayer *s_detail_layer;   // "eBike 6   Std 18" breakdown
static TextLayer *s_footer_layer;   // hint / warning

static char s_station_name[48];
static char s_index_text[12];
static char s_detail_text[32];
static char s_footer_text[48];

static int s_bikes = 0;
static int s_ebikes = 0;
static int s_docks = 0;
static bool s_show_docks = false;
static bool s_have_data = false;
static bool s_has_warning = false;

// Availability color rules: 0 = red, <3 = yellow, >=3 = green.
// On B&W watches the circle is solid white with a black number.
static void circle_colors(int n, GColor *fill, GColor *txt) {
#if defined(PBL_COLOR)
  if (n == 0)       { *fill = GColorRed;          *txt = GColorWhite; }
  else if (n < 3)   { *fill = GColorChromeYellow; *txt = GColorBlack; }
  else              { *fill = GColorIslamicGreen; *txt = GColorWhite; }
#else
  *fill = GColorWhite;
  *txt = GColorBlack;
#endif
}

static void circle_update_proc(Layer *layer, GContext *ctx) {
  if (!s_have_data) return;

  GRect bounds = layer_get_bounds(layer);
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  int radius = (bounds.size.h / 2) - 2;

  int value = s_show_docks ? s_docks : s_bikes;
  GColor fill, txt;
  circle_colors(value, &fill, &txt);

  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_circle(ctx, center, radius);

  static char numbuf[8];
  snprintf(numbuf, sizeof(numbuf), "%d", value);

  GFont font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  GSize ts = graphics_text_layout_get_content_size(
      numbuf, font, bounds, GTextOverflowModeFill, GTextAlignmentCenter);
  // Pebble text frames have top-side padding; nudge up to optically center.
  GRect text_rect = GRect(0, center.y - (ts.h / 2) - 4, bounds.size.w, ts.h + 8);

  graphics_context_set_text_color(ctx, txt);
  graphics_draw_text(ctx, numbuf, font, text_rect,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void update_view(void) {
  if (!s_have_data) return;

  if (s_show_docks) {
    text_layer_set_text(s_mode_layer, "DOCKS");
    snprintf(s_detail_text, sizeof(s_detail_text), "%d bikes here", s_bikes);
  } else {
    text_layer_set_text(s_mode_layer, "BIKES");
    snprintf(s_detail_text, sizeof(s_detail_text), "eBike %d   Std %d",
             s_ebikes, s_bikes - s_ebikes);
  }
  text_layer_set_text(s_detail_layer, s_detail_text);

  if (!s_has_warning) {
    text_layer_set_text(s_footer_layer,
                        s_show_docks ? "SELECT: bikes" : "SELECT: docks");
  }

  layer_mark_dirty(s_circle_layer);
}

static void show_error(const char *msg) {
  s_have_data = false;
  layer_mark_dirty(s_circle_layer);
  text_layer_set_text(s_mode_layer, "");
  text_layer_set_text(s_station_layer, msg);
  text_layer_set_text(s_detail_layer, "");
  text_layer_set_text(s_footer_layer, "");
}

static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  Tuple *error_t = dict_find(iter, KEY_ERROR_CODE);
  if (error_t && error_t->value->int32 != 0) {
    switch (error_t->value->int32) {
      case 1: show_error("Location failed"); break;
      case 2: show_error("No stations found"); break;
      case 3: show_error("No live data"); break;
      default: show_error("Error"); break;
    }
    return;
  }

  Tuple *bundle_t = dict_find(iter, KEY_BUNDLE);
  if (bundle_t && bundle_t->type == TUPLE_CSTRING) {
    // Bundle format: "StationName|totalBikes|eBikes|docks"
    char buf[96];
    strncpy(buf, bundle_t->value->cstring, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *p1 = strchr(buf, '|');
    if (p1) {
      *p1 = '\0';
      char *p2 = strchr(p1 + 1, '|');
      if (p2) {
        *p2 = '\0';
        char *p3 = strchr(p2 + 1, '|');
        if (p3) {
          *p3 = '\0';
          strncpy(s_station_name, buf, sizeof(s_station_name) - 1);
          s_station_name[sizeof(s_station_name) - 1] = '\0';
          s_bikes = atoi(p1 + 1);
          s_ebikes = atoi(p2 + 1);
          s_docks = atoi(p3 + 1);
          s_have_data = true;

          text_layer_set_text(s_station_layer, s_station_name);
          update_view();
        }
      }
    }
  }

  Tuple *index_t = dict_find(iter, KEY_STATION_INDEX);
  Tuple *count_t = dict_find(iter, KEY_STATION_COUNT);
  if (index_t && count_t) {
    snprintf(s_index_text, sizeof(s_index_text), "%d/%d",
             (int)index_t->value->int32, (int)count_t->value->int32);
    text_layer_set_text(s_index_layer, s_index_text);
  }

  Tuple *warning_t = dict_find(iter, KEY_WARNING_MSG);
  if (warning_t && warning_t->type == TUPLE_CSTRING && warning_t->value->cstring[0]) {
    strncpy(s_footer_text, warning_t->value->cstring, sizeof(s_footer_text) - 1);
    s_footer_text[sizeof(s_footer_text) - 1] = '\0';
    s_has_warning = true;
    text_layer_set_text(s_footer_layer, s_footer_text);
  } else if (bundle_t) {
    s_has_warning = false;
    text_layer_set_text(s_footer_layer,
                        s_show_docks ? "SELECT: bikes" : "SELECT: docks");
  }
}

static void send_key(uint32_t key) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, key, 1);
  app_message_outbox_send();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_show_docks = !s_show_docks;
  update_view();
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  send_key(KEY_REQUEST_PREV);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  send_key(KEY_REQUEST_NEXT);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static TextLayer *make_text_layer(Layer *parent, GRect frame, const char *font_key,
                                  GColor color, GTextAlignment align) {
  TextLayer *tl = text_layer_create(frame);
  text_layer_set_font(tl, fonts_get_system_font(font_key));
  text_layer_set_text_color(tl, color);
  text_layer_set_background_color(tl, GColorClear);
  text_layer_set_text_alignment(tl, align);
  layer_add_child(parent, text_layer_get_layer(tl));
  return tl;
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  int w = bounds.size.w;

  window_set_background_color(window, GColorBlack);

  GColor grey = PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite);

  // Top-left index "1/4"
  s_index_layer = make_text_layer(window_layer, GRect(4, 2, 44, 16),
                                  FONT_KEY_GOTHIC_14, grey, GTextAlignmentLeft);

  // Mode caps label "BIKES" / "DOCKS" (like the transit "UPTOWN")
  s_mode_layer = make_text_layer(window_layer, GRect(2, 2, w - 4, 16),
                                 FONT_KEY_GOTHIC_14, grey, GTextAlignmentCenter);

  // Station name (white, bold, centered, up to two lines)
  s_station_layer = make_text_layer(window_layer, GRect(2, 18, w - 4, 44),
                                    FONT_KEY_GOTHIC_18_BOLD, GColorWhite,
                                    GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_station_layer, GTextOverflowModeTrailingEllipsis);

  // Color-coded circle with big count
  s_circle_layer = layer_create(GRect(0, 62, w, 64));
  layer_set_update_proc(s_circle_layer, circle_update_proc);
  layer_add_child(window_layer, s_circle_layer);

  // Breakdown line (eBike / std, or bikes-here in docks view)
  s_detail_layer = make_text_layer(window_layer, GRect(2, 128, w - 4, 22),
                                   FONT_KEY_GOTHIC_18, GColorWhite,
                                   GTextAlignmentCenter);

  // Footer hint / warning
  s_footer_layer = make_text_layer(window_layer, GRect(2, bounds.size.h - 18, w - 4, 16),
                                   FONT_KEY_GOTHIC_14, grey, GTextAlignmentCenter);

  text_layer_set_text(s_station_layer, "Locating...");
}

static void window_unload(Window *window) {
  text_layer_destroy(s_index_layer);
  text_layer_destroy(s_mode_layer);
  text_layer_destroy(s_station_layer);
  layer_destroy(s_circle_layer);
  text_layer_destroy(s_detail_layer);
  text_layer_destroy(s_footer_layer);
}

static void init(void) {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_stack_push(s_main_window, true);

  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(256, 32);

  send_key(KEY_USE_NEAREST);
}

static void deinit(void) {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
