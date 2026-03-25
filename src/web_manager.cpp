#include "web_manager.h"

#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include "app_config.h"
#include "io_manager.h"
#include "logger.h"

namespace {
WebServer server(80);
String wifiIp = "0.0.0.0";
InputSnapshot latestInput;
EnvSnapshot latestEnv;
bool serverStarted = false;
uint32_t lastReconnectAttemptMs = 0;
bool ntpSynced = false;
uint32_t lastNtpSyncAttemptMs = 0;
String preferredSsid = AppConfig::Wifi::SSID;
String preferredPassword = AppConfig::Wifi::PASSWORD;

constexpr int MAX_SCANNED_WIFI = 20;
int scannedCount = 0;
String scannedSsid[MAX_SCANNED_WIFI];
int32_t scannedRssi[MAX_SCANNED_WIFI];
bool scannedOpen[MAX_SCANNED_WIFI];

String formatNow() {
  if (!ntpSynced) {
    return "--:--:--";
  }
  time_t now = time(nullptr);
  struct tm timeInfo;
  localtime_r(&now, &timeInfo);

  char out[24];
  strftime(out, sizeof(out), "%Y-%m-%d %H:%M:%S", &timeInfo);
  return String(out);
}

void syncNtpTime() {
  lastNtpSyncAttemptMs = millis();
  setenv("TZ", "CST-8", 1);
  tzset();
  configTzTime("CST-8", "ntp.aliyun.com", "ntp.ntsc.ac.cn", "pool.ntp.org");

  const uint32_t start = millis();
  while (millis() - start < 10000) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      ntpSynced = true;
      Logger::logPrintf("[BOOT] NTP synced: %s\n", formatNow().c_str());
      return;
    }
    delay(200);
  }

  ntpSynced = false;
  Logger::logPrintf("[BOOT] NTP sync timeout\n");
}

bool connectWifiInternal(const String &ssid, const String &password, bool updatePreferred) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), password.c_str());
  Logger::logPrintf("[NET] connecting Wi-Fi: %s\n", ssid.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < AppConfig::Wifi::CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
    Serial0.print(".");
  }
  Serial.println();
  Serial0.println();

  if (WiFi.status() != WL_CONNECTED) {
    wifiIp = "0.0.0.0";
    Logger::logPrintf("[NET] Wi-Fi connect failed: %s\n", ssid.c_str());
    return false;
  }

  if (updatePreferred) {
    preferredSsid = ssid;
    preferredPassword = password;
  }

  wifiIp = WiFi.localIP().toString();
  Logger::logPrintf("[NET] Wi-Fi connected: %s IP=%s\n", ssid.c_str(), wifiIp.c_str());
  syncNtpTime();
  return true;
}

