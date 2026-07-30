#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <time.h>

namespace {

constexpr char kSetupSsid[] = "HKBus-ETA-Setup";
constexpr char kSetupPassword[] = "hkbuseta";
constexpr uint32_t kRefreshIntervalMs = 30000;
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr uint16_t kHttpPort = 80;
constexpr size_t kRouteCount = 3;

struct RouteConfig {
  String route;
  String stopId;
};

struct AppConfig {
  String ssid;
  String password;
  String stopLabel = "上水";
  RouteConfig routes[kRouteCount] = {
    {"A43", ""},
    {"278A", ""},
    {"277X", ""}
  };
};

Preferences preferences;
WebServer server(kHttpPort);
AppConfig config;
String cachedStatus = R"({"configured":false,"online":false,"routes":[],"weather":{}})";
uint32_t lastRefresh = 0;
bool setupMode = false;
bool refreshInProgress = false;

String contentTypeFor(const String& path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".json") || path.endsWith(".webmanifest")) {
    return "application/json; charset=utf-8";
  }
  return "text/plain; charset=utf-8";
}

bool serveFile(String path) {
  if (path == "/") path = "/index.html";
  if (!LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  server.streamFile(file, contentTypeFor(path));
  file.close();
  return true;
}

void loadConfig() {
  preferences.begin("hkbus-eta", true);
  config.ssid = preferences.getString("ssid", "");
  config.password = preferences.getString("password", "");
  config.stopLabel = preferences.getString("stopLabel", "上水");
  for (size_t i = 0; i < kRouteCount; ++i) {
    const String routeKey = "route" + String(i);
    const String stopKey = "stop" + String(i);
    config.routes[i].route =
      preferences.getString(routeKey.c_str(), config.routes[i].route);
    config.routes[i].stopId =
      preferences.getString(stopKey.c_str(), "");
  }
  preferences.end();
}

void saveConfig(const JsonDocument& body) {
  preferences.begin("hkbus-eta", false);
  const String ssid = body["ssid"] | "";
  const String password = body["password"] | "";
  const String stopLabel = body["stopLabel"] | "上水";

  if (!ssid.isEmpty()) preferences.putString("ssid", ssid);
  if (!password.isEmpty()) preferences.putString("password", password);
  preferences.putString("stopLabel", stopLabel);

  JsonArrayConst routes = body["routes"].as<JsonArrayConst>();
  for (size_t i = 0; i < kRouteCount && i < routes.size(); ++i) {
    const String routeKey = "route" + String(i);
    const String stopKey = "stop" + String(i);
    String route = routes[i]["route"] | "";
    route.toUpperCase();
    const String stopId = routes[i]["stopId"] | "";
    preferences.putString(routeKey.c_str(), route);
    preferences.putString(stopKey.c_str(), stopId);
  }
  preferences.end();
}

bool isConfigured() {
  if (config.ssid.isEmpty()) return false;
  for (const auto& route : config.routes) {
    if (route.route.isEmpty() || route.stopId.isEmpty()) return false;
  }
  return true;
}

void startSetupAccessPoint() {
  setupMode = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(kSetupSsid, kSetupPassword);
  Serial.printf("Setup Wi-Fi: %s / %s\n", kSetupSsid, kSetupPassword);
  Serial.printf("Open http://%s/\n", WiFi.softAPIP().toString().c_str());
}

bool connectWifi() {
  if (config.ssid.isEmpty()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < kWifiTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) return false;

  Serial.printf("Connected: http://%s/\n", WiFi.localIP().toString().c_str());
  configTime(8 * 3600, 0, "time.cloudflare.com", "pool.ntp.org", "time.google.com");
  return true;
}

bool getJson(const String& url, JsonDocument& result) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
  client.setInsecure();  // Public read-only APIs; avoids failures when device clock is not ready.
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(8000);
  if (!http.begin(client, url)) return false;
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "blackjack280-hkbus-eta/1.0");
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("GET %s -> %d\n", url.c_str(), status);
    http.end();
    return false;
  }
  const DeserializationError error = deserializeJson(result, http.getStream());
  http.end();
  if (error) {
    Serial.printf("JSON error: %s\n", error.c_str());
    return false;
  }
  return true;
}

time_t parseHongKongIsoTime(const char* value) {
  if (value == nullptr || strlen(value) < 19) return 0;
  tm parsed = {};
  if (sscanf(value, "%d-%d-%dT%d:%d:%d",
             &parsed.tm_year, &parsed.tm_mon, &parsed.tm_mday,
             &parsed.tm_hour, &parsed.tm_min, &parsed.tm_sec) != 6) {
    return 0;
  }
  parsed.tm_year -= 1900;
  parsed.tm_mon -= 1;
  // timegm interprets fields as UTC. Subtract HKT's UTC+8 offset afterwards.
  return timegm(&parsed) - (8 * 3600);
}

void appendEtas(JsonObject target, const RouteConfig& routeConfig) {
  target["route"] = routeConfig.route;
  JsonArray etaOut = target["eta"].to<JsonArray>();

  JsonDocument response;
  const String url = "https://data.etabus.gov.hk/v1/transport/kmb/eta/" +
                     routeConfig.stopId + "/" + routeConfig.route + "/1";
  if (!getJson(url, response)) return;

  const time_t now = time(nullptr);
  for (JsonObjectConst item : response["data"].as<JsonArrayConst>()) {
    if (etaOut.size() >= 3) break;
    const char* eta = item["eta"];
    const time_t arrival = parseHongKongIsoTime(eta);
    if (arrival <= 0 || now <= 0) continue;
    const long seconds = static_cast<long>(arrival - now);
    if (seconds < -30) continue;
    etaOut.add(max(0L, (seconds + 30) / 60));
  }
}

