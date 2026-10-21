#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <cstring>

#include "AcConfig.h"
#include "AcProtocol.h"
#include "ui/ui.h"
#include "ui/ac_ui_bridge.h"

namespace {

// ------------------------------------------------------------------ radio
constexpr uint8_t BROADCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
constexpr uint8_t TP_SCLK = 25, TP_MISO = 39, TP_MOSI = 32, TP_CS = 33, TP_IRQ = 36;
constexpr uint8_t TFT_BACKLIGHT = 21;
constexpr unsigned long HELLO_INTERVAL_MS = 1000;
constexpr unsigned long LINK_TIMEOUT_MS = 20000;
constexpr unsigned long RETRY_INTERVAL_MS = 550;
constexpr int MAX_ATTEMPTS = 4;
constexpr uint8_t MAX_QUEUED = 8;

// ------------------------------------------------------------------- LVGL
// The EEZ project is 320x240 landscape; the CYD panel is 240x320 portrait
// and TFT_eSPI's setRotation(1) maps it to 320x240.
constexpr int LCD_W = 320;
constexpr int LCD_H = 240;
constexpr int DRAW_BUF_LINES = 20;

// Touch calibration. The crosshairs sit CAL_INSET pixels in from the edges so
// the whole 320x240 area is reachable; the map is extrapolated beyond them.
constexpr int CAL_VER = 2;  // bump to invalidate stored calibrations
constexpr int CAL_INSET = 12;
constexpr int CAL_X0 = CAL_INSET;
constexpr int CAL_X1 = LCD_W - 1 - CAL_INSET;
constexpr int CAL_Y0 = CAL_INSET;
constexpr int CAL_Y1 = LCD_H - 1 - CAL_INSET;
// How long after a confirmed press the XPT2046 interrupt line is still
// trusted to re-arm the library's polling.
constexpr uint32_t TOUCH_REARM_MS = 500;

TFT_eSPI tft;
SPIClass touchSpi(VSPI); // separate physical bus from ILI9341 on HSPI
XPT2046_Touchscreen touch(TP_CS, TP_IRQ);
Preferences prefs;
WiFiClient net;
PubSubClient mqtt(net);

lv_display_t* g_disp = nullptr;
lv_indev_t* g_indev = nullptr;
// The draw buffer lives on the heap: a 13 kB static array would overflow the
// ESP32's dram0_0_seg region together with the rest of .bss.
lv_color_t* g_draw_buf = nullptr;

struct Incoming { uint8_t mac[6]; ac::Frame frame; };
QueueHandle_t incomingQueue = nullptr;
uint8_t irMac[6] = {};
bool haveIrMac = false;
uint32_t lastNodeHello = 0;
uint32_t lastHelloSent = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastWifiAttempt = 0;
uint32_t lastAvailabilityPublish = 0;
uint32_t nextSeq = 1;

ac::State desired{};
ac::State applied{};
ac::Frame queued[MAX_QUEUED];
uint8_t queueHead = 0, queueCount = 0;
ac::Frame pending{};
bool hasPending = false;
uint8_t attempts = 0;
uint32_t lastAttempt = 0;
bool lastOnline = false;   // used by the MQTT availability publisher
bool uiLinkOnline = false;  // last link state pushed to the UI
bool linkDirty = true;

// Last state handed to the UI bridge, used to detect toggle edges.
ac_ui_state_t lastSent{};
bool haveLastSent = false;

void publishApplied();

struct Cal {
  int32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  bool xIsRawX = true;
  bool valid = false;
} cal;

bool linked() {
  return haveIrMac && millis() - lastNodeHello < LINK_TIMEOUT_MS;
}

bool addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.ifidx = WIFI_IF_STA;
  peer.channel = 0;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
  if (len != static_cast<int>(sizeof(ac::Frame)) || !incomingQueue) return;
  Incoming in{};
  memcpy(in.mac, mac, 6);
  memcpy(&in.frame, data, sizeof(in.frame));
  if (!ac::validFrame(in.frame)) return;

  // Answer a Hello right here, in the Wi-Fi task, so the reply goes out on
  // the channel the request just arrived on rather than on whatever channel
  // the radio has drifted to by the time the main loop runs.
  if (in.frame.role == static_cast<uint8_t>(ac::Role::IrNode) &&
      in.frame.type == static_cast<uint8_t>(ac::Type::Hello)) {
    ac::Frame reply = ac::hello(ac::Role::Remote);
    esp_now_send(mac, reinterpret_cast<const uint8_t*>(&reply), sizeof(reply));
  }
  xQueueSend(incomingQueue, &in, 0);
}

