#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <sys/time.h>

#include "ST7305_U8g2.h"
#include "adc_bsp.h"

#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

#ifndef HKO_TEMPERATURE_STATION
#define HKO_TEMPERATURE_STATION "打鼓嶺"
#endif

// Unused schedule slots may be omitted from config.h.
#ifndef SCHEDULE_SLOT_COUNT
#define SCHEDULE_SLOT_COUNT 1
#endif
#ifndef SLOT_2_START_HOUR
#define SLOT_2_START_HOUR 0
#define SLOT_2_START_MINUTE 0
#define SLOT_2_ROUTE_1 ""
#define SLOT_2_STOP_ID_1 ""
#define SLOT_2_ROUTE_2 ""
#define SLOT_2_STOP_ID_2 ""
#define SLOT_2_ROUTE_3 ""
#define SLOT_2_STOP_ID_3 ""
#endif
#ifndef SLOT_3_START_HOUR
#define SLOT_3_START_HOUR 0
#define SLOT_3_START_MINUTE 0
#define SLOT_3_ROUTE_1 ""
#define SLOT_3_STOP_ID_1 ""
#define SLOT_3_ROUTE_2 ""
#define SLOT_3_STOP_ID_2 ""
#define SLOT_3_ROUTE_3 ""
#define SLOT_3_STOP_ID_3 ""
#endif
#ifndef SLOT_4_START_HOUR
#define SLOT_4_START_HOUR 0
#define SLOT_4_START_MINUTE 0
#define SLOT_4_ROUTE_1 ""
#define SLOT_4_STOP_ID_1 ""
#define SLOT_4_ROUTE_2 ""
#define SLOT_4_STOP_ID_2 ""
#define SLOT_4_ROUTE_3 ""
#define SLOT_4_STOP_ID_3 ""
#endif

constexpr int SCREEN_W = 400;
constexpr int SCREEN_H = 300;
constexpr uint8_t SHTC3_ADDR = 0x70;
constexpr uint8_t RTC_ADDR = 0x51;
constexpr uint32_t DATA_REFRESH_MS = 60000;
constexpr uint32_t WEATHER_REFRESH_MS = 30UL * 60UL * 1000UL;

ST7305_U8g2 display(11, 12, 5, 40, 41);
U8G2 *gfx = nullptr;

struct RouteData {
  const char *route;
  const char *stopId;
  int eta[3] = {-1, -1, -1};
};
struct ScheduleSlot {
  uint16_t startMinute;
  const char *route[3];
  const char *stopId[3];
};
enum WarningState { NORMAL, RAIN_AMBER, RAIN_RED, RAIN_BLACK, TC1, TC3, TC8, TC9, TC10 };
enum WeatherVisual { WEATHER_CLEAR, WEATHER_PARTLY, WEATHER_CLOUDY, WEATHER_RAIN, WEATHER_THUNDER };
RouteData routes[3] = {
  {SLOT_1_ROUTE_1, SLOT_1_STOP_ID_1},
  {SLOT_1_ROUTE_2, SLOT_1_STOP_ID_2},
  {SLOT_1_ROUTE_3, SLOT_1_STOP_ID_3}
};
const ScheduleSlot scheduleSlots[4] = {
  {(uint16_t)(SLOT_1_START_HOUR * 60 + SLOT_1_START_MINUTE),
   {SLOT_1_ROUTE_1, SLOT_1_ROUTE_2, SLOT_1_ROUTE_3},
   {SLOT_1_STOP_ID_1, SLOT_1_STOP_ID_2, SLOT_1_STOP_ID_3}},
  {(uint16_t)(SLOT_2_START_HOUR * 60 + SLOT_2_START_MINUTE),
   {SLOT_2_ROUTE_1, SLOT_2_ROUTE_2, SLOT_2_ROUTE_3},
   {SLOT_2_STOP_ID_1, SLOT_2_STOP_ID_2, SLOT_2_STOP_ID_3}},
  {(uint16_t)(SLOT_3_START_HOUR * 60 + SLOT_3_START_MINUTE),
   {SLOT_3_ROUTE_1, SLOT_3_ROUTE_2, SLOT_3_ROUTE_3},
   {SLOT_3_STOP_ID_1, SLOT_3_STOP_ID_2, SLOT_3_STOP_ID_3}},
  {(uint16_t)(SLOT_4_START_HOUR * 60 + SLOT_4_START_MINUTE),
   {SLOT_4_ROUTE_1, SLOT_4_ROUTE_2, SLOT_4_ROUTE_3},
   {SLOT_4_STOP_ID_1, SLOT_4_STOP_ID_2, SLOT_4_STOP_ID_3}}
};
int activeScheduleSlot = -1;
float localTemp = NAN, localHumidity = NAN;
const char *weatherSource = "NO DATA";
WeatherVisual weatherVisual = WEATHER_PARTLY;
WarningState warningState = NORMAL;
uint32_t lastDataRefresh = 0;
uint32_t lastWeatherRefresh = 0;
int lastDrawnMinute = -1;

