#include <pebble.h>

#define MAX_ROWS 24
#define GRAPH_POINTS 6
#define RED_THRESHOLD 45
#define ACTIONBAR_ICON_WIDTH 30

typedef struct {
  int hour;
  int wind10;
  int gusts;
  int wind100;
  int dir; // degrees, 0-359, 0 = north
} ForecastRow;

static ForecastRow s_rows[MAX_ROWS];
static int s_row_count = 0;
static char s_location_name[32] = "Locating...";
static char s_status_text[48] = "Fetching forecast...";
static bool s_has_data = false;

// --- Windows ---
static Window *s_main_window;
static Window *s_forecast_window;
static Window *s_graph_window;

// --- Main window widgets ---
static ActionBarLayer *s_action_bar;
static GBitmap *s_icon_graph;
static GBitmap *s_icon_list_forecast;
static GBitmap *s_icon_refresh;
static TextLayer *s_location_layer;
static TextLayer *s_lbl_10m_layer, *s_val_10m_layer;
static TextLayer *s_lbl_gusts_layer, *s_val_gusts_layer;
static TextLayer *s_lbl_100m_layer, *s_val_100m_layer;
static Layer *s_main_arrow_layer;
static TextLayer *s_main_status_layer;
static char s_val_10m_buf[16], s_val_gusts_buf[16], s_val_100m_buf[16];

// --- Forecast window widgets ---
static MenuLayer *s_menu_layer;
static TextLayer *s_forecast_status_layer;

// --- Graph window widgets ---
static Layer *s_graph_layer;
static TextLayer *s_graph_status_layer;

static GColor color_for_value(int value) {
  return value > RED_THRESHOLD ? GColorDarkCandyAppleRed : GColorBlack;
}

// ---------------------------------------------------------------------
// Data request / parsing
// ---------------------------------------------------------------------

static void request_forecast(void) {
  s_has_data = false;
  s_row_count = 0;
  strncpy(s_status_text, "Fetching forecast...", sizeof(s_status_text) - 1);

  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK && iter) {
    dict_write_int32(iter, MESSAGE_KEY_REQUEST, 1);
    app_message_outbox_send();
  }
}