void broadcastHello() {
  const ac::Frame f = ac::hello(ac::Role::Remote);
  const esp_err_t err = esp_now_send(BROADCAST, reinterpret_cast<const uint8_t*>(&f), sizeof(f));
  if (err != ESP_OK) Serial.printf("hello broadcast failed: %d\n", (int)err);
  // Also address the known IR node directly: a broadcast only gets through
  // when the receiver's radio happens to be on our channel, and a unicast
  // to a known peer is much more likely to be received.
  if (haveIrMac) {
    esp_now_send(irMac, reinterpret_cast<const uint8_t*>(&f), sizeof(f));
  }
}

void processIncoming(const Incoming& in) {
  const ac::Frame& f = in.frame;
  if (f.role != static_cast<uint8_t>(ac::Role::IrNode)) return;
  if (haveIrMac && memcmp(in.mac, irMac, 6) != 0) return;
  if (!addPeer(in.mac)) return;
  if (!haveIrMac) {
    memcpy(irMac, in.mac, 6);
    haveIrMac = true;
    Serial.printf("Discovered IR node %02X:%02X:%02X:%02X:%02X:%02X\n",
                  in.mac[0],in.mac[1],in.mac[2],in.mac[3],in.mac[4],in.mac[5]);
  }
  lastNodeHello = millis();
  linkDirty = true;
  if (f.type == static_cast<uint8_t>(ac::Type::Hello)) {
    static uint32_t lastHelloLog = 0;
    if (millis() - lastHelloLog > 3000) {
      lastHelloLog = millis();
      Serial.printf("IR node hello, channel %d\n", WiFi.channel());
    }
  }
  if (f.type != static_cast<uint8_t>(ac::Type::Ack)) return;
  if (!hasPending || f.seq != pending.seq || f.action != pending.action) return;
  hasPending = false;
  if (f.result == static_cast<uint8_t>(ac::Result::Ok)) {
    if (pending.action == static_cast<uint8_t>(ac::Action::SetState)) {
      applied = pending.state;
      prefs.putBytes("state", &applied, sizeof(applied));
    }
    publishApplied();
  }
}

bool enqueue(ac::Action action) {
  ac::Frame f = ac::command(action, nextSeq++, desired);
  if (action == ac::Action::SetState && queueCount) {
    const uint8_t tail = (queueHead + queueCount - 1) % MAX_QUEUED;
    if (queued[tail].action == static_cast<uint8_t>(ac::Action::SetState)) {
      queued[tail] = f; // coalesce rapid +/- taps
      return true;
    }
  }
  if (queueCount == MAX_QUEUED) return false;
  queued[(queueHead + queueCount) % MAX_QUEUED] = f;
  ++queueCount;
  return true;
}

void sendPending() {
  if (!haveIrMac || !hasPending) return;
  esp_err_t err = esp_now_send(irMac,
      reinterpret_cast<const uint8_t*>(&pending), sizeof(pending));
  attempts++;
  lastAttempt = millis();
  if (err != ESP_OK) Serial.printf("esp_now_send error: %d\n", err);
}

void updateCommands() {
  if (hasPending) {
    if (millis() - lastAttempt >= RETRY_INTERVAL_MS) {
      if (attempts < MAX_ATTEMPTS) {
        sendPending(); // receiver deduplicates seq to protect toggles
      } else {
        hasPending = false;
      }
    }
  }
  if (!hasPending && linked() && queueCount) {
    pending = queued[queueHead];
    queueHead = (queueHead + 1) % MAX_QUEUED;
    --queueCount;
    attempts = 0;
    hasPending = true;
    sendPending();
  }
}

String modeName(uint8_t value) {
  switch (static_cast<ac::Mode>(value)) {
    case ac::Mode::Cool: return "cool";
    case ac::Mode::Auto: return "auto";
    case ac::Mode::Heat: return "heat";
    case ac::Mode::Dry: return "dry";
    case ac::Mode::Fan: return "fan";
    default: return "cool";
  }
}
String fanName(uint8_t value) {
  switch (static_cast<ac::Fan>(value)) {
    case ac::Fan::Auto: return "auto";
    case ac::Fan::Low: return "low";
    case ac::Fan::Medium: return "medium";
    case ac::Fan::High: return "high";
    default: return "auto";
  }
}

String topic(const char* suffix) {
  return String(MQTT_TOPIC_BASE) + "/" + suffix;
}
void publishAvailability(bool force = false) {
  if (!ENABLE_MQTT || !mqtt.connected()) return;
  if (force || lastOnline != linked() || millis() - lastAvailabilityPublish > 12000) {
    lastOnline = linked();
    lastAvailabilityPublish = millis();
    String t = topic("availability");
    mqtt.publish(t.c_str(), linked() ? "online" : "offline", true);
  }
}
void publishApplied() {
  if (!ENABLE_MQTT || !mqtt.connected()) return;
  const String m = topic("state/mode");
  const String temp = topic("state/temperature");
  const String fan = topic("state/fan");
  const String actualMode = applied.power ? modeName(applied.mode) : "off";
  mqtt.publish(m.c_str(), actualMode.c_str(), true);
  mqtt.publish(temp.c_str(), String(applied.tempC).c_str(), true);
  mqtt.publish(fan.c_str(), fanName(applied.fan).c_str(), true);
}

