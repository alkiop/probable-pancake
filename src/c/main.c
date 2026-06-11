#include <pebble.h>

// Message keys (must match appinfo.json messageKeys)
#define KEY_USE_NEAREST    10001
#define KEY_BUNDLE         10003
#define KEY_ERROR_CODE     10004
#define KEY_REQUEST_NEXT   10005
#define KEY_REQUEST_PREV   10006
#define KEY_STATION_INDEX  10007
#define KEY_STATION_COUNT  10008
#define KEY_WARNING_MSG    10009
#define KEY_MAP_DATA       10010
#define KEY_WEATHER_ALERT  10011
#define KEY_ROAD_DATA      10012

#define RADIUS_METERS     150
#define MAP_MAX_STATIONS  8
#define MAP_MAX_ROAD_SEGS 80

// Layout constants (portrait 200×228 Pebble Time 2 / emery)
// Row 1  y=0..22   : index + mode
// Row 2  y=22..80  : station name (~2 lines GOTHIC_18_BOLD)
// Row 3  y=80..98  : weather banner
// Gap    y=98..100 : 2 px black
// Main   y=100..204: LEFT=circle  RIGHT=eBike/Std (BIKES) or bike count (DOCKS)
// Footer y=204..228: hint / warning
#define ROW_STATION_Y   22
#define ROW_STATION_H   58
#define ROW_WEATHER_Y   80
#define ROW_MAIN_Y      100
#define ROW_FOOTER_Y    204
#define LEFT_W          102   // circle column width
#define RIGHT_X         104   // right column x-start
#define RIGHT_W         92    // right column width

typedef enum { VIEW_BIKES = 0, VIEW_DOCKS = 1, VIEW_MAP = 2 } ViewMode;

typedef struct {
  int16_t dlat;   // metres north (+) / south (-)
  int16_t dlon;   // metres east (+)  / west (-)
  uint8_t color;  // 0 red, 1 yellow, 2 green, 3 selected
} MapStation;

static Window *s_main_window;
static TextLayer *s_index_layer;        // "1/4"
static TextLayer *s_mode_layer;         // "BIKES" / "DOCKS" / "MAP"
static TextLayer *s_station_layer;      // station name
static TextLayer *s_weather_layer;      // weather / sweat banner
static Layer     *s_circle_layer;       // colour circle + big number (LEFT)
static TextLayer *s_ebike_label_layer;  // "eBike" label (right top)
static TextLayer *s_ebike_layer;        // eBike count   (right top)
static TextLayer *s_std_label_layer;    // "Std" label   (right bottom)
static TextLayer *s_std_layer;          // Std count     (right bottom)
static TextLayer *s_detail_layer;       // DOCKS: "Bikes\nN" right side
static Layer     *s_map_layer;          // full map overlay
static TextLayer *s_footer_layer;       // hint / warning

static char s_station_name[48];
static char s_index_text[12];
static char s_detail_text[32];
static char s_ebike_text[16];
static char s_std_text[16];
static char s_footer_text[48];
static char s_weather_text[32];

static int s_bikes = 0;
static int s_ebikes = 0;
static int s_docks = 0;
static ViewMode s_view_mode = VIEW_BIKES;
static bool s_have_data = false;
static bool s_has_warning = false;

typedef struct { uint8_t x1, y1, x2, y2; } RoadSeg;

static MapStation s_map_stations[MAP_MAX_STATIONS];
static int s_map_count = 0;
static RoadSeg s_road_segs[MAP_MAX_ROAD_SEGS];
static int s_road_count = 0;

// Availability colour rules: 0=red <5=yellow >=5=green.
static void circle_colors(int n, GColor *fill, GColor *txt) {
#if defined(PBL_COLOR)
  if (n == 0)      { *fill = GColorRed;          *txt = GColorWhite; }
  else if (n < 5)  { *fill = GColorChromeYellow; *txt = GColorBlack; }
  else             { *fill = GColorIslamicGreen; *txt = GColorWhite; }
#else
  *fill = GColorWhite;
  *txt  = GColorBlack;
#endif
}