static void parse_forecast(const char *data) {
  s_row_count = 0;
  static char buffer[900];
  strncpy(buffer, data, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  char *cursor = buffer;
  while (cursor && *cursor && s_row_count < MAX_ROWS) {
    char *next_row = strchr(cursor, '|');
    if (next_row) {
      *next_row = '\0';
    }

    char *p = cursor;
    char *hour_s = p;
    char *c1 = strchr(p, ',');
    if (!c1) { cursor = next_row ? next_row + 1 : NULL; continue; }
    *c1 = '\0'; p = c1 + 1;

    char *w10_s = p;
    char *c2 = strchr(p, ',');
    if (!c2) { cursor = next_row ? next_row + 1 : NULL; continue; }
    *c2 = '\0'; p = c2 + 1;

    char *gust_s = p;
    char *c3 = strchr(p, ',');
    if (!c3) { cursor = next_row ? next_row + 1 : NULL; continue; }
    *c3 = '\0'; p = c3 + 1;

    char *w100_s = p;
    char *c4 = strchr(p, ',');
    if (!c4) { cursor = next_row ? next_row + 1 : NULL; continue; }
    *c4 = '\0'; p = c4 + 1;

    char *dir_s = p;

    s_rows[s_row_count].hour    = atoi(hour_s);
    s_rows[s_row_count].wind10  = atoi(w10_s);
    s_rows[s_row_count].gusts   = atoi(gust_s);
    s_rows[s_row_count].wind100 = atoi(w100_s);
    s_rows[s_row_count].dir     = atoi(dir_s);
    s_row_count++;

    cursor = next_row ? next_row + 1 : NULL;
  }
}

static void refresh_main_window(void);

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *err_tuple  = dict_find(iter, MESSAGE_KEY_ERROR);
  Tuple *data_tuple = dict_find(iter, MESSAGE_KEY_FORECAST_DATA);
  Tuple *loc_tuple  = dict_find(iter, MESSAGE_KEY_LOCATION_NAME);

  if (loc_tuple) {
    strncpy(s_location_name, loc_tuple->value->cstring, sizeof(s_location_name) - 1);
    s_location_name[sizeof(s_location_name) - 1] = '\0';
  }

  if (err_tuple) {
    snprintf(s_status_text, sizeof(s_status_text), "Error: %s", err_tuple->value->cstring);
    s_has_data = false;
  } else if (data_tuple) {
    parse_forecast(data_tuple->value->cstring);
    s_has_data = (s_row_count > 0);
    if (!s_has_data) {
      strncpy(s_status_text, "No data received", sizeof(s_status_text) - 1);
    }
  }

  refresh_main_window();

  if (s_menu_layer) {
    text_layer_set_text(s_forecast_status_layer, s_has_data ? "" : s_status_text);
    layer_set_hidden(text_layer_get_layer(s_forecast_status_layer), s_has_data);
    layer_set_hidden(menu_layer_get_layer(s_menu_layer), !s_has_data);
    menu_layer_reload_data(s_menu_layer);
  }

  if (s_graph_layer) {
    text_layer_set_text(s_graph_status_layer, s_has_data ? "" : s_status_text);
    layer_set_hidden(text_layer_get_layer(s_graph_status_layer), s_has_data);
    layer_set_hidden(s_graph_layer, !s_has_data);
    layer_mark_dirty(s_graph_layer);
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  strncpy(s_status_text, "Message dropped", sizeof(s_status_text) - 1);
  s_has_data = false;
  refresh_main_window();
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  strncpy(s_status_text, "Send to phone failed", sizeof(s_status_text) - 1);
  s_has_data = false;
  refresh_main_window();
}

// ---------------------------------------------------------------------
// Wind direction arrow (shared helper) - 0 degrees = up = north, clockwise
// ---------------------------------------------------------------------

static const GPathInfo ARROW_PATH_INFO = {
  .num_points = 4,
  .points = (GPoint[]) {{0, -10}, {7, 8}, {0, 3}, {-7, 8}}
};

static void draw_wind_arrow(GContext *ctx, GPoint center, int direction_degrees) {
  GPath *path = gpath_create(&ARROW_PATH_INFO);
  gpath_move_to(path, center);
  int32_t angle = (direction_degrees * TRIG_MAX_ANGLE) / 360;
  gpath_rotate_to(path, angle);
  graphics_context_set_fill_color(ctx, GColorBlack);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// ---------------------------------------------------------------------
// MAIN window
// ---------------------------------------------------------------------

static void main_arrow_update_proc(Layer *layer, GContext *ctx) {
  if (!s_has_data) return;
  GRect bounds = layer_get_bounds(layer);
  draw_wind_arrow(ctx, GPoint(bounds.size.w / 2, bounds.size.h / 2), s_rows[0].dir);
}

static void refresh_main_window(void) {
  if (!s_location_layer) return; // main window not currently loaded

  text_layer_set_text(s_location_layer, s_location_name);

  if (!s_has_data) {
    text_layer_set_text(s_main_status_layer, s_status_text);
    layer_set_hidden(text_layer_get_layer(s_main_status_layer), false);
    return;
  }
  layer_set_hidden(text_layer_get_layer(s_main_status_layer), true);

  ForecastRow *now = &s_rows[0];
  snprintf(s_val_10m_buf, sizeof(s_val_10m_buf), "%d km/h", now->wind10);
  snprintf(s_val_gusts_buf, sizeof(s_val_gusts_buf), "%d km/h", now->gusts);
  snprintf(s_val_100m_buf, sizeof(s_val_100m_buf), "%d km/h", now->wind100);

  text_layer_set_text(s_val_10m_layer, s_val_10m_buf);
  text_layer_set_text(s_val_gusts_layer, s_val_gusts_buf);
  text_layer_set_text(s_val_100m_layer, s_val_100m_buf);

  text_layer_set_text_color(s_val_10m_layer, color_for_value(now->wind10));
  text_layer_set_text_color(s_val_gusts_layer, color_for_value(now->gusts));
  text_layer_set_text_color(s_val_100m_layer, color_for_value(now->wind100));

  layer_mark_dirty(s_main_arrow_layer);
}

static void main_up_click(ClickRecognizerRef recognizer, void *context) {
  window_stack_push(s_graph_window, true);
}

static void main_down_click(ClickRecognizerRef recognizer, void *context) {
  window_stack_push(s_forecast_window, true);
}

static void main_select_click(ClickRecognizerRef recognizer, void *context) {
  request_forecast();
}

static void main_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, main_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, main_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, main_select_click);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  int content_width = bounds.size.w - ACTIONBAR_ICON_WIDTH;

  s_action_bar = action_bar_layer_create();
  action_bar_layer_add_to_window(s_action_bar, window);
  action_bar_layer_set_background_color(s_action_bar, GColorLightGray);
  action_bar_layer_set_click_config_provider(s_action_bar, main_click_config_provider);

  s_icon_graph = gbitmap_create_with_resource(RESOURCE_ID_GRAPH);
  s_icon_list_forecast = gbitmap_create_with_resource(RESOURCE_ID_LIST_FORECAST);
  s_icon_refresh = gbitmap_create_with_resource(RESOURCE_ID_REFRESH);

  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_UP, s_icon_graph);
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_DOWN, s_icon_list_forecast);
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_SELECT, s_icon_refresh);

  s_location_layer = text_layer_create(GRect(4, 0, content_width - 40, 20));
  text_layer_set_font(s_location_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text(s_location_layer, s_location_name);
  layer_add_child(window_layer, text_layer_get_layer(s_location_layer));

  s_main_arrow_layer = layer_create(GRect(content_width - 40, 0, 36, 30));
  layer_set_update_proc(s_main_arrow_layer, main_arrow_update_proc);
  layer_add_child(window_layer, s_main_arrow_layer);

  s_lbl_10m_layer = text_layer_create(GRect(4, 28, content_width - 8, 18));
  text_layer_set_font(s_lbl_10m_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_lbl_10m_layer, "At 10m:");
  layer_add_child(window_layer, text_layer_get_layer(s_lbl_10m_layer));

  s_val_10m_layer = text_layer_create(GRect(4, 44, content_width - 8, 36));
  text_layer_set_font(s_val_10m_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_val_10m_layer));

  s_lbl_gusts_layer = text_layer_create(GRect(4, 84, content_width - 8, 18));
  text_layer_set_font(s_lbl_gusts_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_lbl_gusts_layer, "Gusts:");
  layer_add_child(window_layer, text_layer_get_layer(s_lbl_gusts_layer));

  s_val_gusts_layer = text_layer_create(GRect(4, 100, content_width - 8, 36));
  text_layer_set_font(s_val_gusts_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_val_gusts_layer));

  s_lbl_100m_layer = text_layer_create(GRect(4, 140, content_width - 8, 18));
  text_layer_set_font(s_lbl_100m_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_lbl_100m_layer, "At 100m:");
  layer_add_child(window_layer, text_layer_get_layer(s_lbl_100m_layer));

  s_val_100m_layer = text_layer_create(GRect(4, 156, content_width - 8, 36));
  text_layer_set_font(s_val_100m_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_val_100m_layer));

  s_main_status_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 20, content_width, 40));
  text_layer_set_text_alignment(s_main_status_layer, GTextAlignmentCenter);
  text_layer_set_font(s_main_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text(s_main_status_layer, s_status_text);
  layer_add_child(window_layer, text_layer_get_layer(s_main_status_layer));

  refresh_main_window();
}

