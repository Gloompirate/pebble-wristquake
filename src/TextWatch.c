#include <pebble.h>
#include <ctype.h>

#include "num2words.h"

#define NUM_LINES 4
#define LINE_LENGTH 7
#define STATUS_LINE_LENGTH 10
#define STATUS_LINE_TIMEOUT 7000
#define BUFFER_SIZE (LINE_LENGTH + 2)
#define ROW_HEIGHT 37
#define TOP_MARGIN 10

#define TEXT_ALIGN_CENTER 0
#define TEXT_ALIGN_LEFT 1
#define TEXT_ALIGN_RIGHT 2

// The time it takes for a layer to slide in or out.
#define ANIMATION_DURATION 800
// Delay between the layers animations, from top to bottom
#define ANIMATION_STAGGER_TIME 150
// Delay from the start of the current layer going out until the next layer slides in
#define ANIMATION_OUT_IN_DELAY 100

#define LINE_APPEND_MARGIN 0
// We can add a new word to a line if there are at least this many characters free after
#define LINE_APPEND_LIMIT (LINE_LENGTH - LINE_APPEND_MARGIN)
  
#define MyTupletCString(_key, _cstring) ((const Tuplet) { .type = TUPLE_CSTRING, .key = _key, .cstring = { .data = _cstring, .length = strlen(_cstring) + 1 }})

static uint8_t text_style = 0;
static uint8_t text_align = TEXT_ALIGN_CENTER;
static bool bluetooth = true;
static bool bluetooth_old = true;
static uint8_t weather = 15;
static bool weather_force_update = false;
// Default on so the step count is visible out of the box on PT2 even without
// a Clay-based config UI. The upstream config page (which we don't control)
// has no show_steps checkbox; users can flip this off via persist if needed.
static bool show_steps = true;
static int current_steps = 0;
// Last HR reading from Pebble Health. 0 means no sample yet (or this
// platform has no HRM, e.g. aplite/basalt/chalk/diorite — only emery has it).
static int current_hr = 0;
// Set from window bounds at window_load; defaults match original 144x168 in case anything
// reads them before window_load runs.
static int16_t screen_w = 144;
static int16_t screen_h = 168;
static Language lang = EN_GB;

static Window *window;

typedef struct {
  TextLayer *currentLayer;
  TextLayer *nextLayer;
  char lineStr1[BUFFER_SIZE];
  char lineStr2[BUFFER_SIZE];
  PropertyAnimation *animation1;
  PropertyAnimation *animation2;
} Line;

/////////////////////////////////////////////////ZECOJ/////////////////////////////////////////////////
bool bt_connect_toggle;

typedef struct {
  TextLayer     *layer[2];
} TextLine;

typedef struct {
  // Fits "HH:MM · 999bpm · Rain 100°C" with headroom.
  char  topbar[40];
  char  bottombarL[10];
  // Icon now lives left of the text in its own BitmapLayer, so the text
  // itself is just the digit string; 8 chars covers up to "9999999".
  char  bottombarC[8];
  // Widened to 5 from upstream's 4 so "100%" fits (4 chars + NUL).
  char  bottombarR[5];
} StatusBars;

static StatusBars status_bars;

static TextLine topbar;
static TextLine bottombarL;
static TextLine bottombarC;
static TextLine bottombarR;
// Small "BT" indicator shown when bluetooth drops. Replaces the upstream's
// InverterLayer trick, which was removed from the SDK for color displays.
static TextLayer *bt_indicator;
// Bitmap icons that sit immediately left of the steps and battery values
// in the bottom row, populated from PNG resources at window_load.
static GBitmap *icon_steps_bmp = NULL;
static GBitmap *icon_battery_bmp = NULL;
static BitmapLayer *icon_steps_layer = NULL;
static BitmapLayer *icon_battery_layer = NULL;

static AppTimer *shake_timeout = NULL;

static AppSync sync;
static uint8_t sync_buffer[256];

// Empty defaults: upstream initialized these to "012345..." placeholders that
// leaked into the topbar before any weather fetch completed. Keep them empty
// until the JS fills them in; info_lines now treats empty as "not loaded".
static char weather_str[16] = "";
static char temp_c_str[8] = "";
static char temp_f_str[8] = "";
static bool temp_unit = true; // true==C false==F

#define  CONF_ALIGNMENT             0
#define  CONF_BLUETOOTH             1
#define  CONF_WEATHER               2
#define  WEATHER_ICON_KEY           3
#define  WEATHER_TEMPERATURE_C_KEY  4
#define  WEATHER_TEMPERATURE_F_KEY  5
#define  WEATHER_TEMPERATURE_UNIT   6
#define  CONF_TEXTSTYLE             7
#define  CONF_SHOWSTEPS             8
  
static void display_initial_time(struct tm *t);
static void display_time(struct tm *t);

/////////////////////////////////////////////////ZECOJ/////////////////////////////////////////////////

static Line lines[NUM_LINES];

static struct tm *t;

static uint8_t currentNLines;