static void circle_update_proc(Layer *layer, GContext *ctx) {
  if (!s_have_data || s_view_mode == VIEW_MAP) return;

  GRect bounds = layer_get_bounds(layer);
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  int radius = (bounds.size.h / 2) - 3;   // leaves ~3px margin top/bottom

  int value = (s_view_mode == VIEW_DOCKS) ? s_docks : s_bikes;
  GColor fill, txt;
  circle_colors(value, &fill, &txt);

  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_circle(ctx, center, radius);

  static char numbuf[8];
  snprintf(numbuf, sizeof(numbuf), "%d", value);

  // BITHAM_42_BOLD: large, bold number with ample space inside the circle.
  GFont font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  GSize ts = graphics_text_layout_get_content_size(
      numbuf, font, bounds, GTextOverflowModeFill, GTextAlignmentCenter);
  GRect text_rect = GRect(0, center.y - (ts.h / 2) - 2, bounds.size.w, ts.h + 4);

  graphics_context_set_text_color(ctx, txt);
  graphics_draw_text(ctx, numbuf, font, text_rect,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// Draws nearby stations as dots.  Roads underneath, user dot on top.
static void map_update_proc(Layer *layer, GContext *ctx) {
  if (s_view_mode != VIEW_MAP || !s_have_data) return;

  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int cx = bounds.size.w / 2;
  int cy = bounds.size.h / 2;
  int half = bounds.size.h / 2;
  if (half < 1) half = 1;

  // Road lines — 2px wide for visibility, light grey on colour, white on B&W.
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite));
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < s_road_count; i++) {
    graphics_draw_line(ctx,
      GPoint(s_road_segs[i].x1, s_road_segs[i].y1),
      GPoint(s_road_segs[i].x2, s_road_segs[i].y2));
  }

  for (int i = 0; i < s_map_count; i++) {
    int px = cx + (s_map_stations[i].dlon * half) / RADIUS_METERS;
    int py = cy - (s_map_stations[i].dlat * half) / RADIUS_METERS;
    if (px < 5) px = 5;
    if (px > bounds.size.w - 5) px = bounds.size.w - 5;
    if (py < 5) py = 5;
    if (py > bounds.size.h - 5) py = bounds.size.h - 5;

    GColor c;
    int r;
    uint8_t col = s_map_stations[i].color;
    if (col == 3) {
      c = GColorWhite; r = 6;
    } else {
#if defined(PBL_COLOR)
      if (col == 0)      c = GColorRed;
      else if (col == 1) c = GColorChromeYellow;
      else               c = GColorIslamicGreen;
#else
      c = GColorWhite;
#endif
      r = 4;
    }
    graphics_context_set_fill_color(ctx, c);
    graphics_fill_circle(ctx, GPoint(px, py), r);
  }

  // User: always blue (or white on B&W), on top of everything else.
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite));
  graphics_fill_circle(ctx, GPoint(cx, cy), 4);
}

static void update_view(void) {
  if (!s_have_data) return;

  switch (s_view_mode) {
    case VIEW_DOCKS:
      text_layer_set_text(s_mode_layer, "DOCKS");
      // Left circle: docks count.  Right: total bikes available.
      snprintf(s_detail_text, sizeof(s_detail_text), "Bikes\n%d", s_bikes);
      text_layer_set_text(s_detail_layer, s_detail_text);
      text_layer_set_text(s_ebike_label_layer, "");
      text_layer_set_text(s_ebike_layer, "");
      text_layer_set_text(s_std_label_layer, "");
      text_layer_set_text(s_std_layer, "");
      break;

    case VIEW_MAP:
      text_layer_set_text(s_mode_layer, "MAP");
      text_layer_set_text(s_detail_layer, "");
      text_layer_set_text(s_ebike_label_layer, "");
      text_layer_set_text(s_ebike_layer, "");
      text_layer_set_text(s_std_label_layer, "");
      text_layer_set_text(s_std_layer, "");
      break;

    case VIEW_BIKES:
    default:
      text_layer_set_text(s_mode_layer, "BIKES");
      text_layer_set_text(s_detail_layer, "");
      text_layer_set_text(s_ebike_label_layer, "eBike");
      snprintf(s_ebike_text, sizeof(s_ebike_text), "%d", s_ebikes);
      text_layer_set_text(s_ebike_layer, s_ebike_text);
      text_layer_set_text(s_std_label_layer, "Std");
      snprintf(s_std_text, sizeof(s_std_text), "%d", s_bikes - s_ebikes);
      text_layer_set_text(s_std_layer, s_std_text);
      break;
  }

  if (!s_has_warning) {
    const char *hint = "SELECT: docks";
    if (s_view_mode == VIEW_DOCKS) hint = "SELECT: map";
    else if (s_view_mode == VIEW_MAP) hint = "SELECT: bikes";
    text_layer_set_text(s_footer_layer, hint);
  }

  layer_set_hidden(s_map_layer, s_view_mode != VIEW_MAP);
  layer_mark_dirty(s_circle_layer);
  layer_mark_dirty(s_map_layer);
}

static void show_error(const char *msg) {
  s_have_data = false;
  layer_set_hidden(s_map_layer, true);
  layer_mark_dirty(s_circle_layer);
  layer_mark_dirty(s_map_layer);
  text_layer_set_text(s_mode_layer, "");
  text_layer_set_text(s_station_layer, msg);
  text_layer_set_text(s_weather_layer, "");
  text_layer_set_text(s_detail_layer, "");
  text_layer_set_text(s_ebike_label_layer, "");
  text_layer_set_text(s_ebike_layer, "");
  text_layer_set_text(s_std_label_layer, "");
  text_layer_set_text(s_std_layer, "");
  text_layer_set_text(s_footer_layer, "");
}