static void main_window_unload(Window *window) {
  action_bar_layer_destroy(s_action_bar);
  gbitmap_destroy(s_icon_graph);
  gbitmap_destroy(s_icon_list_forecast);
  gbitmap_destroy(s_icon_refresh);
  text_layer_destroy(s_location_layer);
  text_layer_destroy(s_lbl_10m_layer);
  text_layer_destroy(s_val_10m_layer);
  text_layer_destroy(s_lbl_gusts_layer);
  text_layer_destroy(s_val_gusts_layer);
  text_layer_destroy(s_lbl_100m_layer);
  text_layer_destroy(s_val_100m_layer);
  text_layer_destroy(s_main_status_layer);
  layer_destroy(s_main_arrow_layer);

  s_location_layer = NULL;
  s_main_arrow_layer = NULL;
  s_main_status_layer = NULL;
}

// ---------------------------------------------------------------------
// FORECAST window (no action bar - Back button returns to main)
// ---------------------------------------------------------------------

static uint16_t forecast_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  return s_row_count;
}

static int16_t forecast_get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  return 18;
}

static void forecast_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *context) {
  GRect bounds = layer_get_bounds(cell_layer);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "        10m  Gusts 100m",
                      fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                      GRect(0, 0, bounds.size.w, bounds.size.h),
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static int16_t forecast_get_row_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  return 28;
}