// ------------------------------------------------- EEZ bridge translation
// The EEZ UI uses its own mode/fan numbering; the ESP-NOW wire format uses a
// different one. Translate explicitly, never cast.
uint8_t acModeToUi(uint8_t acMode) {
  switch (static_cast<ac::Mode>(acMode)) {
    case ac::Mode::Auto: return 0;  // AUTO
    case ac::Mode::Cool: return 1;  // COOL
    case ac::Mode::Dry: return 2;   // DRY
    case ac::Mode::Fan: return 3;   // FAN
    case ac::Mode::Heat: return 4;  // HEAT
    default: return 1;              // COOL
  }
}

// Push the locally desired state into the UI, preserving the toggles that the
// wire protocol cannot represent.
void pushDesiredToUi() {
  ac_ui_state_t u = *ac_ui_current_state();
  u.temperature_c = desired.tempC;
  u.power = desired.power ? 1 : 0;
  u.mode = acModeToUi(desired.mode);
  u.fan = desired.fan;
  ac_ui_apply_remote_state(&u);
}

void onMqtt(char* rawTopic, byte* payload, unsigned int length) {
  if (length > 48) return;
  String v;
  for (unsigned int i=0; i<length; ++i) v += static_cast<char>(payload[i]);
  v.trim(); v.toLowerCase();
  String t(rawTopic);
  bool changed = false;
  if (t == topic("set/mode")) {
    if (v == "off") { desired.power = 0; changed = true; }
    else if (v == "cool" || v == "heat" || v == "auto" || v == "dry" || v == "fan") {
      desired.power = 1;
      desired.mode = v == "cool" ? (uint8_t)ac::Mode::Cool :
                     v == "heat" ? (uint8_t)ac::Mode::Heat :
                     v == "dry"  ? (uint8_t)ac::Mode::Dry :
                     v == "fan"  ? (uint8_t)ac::Mode::Fan :
                                  (uint8_t)ac::Mode::Auto;
      if (desired.mode == (uint8_t)ac::Mode::Auto ||
          desired.mode == (uint8_t)ac::Mode::Dry) {
        // Both modes let the AC pick the fan speed.
        desired.fan = (uint8_t)ac::Fan::Auto;
      }
      changed = true;
    }
  } else if (t == topic("set/temperature")) {
    const int n = v.toInt();
    if (n >= 17 && n <= 30) {
      desired.tempC = static_cast<uint8_t>(n);
      changed = desired.power != 0;
    }
  } else if (t == topic("set/fan")) {
    if (desired.mode != (uint8_t)ac::Mode::Auto &&
        desired.mode != (uint8_t)ac::Mode::Dry) {
      if (v == "auto") desired.fan = (uint8_t)ac::Fan::Auto;
      else if (v == "low") desired.fan = (uint8_t)ac::Fan::Low;
      else if (v == "medium") desired.fan = (uint8_t)ac::Fan::Medium;
      else if (v == "high") desired.fan = (uint8_t)ac::Fan::High;
      else return;
      changed = desired.power != 0;
    }
  } else if (t == topic("set/action")) {
    if (v == "led") enqueue(ac::Action::LedToggle);
    else if (v == "turbo") enqueue(ac::Action::TurboToggle);
  }
  if (changed) {
    pushDesiredToUi();
    enqueue(ac::Action::SetState);
  }
}

void publishDiscovery() {
  // Home Assistant MQTT climate discovery; disabled unless MQTT enabled.
  const String discoveryTopic = "homeassistant/climate/midea_espnow/config";
  const String base = MQTT_TOPIC_BASE;
  String json = "{\"name\":\"Midea AC\",\"unique_id\":\"midea_espnow_cyd_01\",";
  json += "\"~\":\"" + base + "\",";
  json += "\"mode_command_topic\":\"~/set/mode\",\"mode_state_topic\":\"~/state/mode\",";
  json += "\"temperature_command_topic\":\"~/set/temperature\",\"temperature_state_topic\":\"~/state/temperature\",";
  json += "\"fan_command_topic\":\"~/set/fan\",\"fan_state_topic\":\"~/state/fan\",";
  json += "\"availability_topic\":\"~/availability\",\"payload_available\":\"online\",\"payload_unavailable\":\"offline\",";
  json += "\"modes\":[\"off\",\"auto\",\"cool\",\"dry\",\"fan\",\"heat\"],";
  json += "\"fan_modes\":[\"auto\",\"low\",\"medium\",\"high\"],";
  json += "\"min_temp\":17,\"max_temp\":30,\"temp_step\":1,\"temperature_unit\":\"C\"}";
  mqtt.publish(discoveryTopic.c_str(), json.c_str(), true);
}