char *translate_error(AppMessageResult result) {
  switch (result) {
    case APP_MSG_OK: return "APP_MSG_OK";
    case APP_MSG_SEND_TIMEOUT: return "APP_MSG_SEND_TIMEOUT";
    case APP_MSG_SEND_REJECTED: return "APP_MSG_SEND_REJECTED";
    case APP_MSG_NOT_CONNECTED: return "APP_MSG_NOT_CONNECTED";
    case APP_MSG_APP_NOT_RUNNING: return "APP_MSG_APP_NOT_RUNNING";
    case APP_MSG_INVALID_ARGS: return "APP_MSG_INVALID_ARGS";
    case APP_MSG_BUSY: return "APP_MSG_BUSY";
    case APP_MSG_BUFFER_OVERFLOW: return "APP_MSG_BUFFER_OVERFLOW";
    case APP_MSG_ALREADY_RELEASED: return "APP_MSG_ALREADY_RELEASED";
    case APP_MSG_CALLBACK_ALREADY_REGISTERED: return "APP_MSG_CALLBACK_ALREADY_REGISTERED";
    case APP_MSG_CALLBACK_NOT_REGISTERED: return "APP_MSG_CALLBACK_NOT_REGISTERED";
    case APP_MSG_OUT_OF_MEMORY: return "APP_MSG_OUT_OF_MEMORY";
    case APP_MSG_CLOSED: return "APP_MSG_CLOSED";
    case APP_MSG_INTERNAL_ERROR: return "APP_MSG_INTERNAL_ERROR";
    default: return "UNKNOWN ERROR";
  }
}

static GTextAlignment lookup_text_alignment(int align_key)
{
GTextAlignment alignment;
  switch (align_key)
  {
    case TEXT_ALIGN_LEFT:
      alignment = GTextAlignmentLeft;
      break;
    case TEXT_ALIGN_RIGHT:
      alignment = GTextAlignmentRight;
      break;
    default:
      alignment = GTextAlignmentCenter;
      break;
  }
  return alignment;
}

/////////////////////////////////////////////////ZECOJ/////////////////////////////////////////////////
void info_lines(char *load_status) {
  BatteryChargeState charge_state = battery_state_service_peek();

  // Build the topbar incrementally: time + (HR) + (weather). Each section is
  // appended only when its data exists, so an unconfigured face just shows
  // the time. Use an intermediate to avoid the snprintf(buf,…,"%s",buf,…)
  // overlap-UB the upstream had.
  char hhmm[8];
  strftime(hhmm, sizeof(hhmm), "%H:%M", t);

  char buf[sizeof(status_bars.topbar)];
  size_t pos = 0;
  pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", hhmm);

#if defined(PBL_HEALTH)
  if (current_hr > 0) {
    pos += snprintf(buf + pos, sizeof(buf) - pos, " • %dbpm", current_hr);
  }
#endif

  const bool have_weather = weather && weather_str[0] != '\0' && strcmp(weather_str, "no data") != 0;
  if (have_weather) {
    if (!strcmp(load_status, "")) {
      const char *temp = temp_unit ? temp_c_str : temp_f_str;
      pos += snprintf(buf + pos, sizeof(buf) - pos, " • %s %s", weather_str, temp);
    } else {
      pos += snprintf(buf + pos, sizeof(buf) - pos, " • %s", load_status);
    }
  }
  snprintf(status_bars.topbar, sizeof(status_bars.topbar), "%s", buf);

  strcpy(status_bars.bottombarL, "");
  strftime(status_bars.bottombarL, sizeof(status_bars.bottombarL), "%a %e", t);
  snprintf(status_bars.bottombarR, sizeof(status_bars.bottombarR), "%d%%", charge_state.charge_percent);

  // Center slot: step count. The steps icon sits left of this text in its
  // own BitmapLayer, so we just emit the number.
  status_bars.bottombarC[0] = '\0';
#if defined(PBL_HEALTH)
  if (show_steps) {
    snprintf(status_bars.bottombarC, sizeof(status_bars.bottombarC), "%d", current_steps);
  }
#endif

  status_bars.bottombarL[0]=tolower((unsigned char)status_bars.bottombarL[0]);

  text_layer_set_text(topbar.layer[0], status_bars.topbar);
  text_layer_set_text(bottombarL.layer[0], status_bars.bottombarL);
  text_layer_set_text(bottombarC.layer[0], status_bars.bottombarC);
  text_layer_set_text(bottombarR.layer[0], status_bars.bottombarR);
}

