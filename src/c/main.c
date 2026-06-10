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

#if defined(PBL_COLOR)
#define CITIBIKE_BLUE GColorFromRGB(0, 85, 170)
#define CREAM_BG      GColorFromRGB(255, 255, 170)
#else
#define CITIBIKE_BLUE GColorBlack
#define CREAM_BG      GColorWhite
#endif

static Window *s_main_window;
static Layer *s_header_layer;
static TextLayer *s_station_layer;
static TextLayer *s_count_layer;
static TextLayer *s_detail1_layer;
static TextLayer *s_detail2_layer;
static TextLayer *s_warning_layer;
static TextLayer *s_nav_layer;
static TextLayer *s_hint_layer;

static char s_station_name[48];
static char s_count_text[20];
static char s_detail1_text[24];
static char s_detail2_text[24];
static char s_warning_text[48];
static char s_nav_text[16];

static int s_bikes = 0;
static int s_ebikes = 0;
static int s_docks = 0;
static bool s_show_docks = false;
static bool s_have_data = false;

static GColor availability_color(int n) {
#if defined(PBL_COLOR)
  if (n == 0) return GColorRed;
  if (n < 3) return GColorChromeYellow;
  return GColorIslamicGreen;
#else
  (void)n;
  return GColorBlack;
#endif
}

static void update_view(void) {
  if (!s_have_data) return;

  if (s_show_docks) {
    snprintf(s_count_text, sizeof(s_count_text), "Docks: %d", s_docks);
    text_layer_set_text_color(s_count_layer, availability_color(s_docks));
    text_layer_set_text(s_detail1_layer, "open parking spots");
    text_layer_set_text(s_detail2_layer, "");
  } else {
    snprintf(s_count_text, sizeof(s_count_text), "Bikes: %d", s_bikes);
    text_layer_set_text_color(s_count_layer, availability_color(s_bikes));
    snprintf(s_detail1_text, sizeof(s_detail1_text), "eBikes: %d", s_ebikes);
    snprintf(s_detail2_text, sizeof(s_detail2_text), "Regular: %d", s_bikes - s_ebikes);
    text_layer_set_text(s_detail1_layer, s_detail1_text);
    text_layer_set_text(s_detail2_layer, s_detail2_text);
  }
  text_layer_set_text(s_count_layer, s_count_text);
}

static void show_error(const char *msg) {
  text_layer_set_text(s_station_layer, "Citi Bike");
  text_layer_set_text(s_count_layer, "");
  text_layer_set_text(s_detail1_layer, msg);
  text_layer_set_text(s_detail2_layer, "");
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
    snprintf(s_nav_text, sizeof(s_nav_text), "%d of %d",
             (int)index_t->value->int32, (int)count_t->value->int32);
    text_layer_set_text(s_nav_layer, s_nav_text);
  }

  Tuple *warning_t = dict_find(iter, KEY_WARNING_MSG);
  if (warning_t && warning_t->type == TUPLE_CSTRING && warning_t->value->cstring[0]) {
    strncpy(s_warning_text, warning_t->value->cstring, sizeof(s_warning_text) - 1);
    s_warning_text[sizeof(s_warning_text) - 1] = '\0';
    text_layer_set_text(s_warning_layer, s_warning_text);
  } else if (bundle_t) {
    text_layer_set_text(s_warning_layer, "");
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

static void header_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, CITIBIKE_BLUE);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
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
  window_set_background_color(window, CREAM_BG);

  // Blue header bar with white station name
  s_header_layer = layer_create(GRect(0, 0, bounds.size.w, 32));
  layer_set_update_proc(s_header_layer, header_update_proc);
  layer_add_child(window_layer, s_header_layer);

  s_station_layer = make_text_layer(window_layer, GRect(2, 3, bounds.size.w - 4, 28),
                                    FONT_KEY_GOTHIC_18_BOLD, GColorWhite, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_station_layer, GTextOverflowModeTrailingEllipsis);

  s_count_layer = make_text_layer(window_layer, GRect(5, 36, bounds.size.w - 10, 30),
                                  FONT_KEY_GOTHIC_24_BOLD, CITIBIKE_BLUE, GTextAlignmentLeft);

  s_detail1_layer = make_text_layer(window_layer, GRect(5, 70, bounds.size.w - 10, 22),
                                    FONT_KEY_GOTHIC_18, GColorBlack, GTextAlignmentLeft);

  s_detail2_layer = make_text_layer(window_layer, GRect(5, 92, bounds.size.w - 10, 22),
                                    FONT_KEY_GOTHIC_18, GColorBlack, GTextAlignmentLeft);

  s_warning_layer = make_text_layer(window_layer, GRect(5, 116, bounds.size.w - 10, 18),
                                    FONT_KEY_GOTHIC_14,
                                    PBL_IF_COLOR_ELSE(GColorRed, GColorBlack),
                                    GTextAlignmentLeft);

  s_nav_layer = make_text_layer(window_layer, GRect(5, 136, bounds.size.w - 10, 22),
                                FONT_KEY_GOTHIC_18_BOLD, CITIBIKE_BLUE, GTextAlignmentCenter);

  s_hint_layer = make_text_layer(window_layer, GRect(5, bounds.size.h - 16, bounds.size.w - 10, 14),
                                 FONT_KEY_GOTHIC_14,
                                 PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack),
                                 GTextAlignmentCenter);

  text_layer_set_text(s_station_layer, "Citi Bike");
  text_layer_set_text(s_count_layer, "Locating...");
  text_layer_set_text(s_hint_layer, "UP/DN: stations  SEL: docks");
}

static void window_unload(Window *window) {
  text_layer_destroy(s_station_layer);
  text_layer_destroy(s_count_layer);
  text_layer_destroy(s_detail1_layer);
  text_layer_destroy(s_detail2_layer);
  text_layer_destroy(s_warning_layer);
  text_layer_destroy(s_nav_layer);
  text_layer_destroy(s_hint_layer);
  layer_destroy(s_header_layer);
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