uint8_t bcdToDec(uint8_t v) { return (v >> 4) * 10 + (v & 15); }
uint8_t decToBcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

bool applyRouteSchedule(const tm &now) {
  int slotCount = constrain((int)SCHEDULE_SLOT_COUNT, 1, 4);
  int minuteOfDay = now.tm_hour * 60 + now.tm_min;
  int selected = -1;
  int bestStart = -1;

  for (int i = 0; i < slotCount; ++i) {
    if (scheduleSlots[i].startMinute <= minuteOfDay &&
        scheduleSlots[i].startMinute > bestStart) {
      selected = i;
      bestStart = scheduleSlots[i].startMinute;
    }
  }
  // Before the first start time, continue using the final slot from yesterday.
  if (selected < 0) {
    for (int i = 0; i < slotCount; ++i) {
      if (scheduleSlots[i].startMinute > bestStart) {
        selected = i;
        bestStart = scheduleSlots[i].startMinute;
      }
    }
  }
  if (selected == activeScheduleSlot) return false;

  activeScheduleSlot = selected;
  for (int i = 0; i < 3; ++i) {
    routes[i].route = scheduleSlots[selected].route[i];
    routes[i].stopId = scheduleSlots[selected].stopId[i];
    routes[i].eta[0] = routes[i].eta[1] = routes[i].eta[2] = -1;
  }
  Serial.printf("Route schedule switched to slot %d at %02d:%02d\n",
                selected + 1, now.tm_hour, now.tm_min);
  return true;
}

bool readShtc3(float &temperature, float &humidity) {
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write(0x35); Wire.write(0x17);
  if (Wire.endTransmission() != 0) return false;
  delay(1);
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write(0x78); Wire.write(0x66);
  if (Wire.endTransmission() != 0) return false;
  delay(15);
  if (Wire.requestFrom(SHTC3_ADDR, (uint8_t)6) != 6) return false;
  uint16_t rawT = (Wire.read() << 8) | Wire.read(); Wire.read();
  uint16_t rawH = (Wire.read() << 8) | Wire.read(); Wire.read();
  temperature = -45.0f + 175.0f * rawT / 65535.0f;
  humidity = 100.0f * rawH / 65535.0f;
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write(0xB0); Wire.write(0x98);
  Wire.endTransmission();
  return true;
}

bool readRtc(tm &v) {
  Wire.beginTransmission(RTC_ADDR); Wire.write(0x04);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(RTC_ADDR, (uint8_t)7) != 7) return false;
  v = {};
  v.tm_sec = bcdToDec(Wire.read() & 0x7F);
  v.tm_min = bcdToDec(Wire.read() & 0x7F);
  v.tm_hour = bcdToDec(Wire.read() & 0x3F);
  v.tm_mday = bcdToDec(Wire.read() & 0x3F);
  v.tm_wday = bcdToDec(Wire.read() & 7);
  v.tm_mon = bcdToDec(Wire.read() & 0x1F) - 1;
  v.tm_year = bcdToDec(Wire.read()) + 100;
  return v.tm_year >= 120;
}

void writeRtc(const tm &v) {
  Wire.beginTransmission(RTC_ADDR); Wire.write(0x04);
  Wire.write(decToBcd(v.tm_sec)); Wire.write(decToBcd(v.tm_min));
  Wire.write(decToBcd(v.tm_hour)); Wire.write(decToBcd(v.tm_mday));
  Wire.write(decToBcd(v.tm_wday)); Wire.write(decToBcd(v.tm_mon + 1));
  Wire.write(decToBcd((v.tm_year + 1900) % 100)); Wire.endTransmission();
}