void hide_bars () {
  layer_set_hidden(text_layer_get_layer(topbar.layer[0]), true);
  layer_set_hidden(text_layer_get_layer(bottombarL.layer[0]), true);
  layer_set_hidden(text_layer_get_layer(bottombarC.layer[0]), true);
  layer_set_hidden(text_layer_get_layer(bottombarR.layer[0]), true);
  if (icon_steps_layer)   layer_set_hidden(bitmap_layer_get_layer(icon_steps_layer), true);
  if (icon_battery_layer) layer_set_hidden(bitmap_layer_get_layer(icon_battery_layer), true);
  weather_force_update = false;
  shake_timeout = NULL;
}
void show_bars ()  {
  info_lines("");
  layer_set_hidden(text_layer_get_layer(topbar.layer[0]), false);
  layer_set_hidden(text_layer_get_layer(bottombarL.layer[0]), false);
  layer_set_hidden(text_layer_get_layer(bottombarC.layer[0]), false);
  layer_set_hidden(text_layer_get_layer(bottombarR.layer[0]), false);
  // The steps icon only makes sense when the steps text is populated.
  const bool steps_visible = (status_bars.bottombarC[0] != '\0');
  if (icon_steps_layer)   layer_set_hidden(bitmap_layer_get_layer(icon_steps_layer), !steps_visible);
  if (icon_battery_layer) layer_set_hidden(bitmap_layer_get_layer(icon_battery_layer), false);
}
void wrist_flick_handler(AccelAxisType axis, int32_t direction) {
  // Accept any axis so wrist-flick (Y), screen-tap (Z), and side-tap (X)
  // all reveal the overlay. Upstream only listened on Y.
  if (!shake_timeout) {
    show_bars();
    shake_timeout = app_timer_register(STATUS_LINE_TIMEOUT, hide_bars, NULL);
  }
  else if (!weather_force_update) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "second tap -- forcing weather refresh");
    app_message_outbox_send();
    info_lines("loading...");
    weather_force_update = true;
    app_timer_reschedule(shake_timeout, STATUS_LINE_TIMEOUT);
  }
}

void bluetooth_connection_handler(bool connected) {
  if(bluetooth){
    if (!bt_connect_toggle && connected) {
      bt_connect_toggle = true;
      vibes_short_pulse();
    }
    if (bt_connect_toggle && !connected) {
      bt_connect_toggle = false;
      vibes_short_pulse();
    }
    // bt_connect_toggle: true = connected (hide indicator), false = disconnected (show)
    layer_set_hidden(text_layer_get_layer(bt_indicator), bt_connect_toggle);
  }
  else {
    layer_set_hidden(text_layer_get_layer(bt_indicator), true);
  }
}

static void sync_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void *context) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Sync Error: %i - %s", app_message_error, translate_error(app_message_error));
    //strcpy(weather_str, "no data");
    //strcpy(temp_c_str, "01234");
}

static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
  GTextAlignment alignment;
  time_t raw_time;

  // process the first and subsequent update
  switch (key) {
    case CONF_TEXTSTYLE:
      if (new_tuple->value->uint8 != persist_read_int(CONF_TEXTSTYLE)) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "CONF_TEXTSTYLE: I'm called");
        text_style = new_tuple->value->uint8;
        persist_write_int(CONF_TEXTSTYLE, text_style);
        time(&raw_time);
        t = localtime(&raw_time);
        display_time(t);
      }
      break;

    case CONF_ALIGNMENT:
      if (new_tuple->value->uint8 != persist_read_int(CONF_ALIGNMENT)) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "CONF_ALIGNMENT: I'm called");
        text_align = new_tuple->value->uint8;
        persist_write_int(CONF_ALIGNMENT, text_align);
        alignment = lookup_text_alignment(text_align);
        for (int i = 0; i < NUM_LINES; i++)
        {
          text_layer_set_text_alignment(lines[i].currentLayer, alignment);
          text_layer_set_text_alignment(lines[i].nextLayer, alignment);
          layer_mark_dirty(text_layer_get_layer(lines[i].currentLayer));
          layer_mark_dirty(text_layer_get_layer(lines[i].nextLayer));
        }
      }
      break;

    case CONF_BLUETOOTH:
      if (new_tuple->value->uint8 != persist_read_int(CONF_BLUETOOTH)) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "CONF_BLUETOOTH: I'm called");
        bluetooth_old = bluetooth;
        bluetooth = new_tuple->value->uint8 == 1;
        persist_write_bool(CONF_BLUETOOTH, bluetooth);
        if (bluetooth && !bluetooth_old) {
          bluetooth_connection_service_subscribe(bluetooth_connection_handler);
          bt_connect_toggle = bluetooth_connection_service_peek();
          layer_set_hidden(text_layer_get_layer(bt_indicator), bt_connect_toggle);
        }
        if (!bluetooth && bluetooth_old) {
          //APP_LOG(APP_LOG_LEVEL_DEBUG, "unsubscribing");
          bluetooth_connection_service_unsubscribe();
        }
        //APP_LOG(APP_LOG_LEVEL_DEBUG, "Set bluetooth: %u", bluetooth ? 1 : 0);
      }
      break;

    case CONF_WEATHER:
      if (new_tuple->value->uint8 != persist_read_int(CONF_WEATHER)) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "CONF_WEATHER: I'm called");
        weather = new_tuple->value->uint8;
        persist_write_int(CONF_WEATHER, weather);
        //APP_LOG(APP_LOG_LEVEL_DEBUG, "Set weather: %u", weather);
      }
      break;

    case WEATHER_ICON_KEY:
      //if (strcmp(new_tuple->value->cstring, persist_read_string(WEATHER_ICON_KEY))) {
        strcpy(weather_str, new_tuple->value->cstring);
        persist_write_string(WEATHER_ICON_KEY, weather_str);
        // make all lowercase
        weather_str[0] = tolower((unsigned char)weather_str[0]);
        APP_LOG(APP_LOG_LEVEL_DEBUG, "CONF_ICON_KEY: I'm called");
      //}
      break;

    case WEATHER_TEMPERATURE_C_KEY:
      //if (strcmp(new_tuple->value->cstring, persist_read_string(WEATHER_TEMPERATURE_C_KEY))) {
        strcpy(temp_c_str, new_tuple->value->cstring);
        persist_write_string(WEATHER_TEMPERATURE_C_KEY, temp_c_str);
        if(weather_force_update) {
          info_lines("");
        }
        APP_LOG(APP_LOG_LEVEL_DEBUG, "CONF_TEMPERATURE_C_KEY: I'm called %s", temp_c_str);
      //}
      break;

    case WEATHER_TEMPERATURE_F_KEY:
      //if (strcmp(new_tuple->value->cstring, persist_read_string(WEATHER_TEMPERATURE_F_KEY))) {
        strcpy(temp_f_str, new_tuple->value->cstring);
        persist_write_string(WEATHER_TEMPERATURE_F_KEY, temp_f_str);
        if(weather_force_update) {
          info_lines("");
        }
        APP_LOG(APP_LOG_LEVEL_DEBUG, "CONF_TEMPERATURE_F_KEY: I'm called %s", temp_f_str);
      //}
      break;

    case WEATHER_TEMPERATURE_UNIT:
      if (new_tuple->value->uint8 != persist_read_int(WEATHER_TEMPERATURE_UNIT)) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "WEATHER_TEMPERATURE_UNIT: I'm called");
        temp_unit = new_tuple->value->uint8 == 1;
        persist_write_bool(WEATHER_TEMPERATURE_UNIT,temp_unit);
      }
      break;

    case CONF_SHOWSTEPS:
      if ((new_tuple->value->uint8 == 1) != show_steps) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "CONF_SHOWSTEPS: I'm called");
        show_steps = new_tuple->value->uint8 == 1;
        persist_write_bool(CONF_SHOWSTEPS, show_steps);
      }
      break;
    }
}