static void forecast_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  if (cell_index->row >= (uint16_t)s_row_count) return;
  ForecastRow *r = &s_rows[cell_index->row];
  GRect bounds = layer_get_bounds(cell_layer);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18);

  char hour_buf[8], w10_buf[6], gust_buf[6], w100_buf[6];
  snprintf(hour_buf, sizeof(hour_buf), "%02d:00", r->hour);
  snprintf(w10_buf, sizeof(w10_buf), "%d", r->wind10);
  snprintf(gust_buf, sizeof(gust_buf), "%d", r->gusts);
  snprintf(w100_buf, sizeof(w100_buf), "%d", r->wind100);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, hour_buf, font, GRect(4, 4, 48, bounds.size.h),
                      GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  draw_wind_arrow(ctx, GPoint(62, bounds.size.h / 2), r->dir);

  graphics_context_set_text_color(ctx, color_for_value(r->wind10));
  graphics_draw_text(ctx, w10_buf, font, GRect(80, 4, 40, bounds.size.h),
                      GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, color_for_value(r->gusts));
  graphics_draw_text(ctx, gust_buf, font, GRect(122, 4, 40, bounds.size.h),
                      GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, color_for_value(r->wind100));
  graphics_draw_text(ctx, w100_buf, font, GRect(164, 4, 36, bounds.size.h),
                      GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void forecast_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = forecast_get_num_rows,
    .get_header_height = forecast_get_header_height,
    .draw_header = forecast_draw_header,
    .get_cell_height = forecast_get_row_height,
    .draw_row = forecast_draw_row,
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
  layer_set_hidden(menu_layer_get_layer(s_menu_layer), !s_has_data);

  s_forecast_status_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 20, bounds.size.w, 40));
  text_layer_set_text_alignment(s_forecast_status_layer, GTextAlignmentCenter);
  text_layer_set_font(s_forecast_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text(s_forecast_status_layer, s_has_data ? "" : s_status_text);
  layer_set_hidden(text_layer_get_layer(s_forecast_status_layer), s_has_data);
  layer_add_child(window_layer, text_layer_get_layer(s_forecast_status_layer));
}

static void forecast_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  text_layer_destroy(s_forecast_status_layer);
  s_menu_layer = NULL;
  s_forecast_status_layer = NULL;
}

// ---------------------------------------------------------------------
// GRAPH window (no action bar - Back button returns to main)
// ---------------------------------------------------------------------

#define GRAPH_Y_MAX 60
#define GRAPH_MARGIN_LEFT 26
#define GRAPH_MARGIN_BOTTOM 18
#define GRAPH_MARGIN_TOP 6