bool getJson(const String &url, JsonDocument &json) {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setConnectTimeout(10000); http.setTimeout(15000);
  http.useHTTP10(true);
  Serial.printf("HTTP GET: %s\n", url.c_str());
  if (!http.begin(client, url)) {
    Serial.println("HTTP begin failed");
    return false;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "blackjack280-hkbus-eta-native/0.2");
  int code = http.GET();
  Serial.printf("HTTP status: %d\n", code);
  if (code != HTTP_CODE_OK) {
    if (code < 0) Serial.printf("HTTP error: %s\n", http.errorToString(code).c_str());
    http.end();
    return false;
  }
  String payload = http.getString();
  Serial.printf("HTTP payload: %u bytes\n", (unsigned)payload.length());
  http.end();
  if (!payload.length()) {
    Serial.println("HTTP payload is empty");
    return false;
  }
  DeserializationError error = deserializeJson(json, payload);
  if (error) Serial.printf("JSON error: %s\n", error.c_str());
  return !error;
}

// Convert a Hong Kong local calendar time to Unix UTC without relying on
// timegm(), which is not exposed by every Arduino-ESP32 toolchain.
time_t hongKongTimeToUtc(const tm &v) {
  int year = v.tm_year + 1900;
  unsigned month = v.tm_mon + 1;
  const unsigned day = v.tm_mday;
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  const int64_t daysSinceEpoch = static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
  return static_cast<time_t>(daysSinceEpoch * 86400LL + v.tm_hour * 3600LL +
                             v.tm_min * 60LL + v.tm_sec - 8 * 3600LL);
}

time_t parseEta(const char *iso) {
  if (!iso || strlen(iso) < 19) return 0;
  tm v = {};
  if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &v.tm_year, &v.tm_mon, &v.tm_mday,
             &v.tm_hour, &v.tm_min, &v.tm_sec) != 6) return 0;
  v.tm_year -= 1900; v.tm_mon--;
  return hongKongTimeToUtc(v);
}

void fetchRoute(RouteData &route) {
  for (int &v : route.eta) v = -1;
  if (!strlen(route.stopId) || WiFi.status() != WL_CONNECTED) return;
  JsonDocument json;
  String url = "https://data.etabus.gov.hk/v1/transport/kmb/eta/" +
               String(route.stopId) + "/" + route.route + "/1";
  if (!getJson(url, json)) return;
  int index = 0;
  time_t now = time(nullptr);
  for (JsonObjectConst item : json["data"].as<JsonArrayConst>()) {
    if (index == 2) break;
    time_t arrival = parseEta(item["eta"]);
    long seconds = arrival - now;
    if (arrival && seconds >= -30) route.eta[index++] = max(0L, (seconds + 30) / 60);
  }
}

bool fetchOutdoorWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;
  JsonDocument json;
  if (!getJson("https://data.weather.gov.hk/weatherAPI/opendata/weather.php"
               "?dataType=rhrread&lang=tc", json)) return false;

  bool updated = false;
  for (JsonObjectConst item : json["temperature"]["data"].as<JsonArrayConst>()) {
    const char *place = item["place"] | "";
    if (strcmp(place, HKO_TEMPERATURE_STATION) == 0) {
      localTemp = item["value"].as<float>();
      updated = true;
      break;
    }
  }
  JsonArrayConst humidity = json["humidity"]["data"].as<JsonArrayConst>();
  if (!humidity.isNull() && humidity.size() > 0) {
    localHumidity = humidity[0]["value"].as<float>();
    updated = true;
  }
  if (json["icon"].is<JsonArrayConst>() && json["icon"].size() > 0) {
    int icon = json["icon"][0].as<int>();
    if (icon == 50 || (icon >= 70 && icon <= 75) || icon == 77) weatherVisual = WEATHER_CLEAR;
    else if (icon == 51 || icon == 52) weatherVisual = WEATHER_PARTLY;
    else if (icon == 53 || icon == 54 || (icon >= 62 && icon <= 64)) weatherVisual = WEATHER_RAIN;
    else if (icon == 65) weatherVisual = WEATHER_THUNDER;
    else if (icon == 60 || icon == 61 || icon == 76 || (icon >= 83 && icon <= 85)) weatherVisual = WEATHER_CLOUDY;
  }
  if (updated) {
    weatherSource = "HKO";
    Serial.printf("HKO outdoor: %.1f C, %.1f %%\n", localTemp, localHumidity);
  } else {
    Serial.printf("HKO station not found: %s\n", HKO_TEMPERATURE_STATION);
  }
  return updated;
}