/////////////////////////////////////////////////ZECOJ/////////////////////////////////////////////////



// Animation handler
static void animationStoppedHandler(struct Animation *animation, bool finished, void *context)
{
  TextLayer *current = (TextLayer *)context;
  GRect rect = layer_get_frame((Layer *)current);
  rect.origin.x = screen_w;
  layer_set_frame((Layer *)current, rect);
}

// Animate line
static void makeAnimationsForLayer(Line *line, int delay)
{
  TextLayer *current = line->currentLayer;
  TextLayer *next = line->nextLayer;

  // Destroy old animations
  if (line->animation1 != NULL)
  {
     property_animation_destroy(line->animation1);
  }
  if (line->animation2 != NULL)
  {
     property_animation_destroy(line->animation2);
  }

  // Configure animation for current layer to move out. PropertyAnimation is
  // opaque in SDK 3+, so use property_animation_get_animation to get an
  // Animation* instead of reaching into the struct.
  GRect rect = layer_get_frame((Layer *)current);
  rect.origin.x = -screen_w;
  line->animation1 = property_animation_create_layer_frame((Layer *)current, NULL, &rect);
  Animation *anim1 = property_animation_get_animation(line->animation1);
  animation_set_duration(anim1, ANIMATION_DURATION);
  animation_set_delay(anim1, delay);
  animation_set_curve(anim1, AnimationCurveEaseIn); // Accelerate

  // Configure animation for next layer to move in
  GRect rect2 = layer_get_frame((Layer *)next);
  rect2.origin.x = 0;
  line->animation2 = property_animation_create_layer_frame((Layer *)next, NULL, &rect2);
  Animation *anim2 = property_animation_get_animation(line->animation2);
  animation_set_duration(anim2, ANIMATION_DURATION);
  animation_set_delay(anim2, delay + ANIMATION_OUT_IN_DELAY);
  animation_set_curve(anim2, AnimationCurveEaseOut); // Deaccelerate

  // Set a handler to rearrange layers after animation is finished
  animation_set_handlers(anim2, (AnimationHandlers) {
    .stopped = (AnimationStoppedHandler)animationStoppedHandler
  }, current);

  // Start the animations
  animation_schedule(anim1);
  animation_schedule(anim2);
}

static void updateLayerText(TextLayer* layer, char* text)
{
  const char* layerText = text_layer_get_text(layer);
  strcpy((char*)layerText, text);
  // To mark layer dirty
  text_layer_set_text(layer, layerText);
    //layer_mark_dirty(&layer->layer);
}

// Update line
static void updateLineTo(Line *line, char *value, int delay)
{
  updateLayerText(line->nextLayer, value);
  makeAnimationsForLayer(line, delay);

  // Swap current/next layers
  TextLayer *tmp = line->nextLayer;
  line->nextLayer = line->currentLayer;
  line->currentLayer = tmp;
}