static void graph_update_proc(Layer *layer, GContext *ctx) {
  if (!s_has_data) return;
  GRect bounds = layer_get_bounds(layer);

  int plot_w = bounds.size.w - GRAPH_MARGIN_LEFT - 6;
  int plot_h = bounds.size.h - GRAPH_MARGIN_BOTTOM - GRAPH_MARGIN_TOP;
  int origin_x = GRAPH_MARGIN_LEFT;
  int origin_y = GRAPH_MARGIN_TOP + plot_h;

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_line(ctx, GPoint(origin_x, GRAPH_MARGIN_TOP), GPoint(origin_x, origin_y));
  graphics_draw_line(ctx, GPoint(origin_x, origin_y), GPoint(origin_x + plot_w, origin_y));

  int marks[] = {0, 30, 45, 60};
  for (int i = 0; i < 4; i++) {
    int y = origin_y - (marks[i] * plot_h) / GRAPH_Y_MAX;
    GColor c = (marks[i] == 45) ? GColorDarkCandyAppleRed : GColorLightGray;
    graphics_context_set_stroke_color(ctx, c);
    graphics_draw_line(ctx, GPoint(origin_x, y), GPoint(origin_x + plot_w, y));

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", marks[i]);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_09),
                        GRect(0, y - 7, GRAPH_MARGIN_LEFT - 4, 14),
                        GTextOverflowModeFill, GTextAlignmentRight, NULL);
  }

  int points = s_row_count < GRAPH_POINTS ? s_row_count : GRAPH_POINTS;
  if (points < 2) return;

  GPoint prev = GPoint(0, 0);
  for (int i = 0; i < points; i++) {
    int x = origin_x + (i * plot_w) / (points - 1);
    int val = s_rows[i].wind10;
    if (val > GRAPH_Y_MAX) val = GRAPH_Y_MAX;
    int y = origin_y - (val * plot_h) / GRAPH_Y_MAX;
    GPoint p = GPoint(x, y);

    if (i > 0) {
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, prev, p);
    }
    graphics_context_set_fill_color(ctx, color_for_value(s_rows[i].wind10));
    graphics_fill_circle(ctx, p, 3);

    char hbuf[4];
    snprintf(hbuf, sizeof(hbuf), "%02d", s_rows[i].hour);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, hbuf, fonts_get_system_font(FONT_KEY_GOTHIC_09),
                        GRect(x - 12, origin_y + 2, 24, 14),
                        GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    prev = p;
  }
}

static void graph_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_graph_layer = layer_create(bounds);
  layer_set_update_proc(s_graph_layer, graph_update_proc);
  layer_set_hidden(s_graph_layer, !s_has_data);
  layer_add_child(window_layer, s_graph_layer);

  s_graph_status_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 20, bounds.size.w, 40));
  text_layer_set_text_alignment(s_graph_status_layer, GTextAlignmentCenter);
  text_layer_set_font(s_graph_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text(s_graph_status_layer, s_has_data ? "" : s_status_text);
  layer_set_hidden(text_layer_get_layer(s_graph_status_layer), s_has_data);
  layer_add_child(window_layer, text_layer_get_layer(s_graph_status_layer));
}

static void graph_window_unload(Window *window) {
  layer_destroy(s_graph_layer);
  text_layer_destroy(s_graph_status_layer);
  s_graph_layer = NULL;
  s_graph_status_layer = NULL;
}

// ---------------------------------------------------------------------
// App init
// ---------------------------------------------------------------------

static void init(void) {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });

  s_forecast_window = window_create();
  window_set_window_handlers(s_forecast_window, (WindowHandlers) {
    .load = forecast_window_load,
    .unload = forecast_window_unload,
  });

  s_graph_window = window_create();
  window_set_window_handlers(s_graph_window, (WindowHandlers) {
    .load = graph_window_load,
    .unload = graph_window_unload,
  });

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  window_stack_push(s_main_window, true);
  request_forecast();
}

static void deinit(void) {
  window_destroy(s_main_window);
  window_destroy(s_forecast_window);
  window_destroy(s_graph_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}