void handleRoot() {
  Logger::logPrintf("[HTTP] / from %s\n", server.client().remoteIP().toString().c_str());
  String html = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Lab Management</title>
  <style>
    :root {
      --bg: #0b0d10;
      --panel: #10141a;
      --line: #e9edf3;
      --muted: #9ba7b7;
      --ink: #f8fbff;
      --invert-bg: #f2f6fc;
      --invert-ink: #0d1117;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      background: radial-gradient(1200px 600px at 20% -10%, #1a2230 0%, var(--bg) 50%);
      color: var(--ink);
      font-family: "JetBrains Mono", "SF Mono", "Fira Code", "Consolas", monospace;
      padding: 18px;
    }
    .wrap { max-width: 860px; margin: 0 auto; }
    .titlebar {
      background: var(--invert-bg);
      color: var(--invert-ink);
      border: 2px solid var(--line);
      padding: 10px 12px;
      font-weight: 700;
      letter-spacing: .4px;
    }
    .time {
      border: 2px solid var(--line);
      border-top: 0;
      background: var(--panel);
      padding: 16px 12px 14px;
      margin-bottom: 14px;
    }
    .time-main { font-size: clamp(28px, 8vw, 54px); line-height: 1; font-weight: 800; }
    .time-sub { margin-top: 8px; color: var(--muted); font-size: 13px; }
    .grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 12px;
    }
    .card {
      border: 2px solid var(--line);
      background: var(--panel);
      min-height: 110px;
    }
    .card-h {
      background: var(--invert-bg);
      color: var(--invert-ink);
      border-bottom: 2px solid var(--line);
      padding: 6px 10px;
      font-size: 13px;
      font-weight: 700;
    }
    .card-b { padding: 10px; font-size: 14px; line-height: 1.65; }
    .kv { display: flex; justify-content: space-between; gap: 8px; }
    .muted { color: var(--muted); }
    .ctrl { margin-top: 8px; }
    .ctrl label { display: block; font-size: 13px; margin-bottom: 4px; color: var(--muted); }
    input[type=range] { width: 100%; accent-color: #ffffff; }
    .btn {
      width: 100%;
      margin-top: 10px;
      border: 2px solid var(--line);
      background: transparent;
      color: var(--ink);
      padding: 8px 10px;
      font-family: inherit;
      font-weight: 700;
      cursor: pointer;
    }
    .btn:hover { background: #1a2230; }
    .links { margin-top: 12px; font-size: 13px; color: var(--muted); }
    .links a { color: var(--ink); text-decoration: none; border-bottom: 1px dashed var(--line); }
    @media (max-width: 720px) { .grid { grid-template-columns: 1fr; } }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="titlebar">LAB MANAGEMENT / OLED STYLE UI</div>
    <div class="time">
      <div id="t" class="time-main">--:--:--</div>
      <div class="time-sub">IP: <span id="ip">__IP__</span></div>
    </div>

    <div class="grid">
      <section class="card">
        <div class="card-h">SENSOR</div>
        <div class="card-b">
          <div class="kv"><span>Temp</span><strong id="temp">--</strong></div>
          <div class="kv"><span>Humidity</span><strong id="hum">--</strong></div>
          <div class="kv"><span>Pressure</span><strong id="pres">--</strong></div>
        </div>
      </section>

      <section class="card">
        <div class="card-h">INPUTS</div>
        <div class="card-b">
          <div class="kv"><span>Fire</span><strong id="fire">0</strong></div>
          <div class="kv"><span>Human</span><strong id="human">0</strong></div>
          <div class="kv"><span>K1 K2 K3 K4</span><strong id="keys">0 0 0 0</strong></div>
        </div>
      </section>

      <section class="card" style="grid-column: 1 / -1;">
        <div class="card-h">LIGHT CONTROL</div>
        <div class="card-b">
          <div class="ctrl">
            <label>Brightness <span id="bv" class="muted">128</span></label>
            <input id="b" type="range" min="0" max="255" value="128">
          </div>
          <div class="ctrl">
            <label>Warmth <span id="wv" class="muted">128</span></label>
            <input id="w" type="range" min="0" max="255" value="128">
          </div>
          <button class="btn" onclick="setLight()">APPLY LIGHT</button>
        </div>
      </section>
    </div>

    <div class="links">
      <a href="/api/status">/api/status</a> |
      <a href="/ping">/ping</a>
    </div>
  </div>

  <script>
    const b = document.getElementById('b');
    const w = document.getElementById('w');
    const bv = document.getElementById('bv');
    const wv = document.getElementById('wv');
    let dirty = false;
    let lastAppliedB = null;
    let lastAppliedW = null;
    b.addEventListener('input', () => { bv.textContent = b.value; dirty = true; });
    w.addEventListener('input', () => { wv.textContent = w.value; dirty = true; });

    function fmt(v, unit) { return v == null ? '--' : `${v}${unit}`; }

    async function update() {
      const r = await fetch('/api/status');
      if (!r.ok) return;
      const j = await r.json();
      document.getElementById('t').textContent = j.time || '--:--:--';
      document.getElementById('ip').textContent = j.ip || '--';
      document.getElementById('temp').textContent = fmt(j.temp_c, ' C');
      document.getElementById('hum').textContent = fmt(j.humidity_pct, ' %');
      document.getElementById('pres').textContent = fmt(j.pressure_hpa, ' hPa');
      document.getElementById('fire').textContent = j.fire ?? 0;
      document.getElementById('human').textContent = j.human ?? 0;
      document.getElementById('keys').textContent = `${j.k1 ?? 0} ${j.k2 ?? 0} ${j.k3 ?? 0} ${j.k4 ?? 0}`;

      const sb = j.light_brightness ?? 128;
      const sw = j.light_warmth ?? 128;
      if (lastAppliedB === null) {
        lastAppliedB = sb;
        lastAppliedW = sw;
      }

      // 如果设备端（按键/OLED）改了灯光值，解除本地未提交锁并跟随设备状态
      const externalChanged = (sb !== lastAppliedB) || (sw !== lastAppliedW);
      if (externalChanged) {
        dirty = false;
      }

      if (!dirty) {
        b.value = sb;
        w.value = sw;
      }
      bv.textContent = b.value;
      wv.textContent = w.value;
      lastAppliedB = sb;
      lastAppliedW = sw;
    }

    async function setLight() {
      await fetch(`/api/light/set?brightness=${b.value}&warmth=${w.value}`);
      dirty = false;
      update();
    }

    setInterval(update, 1000);
    update();
  </script>
</body>
</html>
)HTML";
  html.replace("__IP__", wifiIp);
  server.send(200, "text/html; charset=utf-8", html);
}