// Check to see if the current line needs to be updated
static bool needToUpdateLine(Line *line, char *nextValue)
{
  const char *currentStr = text_layer_get_text(line->currentLayer);

  if (strcmp(currentStr, nextValue) != 0) {
    return true;
  }
  return false;
}

// Configure bold line of text
static void configureBoldLayer(TextLayer *textlayer)
{
  text_layer_set_font(textlayer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_color(textlayer, GColorWhite);
  text_layer_set_background_color(textlayer, GColorClear);
  text_layer_set_text_alignment(textlayer, lookup_text_alignment(text_align));
}

// Configure light line of text
static void configureLightLayer(TextLayer *textlayer)
{
  text_layer_set_font(textlayer, fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT));
  text_layer_set_text_color(textlayer, GColorWhite);
  text_layer_set_background_color(textlayer, GColorClear);
  text_layer_set_text_alignment(textlayer, lookup_text_alignment(text_align));
}

// Configure the layers for the given text
static int configureLayersForText(char text[NUM_LINES][BUFFER_SIZE], char format[])
{
  int numLines = 0;

  // Set bold layer.
  int i;
  for (i = 0; i < NUM_LINES; i++) {
    if (strlen(text[i]) > 0) {
      if (format[i] == 'b')
      {
        configureBoldLayer(lines[i].nextLayer);
      }
      else
      {
        configureLightLayer(lines[i].nextLayer);
      }
    }
    else
    {
      break;
    }
  }
  numLines = i;

  // Calculate y position of top Line
  int ypos = (screen_h - numLines * ROW_HEIGHT) / 2 - TOP_MARGIN;

  // Set y positions for the lines. Lines start off-screen to the right (x = screen_w);
  // the slide-in animation moves them to x = 0.
  for (int i = 0; i < numLines; i++)
  {
    layer_set_frame((Layer *)lines[i].nextLayer, GRect(screen_w, ypos, screen_w, 50));
    ypos += ROW_HEIGHT;
  }

  return numLines;
}

static void time_to_lines(int hours, int minutes, int seconds, char lines[NUM_LINES][BUFFER_SIZE], char format[])
{
  int length = NUM_LINES * BUFFER_SIZE + 1;
  char timeStr[length];
  if (text_style == 0) {
    time_to_words_0(lang, hours, minutes, seconds, timeStr, length);
  }
  else if (text_style == 1) {
    time_to_words_1(lang, hours, minutes, seconds, timeStr, length);
  }
  else if (text_style == 2) {
    time_to_words_2(lang, hours, minutes, seconds, timeStr, length);
  }
  //APP_LOG(APP_LOG_LEVEL_DEBUG, "HERE SIR:  !%s!", timeStr);
  
  // Empty all lines
  for (int i = 0; i < NUM_LINES; i++)
  {
    lines[i][0] = '\0';
  }

  char *start = timeStr;
  char *end = strstr(start, " ");
  int l = 0;
  while (end != NULL && l < NUM_LINES) {
    // Check word for bold prefix
    if (*start == '*' && end - start > 1)
    {
      // Mark line bold and move start to the first character of the word
      format[l] = 'b';
      start++;
    }
    else
    {
      // Mark line normal
      format[l] = ' ';
    }

    // Can we add another word to the line?
    if (format[l] == ' ' && *(end + 1) != '*'    // are both lines formatted normal?
      && end - start < LINE_APPEND_LIMIT - 1)  // is the first word is short enough?
    {
      // See if next word fits
      char *try = strstr(end + 1, " ");
      if (try != NULL && try - start <= LINE_APPEND_LIMIT)
      {
        end = try;
      }
    }

    // copy to line
    *end = '\0';
    strcpy(lines[l++], start);

    // Look for next word
    start = end + 1;
    end = strstr(start, " ");
  }
  
}

// Update screen based on new time
static void display_time(struct tm *t)
{
  // The current time text will be stored in the following strings
  char textLine[NUM_LINES][BUFFER_SIZE];
  char format[NUM_LINES];

  time_to_lines(t->tm_hour, t->tm_min, t->tm_sec, textLine, format);
  
  int nextNLines = configureLayersForText(textLine, format);

  int delay = 0;
  for (int i = 0; i < NUM_LINES; i++) {
    if (nextNLines != currentNLines || needToUpdateLine(&lines[i], textLine[i])) {
      updateLineTo(&lines[i], textLine[i], delay);
      delay += ANIMATION_STAGGER_TIME;
    }
  }
  
  currentNLines = nextNLines;
}

static void initLineForStart(Line* line)
{
  // Switch current and next layer
  TextLayer* tmp  = line->currentLayer;
  line->currentLayer = line->nextLayer;
  line->nextLayer = tmp;

  // Move current layer to screen;
  GRect rect = layer_get_frame((Layer *)line->currentLayer);
  rect.origin.x = 0;
  layer_set_frame((Layer *)line->currentLayer, rect);
}