bool readWeather(JsonObject weather) {
  JsonDocument current;
  if (!getJson(
        "https://data.weather.gov.hk/weatherAPI/opendata/weather.php"
        "?dataType=rhrread&lang=tc",
        current)) {
    return false;
  }

  bool foundSheungShui = false;
  for (JsonObjectConst station : current["temperature"]["data"].as<JsonArrayConst>()) {
    const String place = station["place"] | "";
    if (place == "上水") {
      weather["temperature"] = station["value"];
      foundSheungShui = true;
      break;
    }
  }
  if (!foundSheungShui && current["temperature"]["data"].size() > 0) {
    weather["temperature"] = current["temperature"]["data"][0]["value"];
  }
  if (current["humidity"]["data"].size() > 0) {
    weather["humidity"] = current["humidity"]["data"][0]["value"];
  }
  if (current["icon"].size() > 0) {
    weather["icon"] = current["icon"][0];
  }
  weather["state"] = "normal";
  weather["label"] = "上水天氣";

  JsonDocument warnings;
  if (!getJson(
        "https://data.weather.gov.hk/weatherAPI/opendata/weather.php"
        "?dataType=warnsum&lang=tc",
        warnings)) {
    return true;
  }

  // Rainstorm takes visual priority, followed by the highest tropical cyclone signal.
  const String rainCode = warnings["WRAIN"]["code"] | "";
  if (rainCode == "WRAINB") {
    weather["state"] = "rain-black";
    weather["label"] = "黑色暴雨警告";
    return true;
  }
  if (rainCode == "WRAINR") {
    weather["state"] = "rain-red";
    weather["label"] = "紅色暴雨警告";
    return true;
  }
  if (rainCode == "WRAINA") {
    weather["state"] = "rain-amber";
    weather["label"] = "黃色暴雨警告";
    return true;
  }

  const String tcCode = warnings["WTCSGNL"]["code"] | "";
  if (tcCode == "TC10") {
    weather["state"] = "tc-10";
    weather["label"] = "十號風球";
  } else if (tcCode == "TC9") {
    weather["state"] = "tc-9";
    weather["label"] = "九號風球";
  } else if (tcCode.startsWith("TC8")) {
    weather["state"] = "tc-8";
    weather["label"] = "八號風球";
  } else if (tcCode == "TC3") {
    weather["state"] = "tc-3";
    weather["label"] = "三號風球";
  } else if (tcCode == "TC1") {
    weather["state"] = "tc-1";
    weather["label"] = "一號風球";
  }
  return true;
}

void refreshData() {
  if (refreshInProgress || WiFi.status() != WL_CONNECTED || !isConfigured()) return;
  refreshInProgress = true;
  lastRefresh = millis();

  JsonDocument status;
  status["configured"] = true;
  status["online"] = true;
  status["stopLabel"] = config.stopLabel;
  status["uptime"] = millis() / 1000;
  status["rssi"] = WiFi.RSSI();
  JsonArray routes = status["routes"].to<JsonArray>();

  bool hasAnyEta = false;
  for (const auto& routeConfig : config.routes) {
    JsonObject route = routes.add<JsonObject>();
    appendEtas(route, routeConfig);
    if (route["eta"].size() > 0) hasAnyEta = true;
    server.handleClient();
  }

  JsonObject weather = status["weather"].to<JsonObject>();
  const bool hasWeather = readWeather(weather);
  status["online"] = hasAnyEta || hasWeather;

  String nextStatus;
  serializeJson(status, nextStatus);
  cachedStatus = nextStatus;
  refreshInProgress = false;
}

void handleStatus() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", cachedStatus);
}

void handleGetConfig() {
  JsonDocument output;
  output["ssid"] = config.ssid;
  output["stopLabel"] = config.stopLabel;
  JsonArray routes = output["routes"].to<JsonArray>();
  for (const auto& configuredRoute : config.routes) {
    JsonObject route = routes.add<JsonObject>();
    route["route"] = configuredRoute.route;
    route["stopId"] = configuredRoute.stopId;
  }
  String body;
  serializeJson(output, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", body);
}

void handlePostConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", R"({"error":"missing JSON body"})");
    return;
  }
  JsonDocument body;
  const DeserializationError error = deserializeJson(body, server.arg("plain"));
  if (error || !body["routes"].is<JsonArray>()) {
    server.send(400, "application/json", R"({"error":"invalid configuration"})");
    return;
  }
  saveConfig(body);
  server.send(200, "application/json", R"({"ok":true,"restarting":true})");
  delay(500);
  ESP.restart();
}

void configureServer() {
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.onNotFound([]() {
    if (!serveFile(server.uri())) {
      // Captive-portal friendly fallback: unknown paths return the setup UI.
      serveFile("/index.html");
    }
  });
  server.begin();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nHKBus ETA starting");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }
  loadConfig();
  if (!connectWifi()) startSetupAccessPoint();
  configureServer();
  refreshData();
}

void loop() {
  server.handleClient();
  if (!setupMode && WiFi.status() != WL_CONNECTED) {
    if (!connectWifi()) startSetupAccessPoint();
  }
  if (!setupMode && millis() - lastRefresh >= kRefreshIntervalMs) {
    refreshData();
  }
  delay(2);
}