void handleStatusApi() {
  Logger::logPrintf("[HTTP] /api/status from %s\n", server.client().remoteIP().toString().c_str());
  String json = "{";
  json += "\"ip\":\"" + wifiIp + "\",";
  json += "\"fire\":" + String(latestInput.fireDetected ? 1 : 0) + ",";
  json += "\"human\":" + String(latestInput.humanDetected ? 1 : 0) + ",";
  json += "\"k1\":" + String(latestInput.key1Pressed ? 1 : 0) + ",";
  json += "\"k2\":" + String(latestInput.key2Pressed ? 1 : 0) + ",";
  json += "\"k3\":" + String(latestInput.key3Pressed ? 1 : 0) + ",";
  json += "\"k4\":" + String(latestInput.key4Pressed ? 1 : 0) + ",";
  const LightState ls = IOManager::getLightState();
  json += "\"light_brightness\":" + String(ls.brightness) + ",";
  json += "\"light_warmth\":" + String(ls.warmth) + ",";
  json += "\"time\":\"" + formatNow() + "\",";
  json += "\"time_synced\":" + String(ntpSynced ? 1 : 0) + ",";
  json += "\"temp_c\":" + (isnan(latestEnv.temperatureC) ? String("null") : String(latestEnv.temperatureC, 1)) + ",";
  json += "\"humidity_pct\":" + (isnan(latestEnv.humidityPct) ? String("null") : String(latestEnv.humidityPct, 1)) + ",";
  json += "\"pressure_hpa\":" + (isnan(latestEnv.pressureHpa) ? String("null") : String(latestEnv.pressureHpa, 1));
  json += "}";
  server.send(200, "application/json; charset=utf-8", json);
}

void handlePing() {
  Logger::logPrintf("[HTTP] /ping from %s\n", server.client().remoteIP().toString().c_str());
  server.send(200, "text/plain; charset=utf-8", "pong");
}

void handleLightSet() {
  LightState ls = IOManager::getLightState();

  if (server.hasArg("brightness")) {
    int v = server.arg("brightness").toInt();
    if (v < 0) {
      v = 0;
    } else if (v > 255) {
      v = 255;
    }
    ls.brightness = static_cast<uint8_t>(v);
  }

  if (server.hasArg("warmth")) {
    int v = server.arg("warmth").toInt();
    if (v < 0) {
      v = 0;
    } else if (v > 255) {
      v = 255;
    }
    ls.warmth = static_cast<uint8_t>(v);
  }

  IOManager::setLightState(ls);
  Logger::logPrintf("[HTTP] /api/light/set bri=%u warmth=%u\n", ls.brightness, ls.warmth);
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}
} // namespace