void updateMqtt() {
  if (!ENABLE_MQTT) return;
  ac_ui_set_mqtt_status(mqtt.connected());
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiAttempt >= 10000) {
      lastWifiAttempt = millis();
      WiFi.reconnect();
    }
    return;
  }
  if (!mqtt.connected() && millis() - lastMqttAttempt >= 5000) {
    lastMqttAttempt = millis();
    const String offline = topic("availability");
    bool ok = false;
    if (strlen(MQTT_USERNAME)) {
      ok = mqtt.connect("midea-espnow-cyd", MQTT_USERNAME, MQTT_PASSWORD,
                        offline.c_str(), 0, true, "offline");
    } else {
      ok = mqtt.connect("midea-espnow-cyd", offline.c_str(),
                        0, true, "offline");
    }
    if (ok) {
      const String mode = topic("set/mode");
      const String temp = topic("set/temperature");
      const String fan = topic("set/fan");
      const String action = topic("set/action");
      mqtt.subscribe(mode.c_str()); mqtt.subscribe(temp.c_str());
      mqtt.subscribe(fan.c_str()); mqtt.subscribe(action.c_str());
      publishDiscovery(); publishApplied(); publishAvailability(true);
      ac_ui_set_mqtt_status(true);
      Serial.println("MQTT connected and HA discovery published");
    } else {
      Serial.printf("MQTT connect failed, state=%d\n", mqtt.state());
    }
  }
  if (mqtt.connected()) {
    mqtt.loop();
    publishAvailability();
  }
}

// --------------------------------------------------------- touch handling
TS_Point waitCalPress() {
  // Disallow reusing the previous corner's finger-down event.
  while (touch.touched()) delay(25);
  while (!touch.touched()) delay(15);
  delay(80);
  TS_Point p = touch.getPoint();
  while (touch.touched()) delay(20);
  delay(150);
  return p;
}

TS_Point calibratePoint(int x, int y, const char* label) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Touch calibration", 160, 110, 4);
  tft.drawString(label, 160, 141, 2);
  tft.drawCircle(x, y, 12, TFT_YELLOW);
  tft.drawFastHLine(x - 17, y, 34, TFT_YELLOW);
  tft.drawFastVLine(x, y - 17, 34, TFT_YELLOW);
  return waitCalPress();
}

void loadCalibration() {
  cal.valid = prefs.getBool("cal_ok", false);
  cal.x0 = prefs.getInt("cx0", 0);
  cal.x1 = prefs.getInt("cx1", 0);
  cal.y0 = prefs.getInt("cy0", 0);
  cal.y1 = prefs.getInt("cy1", 0);
  cal.xIsRawX = prefs.getBool("crawx", true);
  if (prefs.getInt("calver", 0) != CAL_VER) cal.valid = false;
  if (abs(cal.x1 - cal.x0) < 700 || abs(cal.y1 - cal.y0) < 500) cal.valid = false;
}