// Parses "dlat,dlon,color;..." into s_map_stations[].
static void parse_map_data(const char *data) {
  s_map_count = 0;
  char buf[128];
  strncpy(buf, data, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *rec = buf;
  while (rec && *rec && s_map_count < MAP_MAX_STATIONS) {
    char *next = strchr(rec, ';');
    if (next) *next = '\0';
    char *c1 = strchr(rec, ',');
    if (c1) {
      *c1 = '\0';
      char *c2 = strchr(c1 + 1, ',');
      if (c2) {
        *c2 = '\0';
        s_map_stations[s_map_count].dlat  = (int16_t)atoi(rec);
        s_map_stations[s_map_count].dlon  = (int16_t)atoi(c1 + 1);
        s_map_stations[s_map_count].color = (uint8_t)atoi(c2 + 1);
        s_map_count++;
      }
    }
    if (!next) break;
    rec = next + 1;
  }
}

// Parses "x1,y1,x2,y2;..." pixel-space road segments into s_road_segs[].
static void parse_road_data(const char *data) {
  s_road_count = 0;
  char buf[1500];
  strncpy(buf, data, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *rec = buf;
  while (rec && *rec && s_road_count < MAP_MAX_ROAD_SEGS) {
    char *next = strchr(rec, ';');
    if (next) *next = '\0';
    char *c1 = strchr(rec, ',');
    if (c1) {
      *c1 = '\0';
      char *c2 = strchr(c1 + 1, ',');
      if (c2) {
        *c2 = '\0';
        char *c3 = strchr(c2 + 1, ',');
        if (c3) {
          *c3 = '\0';
          s_road_segs[s_road_count].x1 = (uint8_t)atoi(rec);
          s_road_segs[s_road_count].y1 = (uint8_t)atoi(c1 + 1);
          s_road_segs[s_road_count].x2 = (uint8_t)atoi(c2 + 1);
          s_road_segs[s_road_count].y2 = (uint8_t)atoi(c3 + 1);
          s_road_count++;
        }
      }
    }
    if (!next) break;
    rec = next + 1;
  }
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
          s_bikes  = atoi(p1 + 1);
          s_ebikes = atoi(p2 + 1);
          s_docks  = atoi(p3 + 1);
          s_have_data = true;
          text_layer_set_text(s_station_layer, s_station_name);
          update_view();
        }
      }
    }
  }

  Tuple *map_t = dict_find(iter, KEY_MAP_DATA);
  if (map_t && map_t->type == TUPLE_CSTRING) {
    parse_map_data(map_t->value->cstring);
    layer_mark_dirty(s_map_layer);
  }

  Tuple *road_t = dict_find(iter, KEY_ROAD_DATA);
  if (road_t && road_t->type == TUPLE_CSTRING) {
    parse_road_data(road_t->value->cstring);
    layer_mark_dirty(s_map_layer);
  }

  // '!' prefix = severe (red).  '~' prefix = caution/sweat (yellow).
  Tuple *weather_t = dict_find(iter, KEY_WEATHER_ALERT);
  if (weather_t && weather_t->type == TUPLE_CSTRING) {
    const char *alert = weather_t->value->cstring;
    if (!alert[0]) {
      text_layer_set_text(s_weather_layer, "");
    } else {
      GColor color = PBL_IF_COLOR_ELSE(
        (alert[0] == '~') ? GColorChromeYellow : GColorRed,
        GColorWhite);
      text_layer_set_text_color(s_weather_layer, color);
      strncpy(s_weather_text, alert + 2, sizeof(s_weather_text) - 1);
      s_weather_text[sizeof(s_weather_text) - 1] = '\0';
      text_layer_set_text(s_weather_layer, s_weather_text);
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
    const char *hint = "SELECT: docks";
    if (s_view_mode == VIEW_DOCKS) hint = "SELECT: map";
    else if (s_view_mode == VIEW_MAP) hint = "SELECT: bikes";
    text_layer_set_text(s_footer_layer, hint);
  }
}