// Update screen without animation first time we start the watchface
static void display_initial_time(struct tm *t)
{
  // The current time text will be stored in the following strings
  char textLine[NUM_LINES][BUFFER_SIZE];
  char format[NUM_LINES];

  time_to_lines(t->tm_hour, t->tm_min, t->tm_sec, textLine, format);

  // This configures the nextLayer for each line
  currentNLines = configureLayersForText(textLine, format);

  // Set the text and configure layers to the start position
  for (int i = 0; i < currentNLines; i++)
  {
    updateLayerText(lines[i].nextLayer, textLine[i]);
    // This call switches current- and nextLayer
    initLineForStart(&lines[i]);
  }  
}

// Time handler called every minute by the system
static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed)
{
  //t = tick_time;
  display_time(tick_time);
  //info_lines();
  //APP_LOG(APP_LOG_LEVEL_DEBUG, "MINUTE_TICK I'm seeing weather: %u", weather);
  if(weather && tick_time->tm_min % weather == 0) {
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "MINUTE_TICK I'm supposed to check the weather now");
    app_message_outbox_send();
  }
}

static void init_line(Line* line)
{
  // Create layers with dummy position to the right of the screen
  line->currentLayer = text_layer_create(GRect(screen_w, 0, screen_w, 50));
  line->nextLayer = text_layer_create(GRect(screen_w, 0, screen_w, 50));

  // Configure a style
  configureLightLayer(line->currentLayer);
  configureLightLayer(line->nextLayer);

  // Set the text buffers
  line->lineStr1[0] = '\0';
  line->lineStr2[0] = '\0';
  text_layer_set_text(line->currentLayer, line->lineStr1);
  text_layer_set_text(line->nextLayer, line->lineStr2);

  // Initially there are no animations
  line->animation1 = NULL;
  line->animation2 = NULL;
}

static void destroy_line(Line* line)
{
  // Free layers
  text_layer_destroy(line->currentLayer);
  text_layer_destroy(line->nextLayer);
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context)
{
  if (event == HealthEventSignificantUpdate || event == HealthEventMovementUpdate) {
    current_steps = (int)health_service_sum_today(HealthMetricStepCount);
  }
  if (event == HealthEventSignificantUpdate || event == HealthEventHeartRateUpdate) {
    // peek_current_value returns the last sample the OS recorded — exactly
    // the "last reading" UX we want. Returns 0 if the platform has no HRM.
    HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
    if (hr > 0) current_hr = (int)hr;
  }
}
#endif