void calibrateTouch() {
  const TS_Point tl = calibratePoint(CAL_X0, CAL_Y0, "Top left");
  const TS_Point tr = calibratePoint(CAL_X1, CAL_Y0, "Top right");
  const TS_Point bl = calibratePoint(CAL_X0, CAL_Y1, "Bottom left");
  cal.xIsRawX = abs(tr.x - tl.x) > abs(tr.y - tl.y);
  cal.x0 = cal.xIsRawX ? tl.x : tl.y;
  cal.x1 = cal.xIsRawX ? tr.x : tr.y;
  cal.y0 = cal.xIsRawX ? tl.y : tl.x;
  cal.y1 = cal.xIsRawX ? bl.y : bl.x;
  cal.valid = abs(cal.x1 - cal.x0) >= 700 && abs(cal.y1 - cal.y0) >= 500;
  if (cal.valid) {
    prefs.putInt("cx0", cal.x0); prefs.putInt("cx1", cal.x1);
    prefs.putInt("cy0", cal.y0); prefs.putInt("cy1", cal.y1);
    prefs.putBool("crawx", cal.xIsRawX); prefs.putBool("cal_ok", true);
    prefs.putInt("calver", CAL_VER);
  }
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(cal.valid ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.drawString(cal.valid ? "Calibration saved" : "Calibration failed", 160, 120, 4);
  delay(1200);
}

// Map a raw XPT2046 sample onto the 320x240 LVGL coordinate space.
void rawToScreen(int rawX, int rawY, int* outX, int* outY) {
  const int rx = cal.xIsRawX ? rawX : rawY;
  const int ry = cal.xIsRawX ? rawY : rawX;
  const int spanX = cal.x1 - cal.x0;
  const int spanY = cal.y1 - cal.y0;
  int x = CAL_X0 + (rx - cal.x0) * (CAL_X1 - CAL_X0) / (spanX ? spanX : 1);
  int y = CAL_Y0 + (ry - cal.y0) * (CAL_Y1 - CAL_Y0) / (spanY ? spanY : 1);
  *outX = constrain(x, 0, LCD_W - 1);
  *outY = constrain(y, 0, LCD_H - 1);
}

// ------------------------------------------------------------ LVGL drivers
// Last reported touch position and the time of the last confirmed press.
int lastTouchX = 0;
int lastTouchY = 0;
uint32_t lastTouchMs = 0;
// Touch tracing, so the mapping can be checked against known button positions.
bool touchWasDown = false;
uint32_t lastTouchPrint = 0;

void dispFlush(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  // The byte swap is required: without it the panel interprets every RGB565
  // value the wrong way round and the dark UI turns olive/cyan/purple.
  tft.pushColors(reinterpret_cast<uint16_t*>(px), w * h, true);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

void touchRead(lv_indev_t* indev, lv_indev_data_t* data) {
  data->point.x = lastTouchX;
  data->point.y = lastTouchY;
  data->state = LV_INDEV_STATE_RELEASED;
  data->timestamp = lv_tick_get();
  if (!cal.valid) return;

  const int irq = digitalRead(TP_IRQ);
  const bool pressed = touch.touched();

  if (pressed) {
    const TS_Point p = touch.getPoint();
    rawToScreen(p.x, p.y, &lastTouchX, &lastTouchY);
    lastTouchMs = millis();
    data->state = LV_INDEV_STATE_PRESSED;
    // Report the position of *this* reading. Setting data->point before the
    // update above hands LVGL the position of the previous poll, so a tap
    // lands on whatever was touched last.
    data->point.x = lastTouchX;
    data->point.y = lastTouchY;
    // Log on the transitions, plus a slow position trace while held.
    if (!touchWasDown) {
      Serial.printf("TOUCH down irq=%d z=%d raw=(%d,%d) -> screen=(%d,%d)\n",
                    irq, p.z, p.x, p.y, lastTouchX, lastTouchY);
      lastTouchPrint = millis();
    } else if (millis() - lastTouchPrint > 1000) {
      Serial.printf("TOUCH hold raw=(%d,%d) -> screen=(%d,%d)\n",
                    p.x, p.y, lastTouchX, lastTouchY);
      lastTouchPrint = millis();
    }
    touchWasDown = true;
    return;
  }

  if (touchWasDown) {
    Serial.printf("TOUCH up   irq=%d -> last screen=(%d,%d)\n", irq,
                  lastTouchX, lastTouchY);
    touchWasDown = false;
  }

  // The XPT2046 reports pressure rather than a pen-down line, and the library
  // stops polling once a reading falls below its threshold. If the pen is
  // still down, re-arm the library so the next poll reads again.
  //
  // The interrupt pin is only used to re-arm, never to decide the state: on
  // some CYD revisions it is not pulled up and reads low while idle, and
  // reporting a press from it leaves the input device stuck in the pressed
  // state, which makes every button need two taps.
  if ((millis() - lastTouchMs) < TOUCH_REARM_MS && irq == LOW) {
    touch.isrWake = true;
  }
}

// The EEZ project only styles LV_STATE_DEFAULT, and a local DEFAULT style
// outranks the theme's PRESSED style, so the buttons would not show a press
// at all. Give every button explicit colours for the interactive states, in
// the project's own dark palette rather than the theme's.
void styleButtonsRec(lv_obj_t* parent) {
  const uint32_t count = lv_obj_get_child_count(parent);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(parent, i);
    if (lv_obj_check_type(child, &lv_button_class)) {
      lv_obj_set_style_bg_color(child, lv_color_hex(0x1d3a63), LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_set_style_border_color(child, lv_color_hex(0x14e6f4), LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_set_style_text_color(child, lv_color_hex(0x14e6f4), LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_set_style_bg_color(child, lv_color_hex(0x213751), LV_PART_MAIN | LV_STATE_CHECKED);
      lv_obj_set_style_border_color(child, lv_color_hex(0x14e6f4), LV_PART_MAIN | LV_STATE_CHECKED);
      lv_obj_set_style_outline_width(child, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
      lv_obj_set_style_outline_width(child, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    }
    styleButtonsRec(child);
  }
}

void styleButtons() {
  styleButtonsRec(objects.main);
  styleButtonsRec(objects.extras);
  styleButtonsRec(objects.network);
}

// A short lived message on top of the current screen. The bridge's status
// text lives on the Network screen, which is not where the user is when a
// button does not do what they expect.
lv_obj_t* g_toast[3] = {nullptr, nullptr, nullptr};
uint32_t g_toastUntil = 0;

void initToast() {
  lv_obj_t* screens[3] = {objects.main, objects.extras, objects.network};
  for (int i = 0; i < 3; i++) {
    if (!screens[i]) continue;
    lv_obj_t* t = lv_label_create(screens[i]);
    lv_label_set_long_mode(t, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(t, 296);
    lv_obj_set_pos(t, 12, 38);
    lv_obj_set_style_bg_color(t, lv_color_hex(0x14243B), 0);
    lv_obj_set_style_bg_opa(t, 240, 0);
    lv_obj_set_style_border_color(t, lv_color_hex(0x14E6F4), 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_radius(t, 6, 0);
    lv_obj_set_style_pad_all(t, 6, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x14E6F4), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
    lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
    g_toast[i] = t;
  }
}

void showToast(const char* msg) {
  if (!msg || !msg[0] || !g_toast[0]) return;
  lv_obj_t* act = lv_screen_active();
  int idx = -1;
  if (act == objects.main) idx = 0;
  else if (act == objects.extras) idx = 1;
  else if (act == objects.network) idx = 2;
  if (idx < 0 || !g_toast[idx]) return;
  lv_label_set_text(g_toast[idx], msg);
  lv_obj_clear_flag(g_toast[idx], LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_toast[idx]);
  g_toastUntil = millis() + 3500;
}

void updateToast() {
  if (!g_toastUntil || millis() < g_toastUntil) return;
  for (int i = 0; i < 3; i++) {
    if (g_toast[i]) lv_obj_add_flag(g_toast[i], LV_OBJ_FLAG_HIDDEN);
  }
  g_toastUntil = 0;
}

// Force LVGL's default theme to the dark palette with the project's own
// accent colours. The theme is a singleton, so re-initialising it after
// ui_init() re-styles it; the widgets' own local styles still win.
void applyDarkTheme() {
  lv_theme_t* theme = lv_theme_default_init(g_disp,
                                             lv_color_hex(0x14243B),  // primary
                                             lv_color_hex(0x0D1830),  // secondary
                                             true,                  // dark
                                             LV_FONT_DEFAULT);
  if (theme) lv_display_set_theme(g_disp, theme);
}

void setupLvgl() {
  // One extra line of slack: TFT_eSPI's pushPixels() may read a word past the
  // last pixel of the flushed area.
  const size_t bufBytes = sizeof(lv_color_t) * (LCD_W * (DRAW_BUF_LINES + 1));
  g_draw_buf = static_cast<lv_color_t*>(heap_caps_malloc(bufBytes, MALLOC_CAP_DMA));
  if (!g_draw_buf) {
    Serial.printf("LVGL draw buffer allocation failed (%u bytes)\n", (unsigned)bufBytes);
    return;
  }

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  lv_delay_set_cb([](uint32_t ms) { delay(ms); });

  g_disp = lv_display_create(LCD_W, LCD_H);
  lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(g_disp, dispFlush);
  lv_display_set_buffers(g_disp, g_draw_buf, nullptr,
                         bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  g_indev = lv_indev_create();
  lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(g_indev, touchRead);
  lv_indev_set_display(g_indev, g_disp);

  ui_init();
  applyDarkTheme();
  styleButtons();
  initToast();
}

void setupRadio() {
  WiFi.mode(WIFI_STA);
  if (ENABLE_MQTT) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("Wi-Fi channel %d, IP %s\n", WiFi.channel(), WiFi.localIP().toString().c_str());
      Serial.println("Set IR node ESPNOW_CHANNEL to this channel!");
    } else {
      Serial.println("Wi-Fi unavailable; attempting local ESP-NOW");
      esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    }
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(onMqtt);
    mqtt.setBufferSize(1024);
    lastMqttAttempt = millis() - 5000;
  } else {
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  }
  Serial.printf("CYD MAC: %s, current Wi-Fi channel: %d\n",
                WiFi.macAddress().c_str(), WiFi.channel());
  incomingQueue = xQueueCreate(16, sizeof(Incoming));
  if (!incomingQueue || esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW initialization FAILED"); return;
  }
  // Set the channel after esp_now_init(), as the ESP-NOW docs require.
  // With MQTT the station has to stay on the router's channel, so the fixed
  // ESPNOW_CHANNEL is only used in ESP-NOW-only mode.
  if (!ENABLE_MQTT) {
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  }
  esp_now_register_recv_cb(onReceive);
  if (!addPeer(BROADCAST)) Serial.println("Broadcast peer FAILED");
  lastHelloSent = millis() - HELLO_INTERVAL_MS;
  Serial.printf("CYD MAC: %s, Wi-Fi channel: %d, ESP-NOW channel: %u%s\n",
                WiFi.macAddress().c_str(), WiFi.channel(), ESPNOW_CHANNEL,
                ENABLE_MQTT ? " (MQTT: the IR node must use the router's channel)"
                             : "");
}
} // namespace

// --------------------------------------------------- strong bridge hooks
// These replace the weak no-op defaults in the generated ui/actions.c.

extern "C" void ac_ui_send_state(const ac_ui_state_t* s) {
  if (!s) return;

  const bool wasPoweredOn = desired.power != 0;
  bool changed = false;
  const char* note = nullptr;

  // Mode: the UI numbers the modes its own way; the wire protocol uses a
  // different one. Translate explicitly, never cast.
  uint8_t mode = static_cast<uint8_t>(ac::Mode::Cool);
  bool modeOk = true;
  switch (s->mode) {
    case 0: mode = static_cast<uint8_t>(ac::Mode::Auto); break;
    case 1: mode = static_cast<uint8_t>(ac::Mode::Cool); break;
    case 2: mode = static_cast<uint8_t>(ac::Mode::Dry); break;
    case 3: mode = static_cast<uint8_t>(ac::Mode::Fan); break;
    case 4: mode = static_cast<uint8_t>(ac::Mode::Heat); break;
    default:
      modeOk = false;
      break;
  }
  if (modeOk && desired.mode != mode) {
    desired.mode = mode;
    changed = true;
  }

  // Fan numbering happens to match (Auto/Low/Med/High = 0..3).
  uint8_t fan = s->fan > 3 ? static_cast<uint8_t>(ac::Fan::Auto) : s->fan;
  if (desired.mode == static_cast<uint8_t>(ac::Mode::Auto) ||
      desired.mode == static_cast<uint8_t>(ac::Mode::Dry)) {
    // Both modes let the AC pick the fan speed, so the card shows Auto.
    fan = static_cast<uint8_t>(ac::Fan::Auto);
  }
  // The mode does not allow the speed the card asked for.
  const bool fanForced = fan != s->fan;
  if (desired.fan != fan) {
    desired.fan = fan;
    changed = true;
  }

  const int16_t temp = s->temperature_c < 17 ? 17 : (s->temperature_c > 30 ? 30 : s->temperature_c);
  if (desired.tempC != static_cast<uint8_t>(temp)) {
    desired.tempC = static_cast<uint8_t>(temp);
    changed = true;
  }

  const uint8_t power = s->power ? 1 : 0;
  if (desired.power != power) {
    desired.power = power;
    changed = true;
  }

  // Toggles the AC applies as discrete commands, not as state.
  const bool ledEdge = haveLastSent && !lastSent.display_led && s->display_led;
  const bool turboEdge = haveLastSent && !lastSent.turbo && s->turbo;
  if (ledEdge) enqueue(ac::Action::LedToggle);
  if (turboEdge) enqueue(ac::Action::TurboToggle);
  if (haveLastSent && (!lastSent.swing && s->swing)) note = "Swing is not in the IR protocol";
  if (haveLastSent && (!lastSent.eco && s->eco)) note = "Eco is not in the IR protocol";
  if (haveLastSent && (!lastSent.sleep && s->sleep)) note = "Sleep is not in the IR protocol";

  lastSent = *s;
  haveLastSent = true;

  if (changed) {
    // Preset-only changes while OFF are local; an ON->OFF transition (or
    // cancellation of an unacknowledged ON) still transmits an OFF frame.
    if (desired.power || wasPoweredOn || applied.power)
      enqueue(ac::Action::SetState);
  }
  if (note) {
    ac_ui_set_link_status(linked(), note);
    showToast(note);
    // Put back what the AC can actually do, so the display never disagrees
    // with the air conditioner.
    ac_ui_state_t real = *ac_ui_current_state();
    real.temperature_c = desired.tempC;
    real.power = desired.power;
    real.mode = acModeToUi(desired.mode);
    real.fan = desired.fan;
    real.swing = 0;
    real.eco = 0;
    real.sleep = 0;
    ac_ui_apply_remote_state(&real);
  } else if (fanForced) {
    // The card asked for a speed the mode does not allow, so it goes back
    // to Auto. Otherwise the display would disagree with the AC.
    ac_ui_state_t real = *ac_ui_current_state();
    real.fan = desired.fan;
    ac_ui_apply_remote_state(&real);
  }
}

extern "C" void ac_ui_start_pairing(void) {
  haveIrMac = false;
  memset(irMac, 0, sizeof(irMac));
  lastNodeHello = 0;
  hasPending = false;
  queueCount = 0;
  queueHead = 0;
  broadcastHello();
  linkDirty = true;
  ac_ui_set_link_status(0, "Pairing: waiting for an IR node");
  showToast("Looking for an IR node...");
}

extern "C" void ac_ui_request_status(void) {
  broadcastHello();
  linkDirty = true;
  ac_ui_set_link_status(linked(), linked() ? "IR node online" : "No IR node answer");
  showToast(linked() ? "IR node online" : "No IR node answer");
}

void reportUiState() {
  const uint16_t bg = lv_color_to_u16(lv_obj_get_style_bg_color(objects.main, LV_PART_MAIN));
  // RGB565 -> #RRGGBB
  const unsigned r = ((bg >> 11) & 0x1F) << 3;
  const unsigned g = ((bg >> 5) & 0x3F) << 2;
  const unsigned b = (bg & 0x1F) << 3;
  Serial.printf("[ui] lvgl %d.%d.%d, %dx%d, screen bg=#%02x%02x%02x, theme=%p\n",
                LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
                LCD_W, LCD_H, r, g, b, (const void*)lv_display_get_theme(g_disp));
  const uint16_t pri = lv_color_to_u16(lv_theme_get_color_primary(objects.main));
  Serial.printf("[ui] theme primary=#%02x%02x%02x\n",
                ((pri >> 11) & 0x1F) << 3, ((pri >> 5) & 0x3F) << 2,
                (pri & 0x1F) << 3);
  Serial.printf("[ui] objects: main=%p extras=%p network=%p\n",
                (void*)objects.main, (void*)objects.extras, (void*)objects.network);
  Serial.printf("[touch] calibrated=%d raw=(%d..%d, %d..%d) swapped=%d\n",
                cal.valid ? 1 : 0, cal.x0, cal.x1, cal.y0, cal.y1,
                cal.xIsRawX ? 0 : 1);
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("\n[Midea touchscreen] ESP32-2432S028 / LVGL");

  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Midea ESP-NOW", 160, 110, 4);

  touchSpi.begin(TP_SCLK, TP_MISO, TP_MOSI, TP_CS);
  if (!touch.begin(touchSpi)) Serial.println("XPT2046 touch initialization failed");
  touch.setRotation(0); // raw coordinates; custom 3-point transform below

  prefs.begin("midea-cyd", false);
  if (prefs.getBytesLength("state") == sizeof(ac::State)) {
    prefs.getBytes("state", &applied, sizeof(applied));
    if (!ac::validState(applied)) applied = ac::State{};
  }
  desired = applied;
  loadCalibration();
  if (!cal.valid) calibrateTouch();

  setupLvgl();
  pushDesiredToUi();
  reportUiState();

  nextSeq = esp_random();
  setupRadio();
  ac_ui_set_link_status(false, "Looking for ESP32-C3 transmitter");
}

void loop() {
  if (Serial.available()) {
    const char key = static_cast<char>(Serial.read());
    if (key == 'c' || key == 'C') {
      calibrateTouch();
      if (g_disp) lv_refr_now(g_disp); // repaint over the calibration screen
    }
  }
  if (incomingQueue) {
    Incoming in{};
    while (xQueueReceive(incomingQueue, &in, 0) == pdTRUE) processIncoming(in);
    if (millis() - lastHelloSent >= HELLO_INTERVAL_MS) {
      lastHelloSent = millis();
      // A disconnected station keeps scanning other channels, so the radio is
      // rarely on the data channel when a frame arrives. Re-asserting the
      // channel often pulls the radio back and keeps it listening where we
      // want it. With MQTT the station has to stay on the router's channel
      // instead, so only do this in ESP-NOW-only mode.
      if (!ENABLE_MQTT) {
        esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
        broadcastHello();
      } else {
        broadcastHello();
      }
    }
    updateCommands();
  }
  if (linkDirty || uiLinkOnline != linked()) {
    linkDirty = false;
    uiLinkOnline = linked();
    ac_ui_set_link_status(uiLinkOnline, uiLinkOnline ? "IR transmitter responding"
                                                     : "Looking for ESP32-C3 transmitter");
  }
  updateMqtt();

  // Refresh the EEZ widgets from the bridge variables first, so a value
  // change is picked up by the redraw in the same pass.
  if (g_disp) {
    ui_tick();
    updateToast();
    lv_timer_handler();
  }
  delay(5);
}