bool fetchOpenMeteoWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;
  JsonDocument json;
  // Sheung Shui coordinates; no account or API key is required.
  if (!getJson("https://api.open-meteo.com/v1/forecast"
               "?latitude=22.501&longitude=114.128"
               "&current=temperature_2m,relative_humidity_2m,weather_code"
               "&timezone=Asia%2FHong_Kong&forecast_days=1", json)) return false;
  JsonObjectConst current = json["current"].as<JsonObjectConst>();
  if (current.isNull() || current["temperature_2m"].isNull() ||
      current["relative_humidity_2m"].isNull()) return false;
  localTemp = current["temperature_2m"].as<float>();
  localHumidity = current["relative_humidity_2m"].as<float>();
  int code = current["weather_code"] | 2;
  if (code == 0) weatherVisual = WEATHER_CLEAR;
  else if (code == 1 || code == 2) weatherVisual = WEATHER_PARTLY;
  else if (code == 3 || code == 45 || code == 48) weatherVisual = WEATHER_CLOUDY;
  else if (code >= 95) weatherVisual = WEATHER_THUNDER;
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) weatherVisual = WEATHER_RAIN;
  else weatherVisual = WEATHER_CLOUDY;
  weatherSource = "OPEN-METEO";
  Serial.printf("Open-Meteo outdoor: %.1f C, %.1f %%\n", localTemp, localHumidity);
  return true;
}

void fetchWarnings() {
  warningState = NORMAL;
  JsonDocument json;
  if (WiFi.status() != WL_CONNECTED ||
      !getJson("https://data.weather.gov.hk/weatherAPI/opendata/weather.php"
               "?dataType=warnsum&lang=tc", json)) return;
  String rain = json["WRAIN"]["code"] | "";
  if (rain == "WRAINB") { warningState = RAIN_BLACK; return; }
  if (rain == "WRAINR") { warningState = RAIN_RED; return; }
  if (rain == "WRAINA") { warningState = RAIN_AMBER; return; }
  String tc = json["WTCSGNL"]["code"] | "";
  if (tc == "TC10") warningState = TC10;
  else if (tc == "TC9") warningState = TC9;
  else if (tc.startsWith("TC8")) warningState = TC8;
  else if (tc == "TC3") warningState = TC3;
  else if (tc == "TC1") warningState = TC1;
}

void centered(const char *text, int x, int width, int y) {
  gfx->drawUTF8(x + max(0, (width - gfx->getUTF8Width(text)) / 2), y, text);
}

void drawBoldUTF8(int x, int y, const char *text) {
  gfx->drawUTF8(x, y, text);
  gfx->drawUTF8(x + 1, y, text);
}

void drawBoldStr(int x, int y, const char *text) {
  gfx->drawStr(x, y, text);
  gfx->drawStr(x + 1, y, text);
}