static void send_key(uint32_t key) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, key, 1);
  app_message_outbox_send();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_view_mode = (ViewMode)((s_view_mode + 1) % 3);
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
  window_single_click_subscribe(BUTTON_ID_UP,     up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
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
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w;   // 144
  int h = bounds.size.h;   // 168

  window_set_background_color(window, GColorBlack);

  GColor grey = PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite);

  // ── Top bar ─────────────────────────────────────────────────────────────
  // "1/4" top-left, "BIKES/DOCKS/MAP" centered in same row.
  s_index_layer = make_text_layer(root, GRect(4, 4, 54, 18),
                                  FONT_KEY_GOTHIC_14_BOLD, grey, GTextAlignmentLeft);
  s_mode_layer  = make_text_layer(root, GRect(2, 4, w - 4, 18),
                                  FONT_KEY_GOTHIC_14_BOLD, grey, GTextAlignmentCenter);

  // ── Station name ─────────────────────────────────────────────────────────
  s_station_layer = make_text_layer(root, GRect(2, ROW_STATION_Y, w - 4, ROW_STATION_H),
                                    FONT_KEY_GOTHIC_18_BOLD, GColorWhite,
                                    GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_station_layer, GTextOverflowModeTrailingEllipsis);

  // ── Weather / sweat banner ───────────────────────────────────────────────
  // Colour is set dynamically on receipt ('!' → red, '~' → yellow).
  s_weather_layer = make_text_layer(root,
                                    GRect(2, ROW_WEATHER_Y, w - 4, 18),
                                    FONT_KEY_GOTHIC_14_BOLD,
                                    PBL_IF_COLOR_ELSE(GColorRed, GColorWhite),
                                    GTextAlignmentCenter);

  // ── Main area: LEFT = colour circle, RIGHT = stats ──────────────────────
  // Circle sits in the left column; 4 px gap above row_main gives breathing
  // room between the weather banner and the circle.
  int main_h = ROW_FOOTER_Y - ROW_MAIN_Y;          // 72 px
  s_circle_layer = layer_create(GRect(2, ROW_MAIN_Y, LEFT_W - 2, main_h));
  layer_set_update_proc(s_circle_layer, circle_update_proc);
  layer_add_child(root, s_circle_layer);

  // Right column: eBike label + value (top half), Std label + value (bottom half).
  // Splitting 104 px: top half y=100..152, bottom half y=152..204.
  int mid = ROW_MAIN_Y + main_h / 2;               // 100 + 52 = 152

  // eBike label (small grey) and count (large blue).
  s_ebike_label_layer = make_text_layer(root,
                                        GRect(RIGHT_X, ROW_MAIN_Y + 4, RIGHT_W, 18),
                                        FONT_KEY_GOTHIC_14_BOLD, grey, GTextAlignmentCenter);
  s_ebike_layer = make_text_layer(root,
                                  GRect(RIGHT_X, ROW_MAIN_Y + 24, RIGHT_W, 26),
                                  FONT_KEY_GOTHIC_28_BOLD,
                                  PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite),
                                  GTextAlignmentCenter);

  // Std label (small grey) and count (white).
  s_std_label_layer = make_text_layer(root,
                                      GRect(RIGHT_X, mid + 2, RIGHT_W, 18),
                                      FONT_KEY_GOTHIC_14_BOLD, grey, GTextAlignmentCenter);
  s_std_layer = make_text_layer(root,
                                GRect(RIGHT_X, mid + 22, RIGHT_W, 26),
                                FONT_KEY_GOTHIC_28_BOLD, GColorWhite, GTextAlignmentCenter);

  // DOCKS view: bike count spans the whole right column.
  s_detail_layer = make_text_layer(root,
                                   GRect(RIGHT_X, ROW_MAIN_Y + 8, RIGHT_W, main_h - 8),
                                   FONT_KEY_GOTHIC_18_BOLD, GColorWhite,
                                   GTextAlignmentCenter);

  // ── Map overlay ───────────────────────────────────────────────────────────
  // Covers weather + main area; hidden in BIKES/DOCKS view.
  int map_y = ROW_WEATHER_Y;
  s_map_layer = layer_create(GRect(0, map_y, w, ROW_FOOTER_Y - map_y));
  layer_set_update_proc(s_map_layer, map_update_proc);
  layer_add_child(root, s_map_layer);
  layer_set_hidden(s_map_layer, true);              // shown only in VIEW_MAP

  // ── Footer ────────────────────────────────────────────────────────────────
  s_footer_layer = make_text_layer(root, GRect(2, h - 24, w - 4, 22),
                                   FONT_KEY_GOTHIC_14_BOLD, grey, GTextAlignmentCenter);

  text_layer_set_text(s_station_layer, "Locating...");
}

static void window_unload(Window *window) {
  text_layer_destroy(s_index_layer);
  text_layer_destroy(s_mode_layer);
  text_layer_destroy(s_station_layer);
  text_layer_destroy(s_weather_layer);
  layer_destroy(s_circle_layer);
  text_layer_destroy(s_ebike_label_layer);
  text_layer_destroy(s_ebike_layer);
  text_layer_destroy(s_std_label_layer);
  text_layer_destroy(s_std_layer);
  text_layer_destroy(s_detail_layer);
  layer_destroy(s_map_layer);
  text_layer_destroy(s_footer_layer);
}

static void init(void) {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_stack_push(s_main_window, true);

  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(2048, 64);

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