static void window_load(Window *window)
{
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  screen_w = bounds.size.w;
  screen_h = bounds.size.h;

  // Init and load lines
  for (int i = 0; i < NUM_LINES; i++)
  {
    init_line(&lines[i]);
    layer_add_child(window_layer, (Layer *)lines[i].currentLayer);
    layer_add_child(window_layer, (Layer *)lines[i].nextLayer);
  }
  
  /////////////////////////////////////////////////ZECOJ/////////////////////////////////////////////////
  // Status bars: pinned to top edge (topbar) and bottom edge (bottombarL/C/R)
  // of whatever screen we're on. Bar geometry and font scale up on emery
  // (200x228) — the 144x168 sizing was unreadably small there.
  const bool big = (screen_h >= 200);
  const char *bar_font_key = big ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14;
  GFont bar_font = fonts_get_system_font(bar_font_key);
  const int16_t BAR_H = big ? 22 : 15;
  const int16_t BAR_TOP_H = big ? 22 : 16;
  const int16_t BAR_TOP_Y = big ? 0 : -4;
  const int16_t L_W = big ? 60 : 45;
  // R widened from the icon-less days so "100%" fits to the right of the
  // battery glyph; same for the steps slot dimensions below.
  const int16_t R_W = big ? 60 : 48;
  const int16_t BT_W = big ? 22 : 14;
  // 12x12 icons, vertically centered inside the bar with a 2-px left pad
  // and a 2-px gap before their text.
  const int16_t ICON_SZ = 12;
  const int16_t ICON_GAP = 2;
  const int16_t ICON_PAD = 2;
  const int16_t ICON_X_OFF = ICON_PAD + ICON_SZ + ICON_GAP;  // text x-offset within each slot
  const int16_t ICON_Y = screen_h - BAR_H + (BAR_H - ICON_SZ) / 2;
  const int16_t C_X = L_W;
  const int16_t C_W = screen_w - L_W - R_W;
  const int16_t R_X = screen_w - R_W;

  topbar.layer[0] = text_layer_create(GRect(0, BAR_TOP_Y, screen_w, BAR_TOP_H));
  text_layer_set_text_color(topbar.layer[0], GColorWhite);
  text_layer_set_background_color(topbar.layer[0], GColorBlack);
  text_layer_set_font(topbar.layer[0], bar_font);
  text_layer_set_text_alignment(topbar.layer[0], GTextAlignmentCenter);

  bottombarL.layer[0] = text_layer_create(GRect(0, screen_h - BAR_H, L_W, BAR_H));
  text_layer_set_text_color(bottombarL.layer[0], GColorWhite);
  text_layer_set_background_color(bottombarL.layer[0], GColorBlack);
  text_layer_set_font(bottombarL.layer[0], bar_font);
  text_layer_set_text_alignment(bottombarL.layer[0], GTextAlignmentLeft);

  // Steps slot: icon at C_X+ICON_PAD, text starts after the icon.
  bottombarC.layer[0] = text_layer_create(GRect(C_X + ICON_X_OFF, screen_h - BAR_H, C_W - ICON_X_OFF, BAR_H));
  text_layer_set_text_color(bottombarC.layer[0], GColorWhite);
  text_layer_set_background_color(bottombarC.layer[0], GColorBlack);
  text_layer_set_font(bottombarC.layer[0], bar_font);
  text_layer_set_text_alignment(bottombarC.layer[0], GTextAlignmentLeft);

  // Battery slot: icon at R_X+ICON_PAD, text starts after the icon.
  bottombarR.layer[0] = text_layer_create(GRect(R_X + ICON_X_OFF, screen_h - BAR_H, R_W - ICON_X_OFF, BAR_H));
  text_layer_set_text_color(bottombarR.layer[0], GColorWhite);
  text_layer_set_background_color(bottombarR.layer[0], GColorBlack);
  text_layer_set_font(bottombarR.layer[0], bar_font);
  text_layer_set_text_alignment(bottombarR.layer[0], GTextAlignmentLeft);

  // Bitmap icons for the steps and battery values.
  icon_steps_bmp   = gbitmap_create_with_resource(RESOURCE_ID_IMG_STEPS);
  icon_battery_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMG_BATTERY);
  icon_steps_layer   = bitmap_layer_create(GRect(C_X + ICON_PAD, ICON_Y, ICON_SZ, ICON_SZ));
  icon_battery_layer = bitmap_layer_create(GRect(R_X + ICON_PAD, ICON_Y, ICON_SZ, ICON_SZ));
  bitmap_layer_set_bitmap(icon_steps_layer,   icon_steps_bmp);
  bitmap_layer_set_bitmap(icon_battery_layer, icon_battery_bmp);
  // GCompOpSet honors the PNG alpha so the transparent-background icons
  // show only their shape rather than a black square.
  bitmap_layer_set_compositing_mode(icon_steps_layer,   GCompOpSet);
  bitmap_layer_set_compositing_mode(icon_battery_layer, GCompOpSet);

  layer_add_child(window_layer, text_layer_get_layer(topbar.layer[0]));
  layer_add_child(window_layer, text_layer_get_layer(bottombarL.layer[0]));
  layer_add_child(window_layer, text_layer_get_layer(bottombarC.layer[0]));
  layer_add_child(window_layer, text_layer_get_layer(bottombarR.layer[0]));
  layer_add_child(window_layer, bitmap_layer_get_layer(icon_steps_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(icon_battery_layer));

  layer_set_hidden(text_layer_get_layer(topbar.layer[0]), true);
  layer_set_hidden(text_layer_get_layer(bottombarL.layer[0]), true);
  layer_set_hidden(text_layer_get_layer(bottombarC.layer[0]), true);
  layer_set_hidden(text_layer_get_layer(bottombarR.layer[0]), true);
  layer_set_hidden(bitmap_layer_get_layer(icon_steps_layer), true);
  layer_set_hidden(bitmap_layer_get_layer(icon_battery_layer), true);

  // Bluetooth-down indicator, top-right corner. Shown only while disconnected.
  bt_indicator = text_layer_create(GRect(screen_w - BT_W, 0, BT_W, BAR_TOP_H));
  text_layer_set_text(bt_indicator, "BT");
  text_layer_set_text_color(bt_indicator, GColorWhite);
  text_layer_set_background_color(bt_indicator, GColorBlack);
  text_layer_set_font(bt_indicator, bar_font);
  text_layer_set_text_alignment(bt_indicator, GTextAlignmentRight);
  layer_add_child(window_layer, text_layer_get_layer(bt_indicator));
  layer_set_hidden(text_layer_get_layer(bt_indicator), true);

  // prepare the initial values of your data
    Tuplet initial_values[] = {
        TupletInteger(CONF_TEXTSTYLE, (uint8_t) text_style),
        TupletInteger(CONF_ALIGNMENT, (uint8_t) text_align),
        TupletInteger(CONF_BLUETOOTH, (uint8_t) bluetooth ? 1 : 0),
        TupletInteger(CONF_WEATHER,   (uint8_t) weather),
        TupletInteger(WEATHER_TEMPERATURE_UNIT, (bool) temp_unit),
        TupletInteger(CONF_SHOWSTEPS, (uint8_t) (show_steps ? 1 : 0)),
        MyTupletCString(WEATHER_ICON_KEY, weather_str),
        MyTupletCString(WEATHER_TEMPERATURE_C_KEY, temp_c_str),
        MyTupletCString(WEATHER_TEMPERATURE_F_KEY, temp_f_str)
    };
    // initialize the syncronization
    app_sync_init(&sync, sync_buffer, sizeof(sync_buffer), initial_values, ARRAY_LENGTH(initial_values),
        sync_tuple_changed_callback, sync_error_callback, NULL);

  // Configure time on init
  time_t raw_time;

  time(&raw_time);
  t = localtime(&raw_time);
  display_initial_time(t);
  info_lines("");
}