void drawWeatherIcon(int cx, int cy) {
  if (warningState != NORMAL) {
    gfx->drawCircle(cx, cy, 27); gfx->drawCircle(cx, cy, 26);
    const char *label = warningState == RAIN_AMBER ? "黃雨" :
      warningState == RAIN_RED ? "紅雨" : warningState == RAIN_BLACK ? "黑雨" :
      warningState == TC1 ? "T1" : warningState == TC3 ? "T3" :
      warningState == TC8 ? "T8" : warningState == TC9 ? "T9" : "T10";
    gfx->setFont(u8g2_font_unifont_t_chinese3);
    centered(label, cx - 27, 54, cy + 5);
    return;
  }
  if (weatherVisual == WEATHER_CLEAR || weatherVisual == WEATHER_PARTLY) {
    int sx = weatherVisual == WEATHER_CLEAR ? cx : cx - 10;
    int sy = weatherVisual == WEATHER_CLEAR ? cy : cy - 9;
    gfx->drawDisc(sx, sy, 12);
    for (int a = 0; a < 360; a += 45) {
      float r = a * PI / 180.0f;
      gfx->drawLine(sx + cos(r) * 17, sy + sin(r) * 17,
                    sx + cos(r) * 22, sy + sin(r) * 22);
    }
    if (weatherVisual == WEATHER_CLEAR) return;
  }

  gfx->setDrawColor(0);
  gfx->drawDisc(cx + 7, cy + 8, 18); gfx->drawDisc(cx - 9, cy + 12, 13);
  gfx->setDrawColor(1);
  gfx->drawCircle(cx + 7, cy + 8, 18); gfx->drawCircle(cx - 9, cy + 12, 13);
  gfx->drawHLine(cx - 22, cy + 24, 43);

  if (weatherVisual == WEATHER_RAIN) {
    gfx->drawLine(cx - 13, cy + 29, cx - 17, cy + 36);
    gfx->drawLine(cx, cy + 29, cx - 4, cy + 36);
    gfx->drawLine(cx + 13, cy + 29, cx + 9, cy + 36);
  } else if (weatherVisual == WEATHER_THUNDER) {
    gfx->drawLine(cx + 2, cy + 27, cx - 5, cy + 38);
    gfx->drawLine(cx - 5, cy + 38, cx + 2, cy + 36);
    gfx->drawLine(cx + 2, cy + 36, cx - 3, cy + 45);
  }
}

