#include "display_manager.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include "io_manager.h"
#include "logger.h"
#include "web_manager.h"

namespace {
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

enum class Page : uint8_t {
  Home = 0,
  EnvQuick = 1,
  HumanQuick = 2,
  LightQuick = 3,
  NetQuick = 4,
  ToolsMenu = 5,
  ToolSensor = 6,
  ToolFire = 7,
  ToolInputs = 8,
  ToolWifi = 9,
};

enum class WifiSetupStage : uint8_t {
  Idle = 0,
  Scanning = 1,
  List = 2,
  Keyboard = 3,
  Result = 4,
};

Page currentPage = Page::Home;
Page previousPage = Page::Home;
bool pageChanged = false;
int transitionDir = 1; // 1: next page; -1: prev page

InputSnapshot lastInput;
uint32_t lastKeyActionMs = 0;
uint32_t holdStartK2 = 0;
uint32_t holdStartK3 = 0;
uint32_t lastRepeatK2 = 0;
uint32_t lastRepeatK3 = 0;

bool lightEditing = false;
uint8_t lightEditField = 0; // 0: brightness, 1: warmth
constexpr uint8_t LIGHT_STEP = 12;

int toolsIndex = 0; // 0 sensor, 1 fire, 2 inputs, 3 wifi
constexpr int TOOL_COUNT = 4;

constexpr char WIFI_CHARSET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
constexpr size_t WIFI_CHARSET_LEN = sizeof(WIFI_CHARSET) - 1;
WifiSetupStage wifiStage = WifiSetupStage::Idle;
int wifiListIndex = 0;
int wifiListCount = 0;
String wifiSelectedSsid;
String wifiPassword;
int wifiCharIndex = 0;
bool wifiConnectOk = false;
String wifiConnectMsg;
bool wifiScanPending = false;
uint32_t wifiScanStartMs = 0;

bool pressedEdge(bool nowPressed, bool prevPressed) {
  return nowPressed && !prevPressed;
}

bool repeatedPress(bool nowPressed,
                   bool prevPressed,
                   uint32_t &holdStartMs,
                   uint32_t &lastRepeatMs,
                   uint32_t nowMs,
                   uint32_t firstDelayMs = 450,
                   uint32_t repeatIntervalMs = 120) {
  if (!nowPressed) {
    holdStartMs = 0;
    lastRepeatMs = 0;
    return false;
  }
  if (!prevPressed) {
    holdStartMs = nowMs;
    lastRepeatMs = nowMs;
    return true;
  }
  if (holdStartMs == 0) {
    holdStartMs = nowMs;
    lastRepeatMs = nowMs;
    return false;
  }
  if (nowMs - holdStartMs < firstDelayMs) {
    return false;
  }
  if (nowMs - lastRepeatMs >= repeatIntervalMs) {
    lastRepeatMs = nowMs;
    return true;
  }
  return false;
}

bool isTopLevel(Page p) {
  return p == Page::Home || p == Page::EnvQuick || p == Page::HumanQuick || p == Page::LightQuick ||
         p == Page::NetQuick;
}

Page nextTop(Page p, int dir) {
  constexpr Page kTopPages[] = {Page::Home, Page::EnvQuick, Page::HumanQuick, Page::LightQuick, Page::NetQuick};
  constexpr int topCount = static_cast<int>(sizeof(kTopPages) / sizeof(kTopPages[0]));

  int idx = 0;
  for (int i = 0; i < topCount; ++i) {
    if (kTopPages[i] == p) {
      idx = i;
      break;
    }
  }
  if (!isTopLevel(p)) {
    idx = 0;
  }

  const int step = dir >= 0 ? 1 : -1;
  idx = (idx + step + topCount) % topCount;
  return kTopPages[idx];
}

void setCurrentPage(Page nextPage, int dir) {
  if (nextPage == currentPage) {
    return;
  }
  previousPage = currentPage;
  currentPage = nextPage;
  transitionDir = dir;
  pageChanged = true;
}

void resetWifiSetup() {
  wifiStage = WifiSetupStage::Idle;
  wifiListIndex = 0;
  wifiListCount = 0;
  wifiSelectedSsid = "";
  wifiPassword = "";
  wifiCharIndex = 0;
  wifiConnectOk = false;
  wifiConnectMsg = "";
  wifiScanPending = false;
  wifiScanStartMs = 0;
}

void startWifiScan(uint32_t nowMs) {
  wifiStage = WifiSetupStage::Scanning;
  wifiScanPending = true;
  wifiScanStartMs = nowMs;
  wifiConnectMsg = "";
}

void processWifiToolKeys(const InputSnapshot &input, uint32_t nowMs) {
  const bool k1 = pressedEdge(input.key1Pressed, lastInput.key1Pressed);
  const bool k4 = pressedEdge(input.key4Pressed, lastInput.key4Pressed);

  if (k4) {
    if (wifiStage == WifiSetupStage::Keyboard) {
      wifiStage = WifiSetupStage::List;
    } else if (wifiStage == WifiSetupStage::List || wifiStage == WifiSetupStage::Result ||
               wifiStage == WifiSetupStage::Scanning) {
      wifiStage = WifiSetupStage::Idle;
    } else {
      resetWifiSetup();
      setCurrentPage(Page::ToolsMenu, -1);
    }
    lastKeyActionMs = nowMs;
    return;
  }

  if (wifiStage == WifiSetupStage::Idle || wifiStage == WifiSetupStage::Result) {
    if (k1) {
      startWifiScan(nowMs);
      lastKeyActionMs = nowMs;
    }
    return;
  }

  if (wifiStage == WifiSetupStage::Scanning) {
    return;
  }

  if (wifiStage == WifiSetupStage::List) {
    const bool k2Repeat = repeatedPress(
        input.key2Pressed, lastInput.key2Pressed, holdStartK2, lastRepeatK2, nowMs, 350, 140);
    const bool k3Repeat = repeatedPress(
        input.key3Pressed, lastInput.key3Pressed, holdStartK3, lastRepeatK3, nowMs, 350, 140);

    if (k2Repeat && wifiListCount > 0) {
      wifiListIndex = (wifiListIndex - 1 + wifiListCount) % wifiListCount;
      lastKeyActionMs = nowMs;
    } else if (k3Repeat && wifiListCount > 0) {
      wifiListIndex = (wifiListIndex + 1) % wifiListCount;
      lastKeyActionMs = nowMs;
    } else if (k1) {
      if (wifiListCount > 0) {
        wifiSelectedSsid = WebManager::wifiNetworkSsid(wifiListIndex);
        wifiPassword = "";
        wifiCharIndex = 0;
        wifiStage = WifiSetupStage::Keyboard;
      } else {
        startWifiScan(nowMs);
      }
      lastKeyActionMs = nowMs;
    }
    return;
  }

  if (wifiStage == WifiSetupStage::Keyboard) {
    const bool k2Repeat = repeatedPress(
        input.key2Pressed, lastInput.key2Pressed, holdStartK2, lastRepeatK2, nowMs);
    const bool k3Repeat = repeatedPress(
        input.key3Pressed, lastInput.key3Pressed, holdStartK3, lastRepeatK3, nowMs);

    const int wheelLen = static_cast<int>(WIFI_CHARSET_LEN + 2); // +DEL +CONNECT
    if (k2Repeat) {
      wifiCharIndex = (wifiCharIndex - 1 + wheelLen) % wheelLen;
      lastKeyActionMs = nowMs;
    } else if (k3Repeat) {
      wifiCharIndex = (wifiCharIndex + 1) % wheelLen;
      lastKeyActionMs = nowMs;
    } else if (k1) {
      if (wifiCharIndex < static_cast<int>(WIFI_CHARSET_LEN)) {
        wifiPassword += WIFI_CHARSET[wifiCharIndex];
      } else if (wifiCharIndex == static_cast<int>(WIFI_CHARSET_LEN)) {
        if (wifiPassword.length() > 0) {
          wifiPassword.remove(wifiPassword.length() - 1);
        }
      } else {
        wifiConnectOk = WebManager::connectToWifi(wifiSelectedSsid, wifiPassword);
        wifiConnectMsg = wifiConnectOk ? "Connected" : "Connect failed";
        wifiStage = WifiSetupStage::Result;
      }
      lastKeyActionMs = nowMs;
    }
  }
}

void processMenuKeys(const InputSnapshot &input) {
  if (millis() - lastKeyActionMs < 120) {
    lastInput = input;
    return;
  }

  const bool k1 = pressedEdge(input.key1Pressed, lastInput.key1Pressed);
  const bool k2 = pressedEdge(input.key2Pressed, lastInput.key2Pressed);
  const bool k3 = pressedEdge(input.key3Pressed, lastInput.key3Pressed);
  const bool k4 = pressedEdge(input.key4Pressed, lastInput.key4Pressed);
  const uint32_t nowMs = millis();

  if (currentPage == Page::ToolWifi) {
    processWifiToolKeys(input, nowMs);
    lastInput = input;
    return;
  }

  if (currentPage == Page::ToolSensor || currentPage == Page::ToolFire || currentPage == Page::ToolInputs) {
    if (k4) {
      setCurrentPage(Page::ToolsMenu, -1);
      lastKeyActionMs = nowMs;
    }
    lastInput = input;
    return;
  }

  if (currentPage == Page::ToolsMenu) {
    const bool k2Repeat = repeatedPress(
        input.key2Pressed, lastInput.key2Pressed, holdStartK2, lastRepeatK2, nowMs, 300, 120);
    const bool k3Repeat = repeatedPress(
        input.key3Pressed, lastInput.key3Pressed, holdStartK3, lastRepeatK3, nowMs, 300, 120);

    if (k2Repeat) {
      toolsIndex = (toolsIndex + TOOL_COUNT - 1) % TOOL_COUNT;
      lastKeyActionMs = nowMs;
    } else if (k3Repeat) {
      toolsIndex = (toolsIndex + 1) % TOOL_COUNT;
      lastKeyActionMs = nowMs;
    } else if (k1) {
      if (toolsIndex == 0) {
        setCurrentPage(Page::ToolSensor, 1);
      } else if (toolsIndex == 1) {
        setCurrentPage(Page::ToolFire, 1);
      } else if (toolsIndex == 2) {
        setCurrentPage(Page::ToolInputs, 1);
      } else {
        setCurrentPage(Page::ToolWifi, 1);
        wifiStage = WifiSetupStage::Idle;
      }
      lastKeyActionMs = nowMs;
    } else if (k4) {
      setCurrentPage(Page::Home, -1);
      lastKeyActionMs = nowMs;
    }
    lastInput = input;
    return;
  }

  if (currentPage == Page::LightQuick && lightEditing) {
    LightState ls = IOManager::getLightState();
    const bool k2Repeat = repeatedPress(
        input.key2Pressed, lastInput.key2Pressed, holdStartK2, lastRepeatK2, nowMs);
    const bool k3Repeat = repeatedPress(
        input.key3Pressed, lastInput.key3Pressed, holdStartK3, lastRepeatK3, nowMs);
    if (k2Repeat) {
      if (lightEditField == 0) {
        ls.brightness = static_cast<uint8_t>(ls.brightness >= LIGHT_STEP ? ls.brightness - LIGHT_STEP : 0);
      } else {
        ls.warmth = static_cast<uint8_t>(ls.warmth >= LIGHT_STEP ? ls.warmth - LIGHT_STEP : 0);
      }
      IOManager::setLightState(ls);
      lastKeyActionMs = nowMs;
    } else if (k3Repeat) {
      if (lightEditField == 0) {
        ls.brightness = static_cast<uint8_t>(ls.brightness <= (255 - LIGHT_STEP) ? ls.brightness + LIGHT_STEP : 255);
      } else {
        ls.warmth = static_cast<uint8_t>(ls.warmth <= (255 - LIGHT_STEP) ? ls.warmth + LIGHT_STEP : 255);
      }
      IOManager::setLightState(ls);
      lastKeyActionMs = nowMs;
    } else if (k1) {
      lightEditing = false;
      setCurrentPage(Page::ToolsMenu, 1);
      lastKeyActionMs = nowMs;
    } else if (k4) {
      lightEditField = static_cast<uint8_t>((lightEditField + 1) % 2);
      lastKeyActionMs = nowMs;
    }
    lastInput = input;
    return;
  }

  if (k2) {
    setCurrentPage(nextTop(currentPage, -1), -1);
    lastKeyActionMs = nowMs;
  } else if (k3) {
    setCurrentPage(nextTop(currentPage, 1), 1);
    lastKeyActionMs = nowMs;
  } else if (k1) {
    if (currentPage == Page::Home || currentPage == Page::EnvQuick || currentPage == Page::HumanQuick ||
        currentPage == Page::LightQuick || currentPage == Page::NetQuick) {
      setCurrentPage(Page::ToolsMenu, 1);
    }
    lastKeyActionMs = nowMs;
  } else if (k4) {
    if (currentPage == Page::LightQuick) {
      lightEditing = true;
      lightEditField = 0;
    } else {
      setCurrentPage(Page::Home, -1);
      lightEditing = false;
      resetWifiSetup();
    }
    lastKeyActionMs = nowMs;
  }

  lastInput = input;
}

void drawTitle(const char *title, int16_t xOffset) {
  oled.fillRect(xOffset, 0, 128, 12, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  oled.setTextSize(1);
  oled.setCursor(xOffset + 3, 2);
  oled.print(title);
  oled.setTextColor(SSD1306_WHITE);
}

void drawTitleIcon(Page page, int16_t xOffset) {
  const int16_t x = xOffset + 118;
  const int16_t y = 6;
  if (page == Page::Home) {
    oled.drawTriangle(x - 5, y + 1, x, y - 4, x + 5, y + 1, SSD1306_BLACK);
    oled.drawRect(x - 4, y + 1, 8, 5, SSD1306_BLACK);
    oled.drawPixel(x, y + 4, SSD1306_BLACK);
  } else if (page == Page::EnvQuick) {
    oled.drawCircle(x, y, 3, SSD1306_BLACK);
    oled.drawLine(x, y - 5, x, y + 5, SSD1306_BLACK);
  } else if (page == Page::HumanQuick) {
    oled.drawCircle(x, y - 2, 2, SSD1306_BLACK);
    oled.drawLine(x, y, x, y + 4, SSD1306_BLACK);
    oled.drawLine(x - 3, y + 2, x + 3, y + 2, SSD1306_BLACK);
  } else if (page == Page::LightQuick) {
    oled.drawCircle(x, y - 1, 3, SSD1306_BLACK);
    oled.fillRect(x - 1, y + 2, 3, 2, SSD1306_BLACK);
    oled.drawLine(x - 2, y + 4, x + 2, y + 4, SSD1306_BLACK);
  } else if (page == Page::NetQuick) {
    oled.drawLine(x - 5, y + 2, x + 5, y + 2, SSD1306_BLACK);
    oled.drawCircle(x, y + 2, 1, SSD1306_BLACK);
    oled.drawCircle(x, y + 2, 3, SSD1306_BLACK);
    oled.drawCircle(x, y + 2, 5, SSD1306_BLACK);
  } else if (page == Page::ToolsMenu || page == Page::ToolWifi) {
    oled.drawCircle(x, y, 2, SSD1306_BLACK);
    oled.drawLine(x - 4, y, x - 2, y, SSD1306_BLACK);
    oled.drawLine(x + 2, y, x + 4, y, SSD1306_BLACK);
    oled.drawLine(x, y - 4, x, y - 2, SSD1306_BLACK);
    oled.drawLine(x, y + 2, x, y + 4, SSD1306_BLACK);
  } else if (page == Page::ToolSensor) {
    oled.drawCircle(x, y - 1, 2, SSD1306_BLACK);
    oled.drawLine(x, y + 1, x, y + 5, SSD1306_BLACK);
    oled.drawLine(x - 2, y + 5, x + 2, y + 5, SSD1306_BLACK);
  } else if (page == Page::ToolFire) {
    oled.drawTriangle(x - 2, y + 3, x, y - 3, x + 2, y + 3, SSD1306_BLACK);
    oled.drawLine(x - 1, y + 1, x + 1, y - 1, SSD1306_BLACK);
  } else if (page == Page::ToolInputs) {
    oled.drawRect(x - 5, y - 3, 8, 6, SSD1306_BLACK);
    oled.drawLine(x - 1, y - 3, x - 1, y - 5, SSD1306_BLACK);
    oled.drawLine(x + 1, y - 3, x + 1, y - 5, SSD1306_BLACK);
    oled.drawLine(x - 4, y + 3, x - 6, y + 5, SSD1306_BLACK);
  }
}

float easeOutCubic(float t) {
  const float inv = 1.0f - t;
  return 1.0f - inv * inv * inv;
}

void drawHintRow(const char *k1, const char *k2, const char *k3, const char *k4, int16_t xOffset) {
  oled.setTextSize(1);
  oled.setCursor(xOffset + 1, 56);
  oled.print(k1);
  oled.setCursor(xOffset + 34, 56);
  oled.print(k2);
  oled.setCursor(xOffset + 66, 56);
  oled.print(k3);
  oled.setCursor(xOffset + 98, 56);
  oled.print(k4);
}

void drawPage(Page page,
              int16_t xOffset,
              const InputSnapshot &input,
              const EnvSnapshot &env,
              const String &timeText,
              const String &ipText,
              bool timeSynced) {
  char line1[24] = {0};
  char line2[24] = {0};
  char line3[24] = {0};

  if (page == Page::Home) {
    drawTitle("Home", xOffset);
    drawTitleIcon(page, xOffset);

    String timePart = timeText.length() >= 8 ? timeText.substring(timeText.length() - 8) : timeText;
    const String datePart = timeText.length() >= 10 ? timeText.substring(0, 10) : "";
    int16_t x1 = 0, y1 = 0;
    uint16_t w = 0, h = 0;
    oled.setTextSize(2);
    oled.getTextBounds(timePart, 0, 0, &x1, &y1, &w, &h);
    const int16_t cx = xOffset + (128 - static_cast<int16_t>(w)) / 2;
    oled.setCursor(cx, 22);
    oled.print(timePart);
    oled.setCursor(cx + 1, 22);
    oled.print(timePart);

    oled.setTextSize(1);
    oled.getTextBounds(datePart, 0, 0, &x1, &y1, &w, &h);
    const int16_t dx = xOffset + (128 - static_cast<int16_t>(w)) / 2;
    oled.setCursor(dx, 46);
    oled.print(datePart);

    if (timeSynced) {
      drawHintRow("Menu", "<", ">", "Home", xOffset);
    } else {
      oled.setCursor(xOffset + 2, 56);
      oled.print("Syncing time...");
    }
  } else if (page == Page::EnvQuick) {
    drawTitle("Env", xOffset);
    drawTitleIcon(page, xOffset);
    oled.setTextSize(1);
    if (!env.ready || isnan(env.temperatureC) || isnan(env.humidityPct) || isnan(env.pressureHpa)) {
      oled.setCursor(xOffset + 4, 20);
      oled.println("Sensor fail");
      oled.setCursor(xOffset + 4, 32);
      oled.println("I2C addr: 0x76");
    } else {
      snprintf(line1, sizeof(line1), "%.1fC", env.temperatureC);
      snprintf(line2, sizeof(line2), "%.0f%%", env.humidityPct);
      oled.setTextSize(2);
      oled.setCursor(xOffset + 4, 18);
      oled.print("T:");
      oled.print(line1);
      oled.setCursor(xOffset + 4, 40);
      oled.print("H:");
      oled.print(line2);
      oled.setTextSize(1);
    }
    drawHintRow("Menu", "<", ">", "Home", xOffset);
  } else if (page == Page::HumanQuick) {
    drawTitle("Human", xOffset);
    drawTitleIcon(page, xOffset);
    oled.setTextSize(1);
    oled.setCursor(xOffset + 4, 18);
    oled.print("PIR STATUS");
    const char *stateText = input.humanDetected ? "DETECTED" : "IDLE";
    int16_t x1 = 0, y1 = 0;
    uint16_t w = 0, h = 0;
    oled.setTextSize(2);
    oled.getTextBounds(stateText, 0, 0, &x1, &y1, &w, &h);
    const int16_t sx = xOffset + (128 - static_cast<int16_t>(w)) / 2;
    oled.setCursor(sx, 34);
    oled.print(stateText);
    oled.setCursor(sx + 1, 34);
    oled.print(stateText);
    oled.setTextSize(1);
    drawHintRow("Menu", "<", ">", "Home", xOffset);
  } else if (page == Page::NetQuick) {
    drawTitle("Network", xOffset);
    drawTitleIcon(page, xOffset);
    oled.setCursor(xOffset + 4, 18);
    oled.println(WebManager::isConnected() ? "WiFi: Connected" : "WiFi: Not connected");
    if (WebManager::isConnected()) {
      String ssid = WebManager::currentSsid();
      if (ssid.length() > 14) {
        ssid = ssid.substring(0, 14);
      }
      oled.setCursor(xOffset + 4, 30);
      oled.print("SSID:");
      oled.println(ssid);
    }
    oled.setCursor(xOffset + 4, WebManager::isConnected() ? 42 : 32);
    oled.print("IP:");
    oled.println(ipText);
    drawHintRow("Menu", "<", ">", "Home", xOffset);
  } else if (page == Page::LightQuick) {
    drawTitle("Light", xOffset);
    drawTitleIcon(page, xOffset);

    const LightState ls = IOManager::getLightState();
    oled.setCursor(xOffset + 4, 18);
    oled.print(lightEditing && lightEditField == 0 ? "> BRI" : "  BRI");
    oled.drawRect(xOffset + 44, 18, 78, 8, SSD1306_WHITE);
    const uint8_t briBar = static_cast<uint8_t>((static_cast<uint16_t>(ls.brightness) * 76) / 255);
    oled.fillRect(xOffset + 45, 19, briBar, 6, SSD1306_WHITE);

    oled.setCursor(xOffset + 4, 34);
    oled.print(lightEditing && lightEditField == 1 ? "> CCT" : "  CCT");
    oled.drawRect(xOffset + 44, 34, 78, 8, SSD1306_WHITE);
    const uint8_t cctBar = static_cast<uint8_t>((static_cast<uint16_t>(ls.warmth) * 76) / 255);
    oled.fillRect(xOffset + 45, 35, cctBar, 6, SSD1306_WHITE);

    if (lightEditing) {
      drawHintRow("Menu", "-", "+", "Next", xOffset);
    } else {
      drawHintRow("Menu", "<", ">", "Edit", xOffset);
    }
  } else if (page == Page::ToolsMenu) {
    drawTitle("Tools", xOffset);
    drawTitleIcon(page, xOffset);

    const char *items[TOOL_COUNT] = {"Sensor", "Fire Monitor", "Input Debug", "WiFi Setup"};
    for (int i = 0; i < TOOL_COUNT; ++i) {
      const int y = 16 + i * 11;
      if (i == toolsIndex) {
        oled.fillRect(xOffset + 2, y - 1, 124, 11, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        oled.setCursor(xOffset + 4, y);
        oled.print("> ");
        oled.print(items[i]);
        oled.setTextColor(SSD1306_WHITE);
      } else {
        oled.setCursor(xOffset + 4, y);
        oled.print("  ");
        oled.print(items[i]);
      }
    }
    drawHintRow("Enter", "Up", "Down", "Home", xOffset);
  } else if (page == Page::ToolSensor) {
    drawTitle("Sensor", xOffset);
    drawTitleIcon(page, xOffset);
    oled.setTextSize(1);
    if (!env.ready || isnan(env.temperatureC) || isnan(env.humidityPct) || isnan(env.pressureHpa)) {
      oled.setCursor(xOffset + 4, 20);
      oled.println("Sensor fail");
      oled.setCursor(xOffset + 4, 32);
      oled.println("I2C addr: 0x76");
    } else {
      snprintf(line1, sizeof(line1), "T: %.1f C", env.temperatureC);
      snprintf(line2, sizeof(line2), "H: %.1f %%", env.humidityPct);
      snprintf(line3, sizeof(line3), "P: %.1f hPa", env.pressureHpa);
      oled.setCursor(xOffset + 4, 20);
      oled.println(line1);
      oled.setCursor(xOffset + 4, 32);
      oled.println(line2);
      oled.setCursor(xOffset + 4, 44);
      oled.println(line3);
    }
    drawHintRow("-", "-", "-", "Back", xOffset);
  } else if (page == Page::ToolFire) {
    drawTitle("Fire Monitor", xOffset);
    drawTitleIcon(page, xOffset);
    oled.setCursor(xOffset + 4, 22);
    oled.print("Flame:");
    oled.println(input.fireDetected ? " ALERT" : " NORMAL");
    oled.setCursor(xOffset + 4, 36);
    oled.println("Human:" + String(input.humanDetected ? "1" : "0"));
    drawHintRow("-", "-", "-", "Back", xOffset);
  } else if (page == Page::ToolInputs) {
    drawTitle("Input Debug", xOffset);
    drawTitleIcon(page, xOffset);
    snprintf(line1, sizeof(line1), "Fire:%d Human:%d", input.fireDetected, input.humanDetected);
    snprintf(line2, sizeof(line2), "K1:%d K2:%d", input.key1Pressed, input.key2Pressed);
    snprintf(line3, sizeof(line3), "K3:%d K4:%d", input.key3Pressed, input.key4Pressed);
    oled.setCursor(xOffset + 4, 20);
    oled.println(line1);
    oled.setCursor(xOffset + 4, 32);
    oled.println(line2);
    oled.setCursor(xOffset + 4, 44);
    oled.println(line3);
    drawHintRow("-", "-", "-", "Back", xOffset);
  } else if (page == Page::ToolWifi) {
    drawTitle("WiFi Setup", xOffset);
    drawTitleIcon(page, xOffset);

    if (wifiStage == WifiSetupStage::Idle) {
      oled.setCursor(xOffset + 4, 18);
      oled.println("Scan WiFi");
      oled.setCursor(xOffset + 4, 32);
      if (WebManager::isConnected()) {
        oled.println("IP:" + ipText);
      } else {
        oled.println("Not connected");
      }
      drawHintRow("Scan", "-", "-", "Back", xOffset);
    } else if (wifiStage == WifiSetupStage::Scanning) {
      oled.setCursor(xOffset + 4, 22);
      oled.println("Scanning WiFi...");
      oled.setCursor(xOffset + 4, 36);
      oled.println("Please wait");
      drawHintRow("-", "-", "-", "Back", xOffset);
    } else if (wifiStage == WifiSetupStage::List) {
      oled.setCursor(xOffset + 4, 14);
      oled.print("Scan:");
      oled.print(wifiListCount);
      if (wifiListCount == 0) {
        oled.setCursor(xOffset + 4, 30);
        oled.println("No WiFi found");
        drawHintRow("Rescan", "-", "-", "Back", xOffset);
      } else {
        const String ssid = WebManager::wifiNetworkSsid(wifiListIndex);
        oled.setCursor(xOffset + 4, 26);
        oled.print(">");
        oled.print(ssid.length() > 14 ? ssid.substring(0, 14) : ssid);
        oled.setCursor(xOffset + 4, 38);
        oled.print("RSSI:");
        oled.print(WebManager::wifiNetworkRssi(wifiListIndex));
        oled.print(" ");
        oled.print(WebManager::wifiNetworkOpen(wifiListIndex) ? "Open" : "Lock");
        drawHintRow("Select", "Up", "Down", "Back", xOffset);
      }
    } else if (wifiStage == WifiSetupStage::Keyboard) {
      oled.setCursor(xOffset + 4, 12);
      oled.print("SSID:");
      oled.print(wifiSelectedSsid.length() > 10 ? wifiSelectedSsid.substring(0, 10) : wifiSelectedSsid);

      oled.setCursor(xOffset + 4, 24);
      oled.print("PWD:");
      const int stars = wifiPassword.length() < 12 ? wifiPassword.length() : 12;
      for (int i = 0; i < stars; ++i) {
        oled.print('*');
      }

      oled.setCursor(xOffset + 4, 38);
      if (wifiCharIndex < static_cast<int>(WIFI_CHARSET_LEN)) {
        oled.print("Char:");
        oled.print(WIFI_CHARSET[wifiCharIndex]);
      } else if (wifiCharIndex == static_cast<int>(WIFI_CHARSET_LEN)) {
        oled.print("Action:DEL");
      } else {
        oled.print("Action:CONNECT");
      }

      drawHintRow("OK", "Prev", "Next", "Back", xOffset);
    } else if (wifiStage == WifiSetupStage::Result) {
      oled.setCursor(xOffset + 4, 20);
      oled.println(wifiConnectOk ? "Connected" : "Connect failed");
      oled.setCursor(xOffset + 4, 34);
      if (WebManager::isConnected()) {
        oled.println("IP:" + WebManager::ip());
      } else {
        oled.println("No IP");
      }
      drawHintRow("Rescan", "-", "-", "Back", xOffset);
    }
  }
}

void maybeRunWifiScan() {
  if (wifiStage != WifiSetupStage::Scanning || !wifiScanPending) {
    return;
  }
  if (millis() - wifiScanStartMs < 120) {
    return;
  }
  wifiListCount = WebManager::scanWifiNetworks();
  wifiListIndex = 0;
  wifiStage = WifiSetupStage::List;
  wifiScanPending = false;
}

void animateTransition(const InputSnapshot &input,
                       const EnvSnapshot &env,
                       const String &timeText,
                       const String &ipText,
                       bool timeSynced) {
  const int steps = 6;
  for (int i = 0; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    const int16_t shift = static_cast<int16_t>(128.0f * easeOutCubic(t));
    oled.clearDisplay();
    if (transitionDir >= 0) {
      drawPage(previousPage, -shift, input, env, timeText, ipText, timeSynced);
      drawPage(currentPage, 128 - shift, input, env, timeText, ipText, timeSynced);
    } else {
      drawPage(previousPage, shift, input, env, timeText, ipText, timeSynced);
      drawPage(currentPage, -128 + shift, input, env, timeText, ipText, timeSynced);
    }
    oled.display();
    delay(16);
  }
}
} // namespace

namespace DisplayManager {
void begin() {
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Logger::logPrintf("[BOOT] oled begin failed (0x3C)\n");
    return;
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("ESP32-S3 OLED OK");
  oled.println("SDA=17 SCL=18");
  oled.display();
}

void render(const InputSnapshot &input,
            const EnvSnapshot &env,
            const String &timeText,
            const String &ipText,
            bool timeSynced) {
  processMenuKeys(input);
  maybeRunWifiScan();
  if (pageChanged) {
    animateTransition(input, env, timeText, ipText, timeSynced);
    pageChanged = false;
  }

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  drawPage(currentPage, 0, input, env, timeText, ipText, timeSynced);
  oled.display();
}
} // namespace DisplayManager