static void window_unload(Window *window)
{

  // Free layers
  //layer_destroy(text_layer_get_layer(topbar.layer[0]));
  //layer_destroy(text_layer_get_layer(bottombarL.layer[0]));
  //layer_destroy(text_layer_get_layer(bottombarR.layer[0]));
  text_layer_destroy(topbar.layer[0]);
  text_layer_destroy(bottombarL.layer[0]);
  text_layer_destroy(bottombarC.layer[0]);
  text_layer_destroy(bottombarR.layer[0]);
  text_layer_destroy(bt_indicator);
  if (icon_steps_layer)   bitmap_layer_destroy(icon_steps_layer);
  if (icon_battery_layer) bitmap_layer_destroy(icon_battery_layer);
  if (icon_steps_bmp)   gbitmap_destroy(icon_steps_bmp);
  if (icon_battery_bmp) gbitmap_destroy(icon_battery_bmp);

  for (int i = 0; i < NUM_LINES; i++)
  {
    destroy_line(&lines[i]);
  }
}

static void handle_init() {
  
  // Load settings from persistent storage
  if (persist_exists(CONF_TEXTSTYLE))
  {
    text_style = persist_read_int(CONF_TEXTSTYLE);
  }
  if (persist_exists(CONF_ALIGNMENT))
  {
    text_align = persist_read_int(CONF_ALIGNMENT);
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "Read text CONF_ALIGNMENT from store: %u", text_align);
  }
  if (persist_exists(CONF_BLUETOOTH))
  {
    bluetooth = persist_read_bool(CONF_BLUETOOTH);
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "Read CONF_BLUETOOTH from store: %u", bluetooth ? 1 : 0);
  }
  if (persist_exists(CONF_WEATHER))
  {
    weather = persist_read_int(CONF_WEATHER);
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "Read CONF_WEATHER from store: %u", weather);
  }
  if (persist_exists(WEATHER_ICON_KEY))
  {
    persist_read_string(WEATHER_ICON_KEY, weather_str, sizeof(weather_str));
    // Migrate the upstream's "012345..." placeholder if it persisted from an older install.
    if (weather_str[0] == '0' && weather_str[1] == '1' && weather_str[2] == '2') weather_str[0] = '\0';
  }
  if (persist_exists(WEATHER_TEMPERATURE_C_KEY))
  {
    persist_read_string(WEATHER_TEMPERATURE_C_KEY, temp_c_str, sizeof(temp_c_str));
    if (temp_c_str[0] == '0' && temp_c_str[1] == '1' && temp_c_str[2] == '2') temp_c_str[0] = '\0';
  }
  if (persist_exists(WEATHER_TEMPERATURE_F_KEY))
  {
    persist_read_string(WEATHER_TEMPERATURE_F_KEY, temp_f_str, sizeof(temp_f_str));
    if (temp_f_str[0] == '0' && temp_f_str[1] == '1' && temp_f_str[2] == '2') temp_f_str[0] = '\0';
  }
  if (persist_exists(WEATHER_TEMPERATURE_UNIT))
  {
    temp_unit=persist_read_bool(WEATHER_TEMPERATURE_UNIT);
  }
  if (persist_exists(CONF_SHOWSTEPS))
  {
    show_steps = persist_read_bool(CONF_SHOWSTEPS);
  }

  window = window_create();
  window_set_background_color(window, GColorBlack);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload
  });

  const bool animated = true;
  window_stack_push(window, animated);
  
  // Subscribe to minute ticks
  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
  
  /////////////////////////////////////////////////ZECOJ/////////////////////////////////////////////////
  // If I monitor bluetooth Subscribe to bluetooth service
  if (bluetooth) {
    bluetooth_connection_service_subscribe(bluetooth_connection_handler);
    bt_connect_toggle = bluetooth_connection_service_peek();
    layer_set_hidden(text_layer_get_layer(bt_indicator), bt_connect_toggle);
  }

  // Subscribe to shake events
  accel_tap_service_subscribe(wrist_flick_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);

#if defined(PBL_HEALTH)
  // Pebble Health: available on basalt/chalk/diorite/emery (not aplite). Subscribe and
  // prime today's step total + the most recent HR reading (peek returns 0 on
  // platforms without an HRM, which is fine — info_lines hides the bpm slot then).
  if (health_service_events_subscribe(health_handler, NULL)) {
    current_steps = (int)health_service_sum_today(HealthMetricStepCount);
    HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
    if (hr > 0) current_hr = (int)hr;
  }
#endif

  // to sync watch fields
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  /////////////////////////////////////////////////ZECOJ/////////////////////////////////////////////////
}

static void handle_deinit()
{
  // Free window
  app_sync_deinit(&sync);
  accel_tap_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  window_destroy(window);
}

int main(void)
{
  handle_init();
  app_event_loop();
  handle_deinit();
}