void drawDashboard() {
  tm now = {}; getLocalTime(&now, 50);
  gfx->clearBuffer(); gfx->setDrawColor(1);
  gfx->drawFrame(0, 0, SCREEN_W, SCREEN_H);
  gfx->drawVLine(230, 0, 300); gfx->drawHLine(0, 40, 230);
  gfx->drawHLine(0, 126, 230); gfx->drawHLine(0, 212, 230);
  gfx->drawVLine(90, 40, 260); gfx->drawVLine(160, 40, 260);

  gfx->setFont(u8g2_font_7x14B_tf); gfx->drawStr(16, 27, "ROUTE");
  gfx->setFont(u8g2_font_helvB14_tf);
  centered("1st", 90, 70, 27); centered("2nd", 160, 70, 27);

  int rowTop[3] = {40, 126, 212};
  for (int r = 0; r < 3; ++r) {
    int y = rowTop[r];
    gfx->drawRBox(6, y + 17, 78, 52, 5); gfx->setDrawColor(0);
    gfx->setFont(u8g2_font_helvB18_tf); centered(routes[r].route, 6, 78, y + 51);
    gfx->setDrawColor(1);
    for (int i = 0; i < 2; ++i) {
      char etaText[6];
      if (routes[r].eta[i] < 0) strcpy(etaText, "--");
      else snprintf(etaText, sizeof(etaText), "%d", routes[r].eta[i]);
      gfx->setFont(u8g2_font_logisoso28_tn); centered(etaText, 90 + i * 70, 54, y + 55);
      if (routes[r].eta[i] >= 0) {
        gfx->setFont(u8g2_font_unifont_t_chinese3); gfx->drawUTF8(143 + i * 70, y + 55, "分");
      }
    }
  }

  uint8_t battery = Adc_GetBatteryLevel();
  gfx->drawFrame(331, 12, 26, 12); gfx->drawBox(357, 15, 3, 6);
  gfx->drawBox(334, 15, map(battery, 0, 100, 0, 20), 6);
  char text[28]; gfx->setFont(u8g2_font_7x14B_tf);
  snprintf(text, sizeof(text), "%u%%", battery); gfx->drawStr(365, 23, text);

  gfx->setFont(u8g2_font_logisoso24_tn);
  snprintf(text, sizeof(text), "%02d/%02d", now.tm_mday, now.tm_mon + 1); centered(text, 230, 170, 66);
  static const char *week[] = {"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
  gfx->drawRBox(283, 76, 64, 26, 4); gfx->setDrawColor(0);
  gfx->setFont(u8g2_font_unifont_t_chinese3); centered(week[now.tm_wday], 283, 64, 95);
  gfx->setDrawColor(1); gfx->setFont(u8g2_font_logisoso38_tn);
  snprintf(text, sizeof(text), "%02d:%02d", now.tm_hour, now.tm_min); centered(text, 230, 170, 148);
  gfx->drawHLine(240, 151, 150);

  drawWeatherIcon(270, 217);
  gfx->setFont(u8g2_font_unifont_t_chinese3);
  gfx->drawUTF8(307, 178, "上水");
  char number[10];
  gfx->setFont(u8g2_font_helvB18_tf);
  if (isnan(localTemp)) strcpy(number, "--");
  else if (strcmp(weatherSource, "OPEN-METEO") == 0)
    snprintf(number, sizeof(number), "%.1f", localTemp);
  else
    snprintf(number, sizeof(number), "%.0f", localTemp);
  drawBoldStr(307, 205, number);
  int unitX = 309 + gfx->getStrWidth(number);
  gfx->setFont(u8g2_font_7x14_tf);
  gfx->drawStr(unitX, 205, "C");

  gfx->setFont(u8g2_font_7x14_tf);
  gfx->drawStr(307, 232, "RH");
  gfx->setFont(u8g2_font_helvB12_tf);
  if (isnan(localHumidity)) strcpy(number, "--");
  else snprintf(number, sizeof(number), "%.0f", localHumidity);
  drawBoldStr(329, 232, number);
  unitX = 331 + gfx->getStrWidth(number);
  gfx->setFont(u8g2_font_7x14_tf);
  gfx->drawStr(unitX, 232, "%");

  gfx->setFont(u8g2_font_5x7_tf);
  gfx->drawStr(307, 256, warningState == NORMAL ? weatherSource : "WARNING");
  if (!strlen(WIFI_SSID)) { gfx->setFont(u8g2_font_5x7_tf); gfx->drawStr(240, 292, "EDIT config.h"); }
  gfx->sendBuffer();
}

void syncClock() {
  configTime(8 * 3600, 0, "time.cloudflare.com", "pool.ntp.org", "time.google.com");
  tm v = {};
  if (getLocalTime(&v, 8000)) { writeRtc(v); return; }
  if (readRtc(v)) {
    time_t utc = hongKongTimeToUtc(v);
    timeval tv = {utc, 0}; settimeofday(&tv, nullptr);
  }
}

void refreshData() {
  float boardTemp = NAN, boardHumidity = NAN;
  if (readShtc3(boardTemp, boardHumidity)) {
    Serial.printf("SHTC3 board: %.1f C, %.1f %%\n", boardTemp, boardHumidity);
  }
  if (WiFi.status() == WL_CONNECTED) {
    for (RouteData &route : routes) fetchRoute(route);
    if (lastWeatherRefresh == 0 || millis() - lastWeatherRefresh >= WEATHER_REFRESH_MS) {
      if (!fetchOutdoorWeather()) {
        Serial.println("HKO unavailable; trying Open-Meteo fallback");
        if (!fetchOpenMeteoWeather()) weatherSource = "NO DATA";
      }
      fetchWarnings();
      lastWeatherRefresh = millis();
    }
  }
  lastDataRefresh = millis(); drawDashboard();
}

void setup() {
  Serial.begin(115200); Wire.begin(14, 13); Adc_PortInit();
  display.begin(U8G2_R1); gfx = display.getU8g2();
  if (strlen(WIFI_SSID)) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    Serial.printf("Connecting to Wi-Fi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) delay(250);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("Wi-Fi connected. IP=%s RSSI=%d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      Serial.printf("Wi-Fi failed. status=%d\n", WiFi.status());
    }
  } else {
    Serial.println("Wi-Fi not configured: edit config.h");
  }
  syncClock();
  tm initialTime = {};
  if (getLocalTime(&initialTime, 100)) applyRouteSchedule(initialTime);
  refreshData();
}

void loop() {
  tm now = {};
  if (getLocalTime(&now, 10) && now.tm_min != lastDrawnMinute) {
    lastDrawnMinute = now.tm_min;
    if (applyRouteSchedule(now)) refreshData();
    else drawDashboard();
  }
  if (millis() - lastDataRefresh >= DATA_REFRESH_MS) refreshData();
  delay(250);
}