namespace WebManager {
void connectWifi() {
  Logger::logPrintf("[BOOT] connecting Wi-Fi: %s\n", preferredSsid.c_str());
  connectWifiInternal(preferredSsid, preferredPassword, false);
}

void begin() {
  if (WiFi.status() != WL_CONNECTED) {
    Logger::logPrintf("[BOOT] web server skipped: Wi-Fi not connected\n");
    return;
  }
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatusApi);
  server.on("/api/light/set", HTTP_GET, handleLightSet);
  server.on("/ping", HTTP_GET, handlePing);
  server.onNotFound([]() {
    Logger::logPrintf("[HTTP] 404 %s from %s\n",
                      server.uri().c_str(),
                      server.client().remoteIP().toString().c_str());
    server.send(404, "text/plain; charset=utf-8", "Not Found");
  });
  server.begin();
  serverStarted = true;
  Logger::logPrintf("[BOOT] web server started on :80\n");
}

void handleClient() {
  if (serverStarted) {
    server.handleClient();
  }
}

void maintain() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiIp = WiFi.localIP().toString();
    if (!serverStarted) {
      begin();
    }
    if (!ntpSynced && millis() - lastNtpSyncAttemptMs > 30000) {
      syncNtpTime();
    }
    return;
  }

  wifiIp = "0.0.0.0";
  ntpSynced = false;
  const uint32_t now = millis();
  if (now - lastReconnectAttemptMs < 5000) {
    return;
  }
  lastReconnectAttemptMs = now;

  Logger::logPrintf("[NET] Wi-Fi lost, reconnecting...\n");
  WiFi.disconnect();
  WiFi.begin(preferredSsid.c_str(), preferredPassword.c_str());
}

void updateStatus(const InputSnapshot &input, const EnvSnapshot &env) {
  latestInput = input;
  latestEnv = env;
}

String ip() {
  return wifiIp;
}

String currentTimeString() {
  return formatNow();
}

bool isTimeSynced() {
  return ntpSynced;
}

bool isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String currentSsid() {
  return WiFi.SSID();
}

int scanWifiNetworks() {
  scannedCount = 0;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  Logger::logPrintf("[NET] scanning Wi-Fi...\n");

  WiFi.scanDelete();
  const int count = WiFi.scanNetworks();
  if (count <= 0) {
    Logger::logPrintf("[NET] scan done: no network found\n");
    return 0;
  }

  const int limit = count < MAX_SCANNED_WIFI ? count : MAX_SCANNED_WIFI;
  for (int i = 0; i < limit; ++i) {
    scannedSsid[i] = WiFi.SSID(i);
    scannedRssi[i] = WiFi.RSSI(i);
    scannedOpen[i] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    ++scannedCount;
  }
  Logger::logPrintf("[NET] scan done: %d network(s)\n", scannedCount);
  return scannedCount;
}

int wifiNetworkCount() {
  return scannedCount;
}

String wifiNetworkSsid(int index) {
  if (index < 0 || index >= scannedCount) {
    return "";
  }
  return scannedSsid[index];
}

int32_t wifiNetworkRssi(int index) {
  if (index < 0 || index >= scannedCount) {
    return -127;
  }
  return scannedRssi[index];
}

bool wifiNetworkOpen(int index) {
  if (index < 0 || index >= scannedCount) {
    return false;
  }
  return scannedOpen[index];
}

bool connectToWifi(const String &ssid, const String &password) {
  const bool ok = connectWifiInternal(ssid, password, true);
  if (ok && !serverStarted) {
    begin();
  }
  return ok;
}
} // namespace WebManager
