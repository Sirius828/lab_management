#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "app_config.h"
#include "app_types.h"
#include "display_manager.h"
#include "env_manager.h"
#include "io_manager.h"
#include "logger.h"
#include "web_manager.h"

namespace {
SPIClass tfBus(FSPI);

void scanI2CBus(TwoWire &bus, const char *busName) {
  Logger::logPrintf("Scanning %s...\n", busName);
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 0x7F; ++addr) {
    bus.beginTransmission(addr);
    if (bus.endTransmission() == 0) {
      Logger::logPrintf("  found: 0x%02X\n", addr);
      ++found;
    }
  }
  if (found == 0) {
    Logger::logPrintf("  no device found on %s\n", busName);
  }
}
} // namespace

void setup() {
  Logger::begin(115200);
  Logger::logPrintf("\n[BOOT] app setup start\n");

  IOManager::begin();
  Logger::logPrintf("[BOOT] io ok\n");

  Wire.begin(AppConfig::Pins::OLED_SDA, AppConfig::Pins::OLED_SCL, 400000);
  tfBus.begin(AppConfig::Pins::TF_CLK,
              AppConfig::Pins::TF_MISO,
              AppConfig::Pins::TF_MOSI,
              AppConfig::Pins::TF_CS);
  Logger::logPrintf("[BOOT] bus core ok\n");

  EnvManager::begin();
  scanI2CBus(Wire, "OLED bus (Wire, SDA=17 SCL=18)");
  scanI2CBus(EnvManager::bus(), "Sensor bus (Wire1, SDA=9 SCL=10)");

  DisplayManager::begin();
  Logger::logPrintf("[BOOT] oled ok\n");

  WebManager::connectWifi();
  WebManager::begin();

  Logger::logPrintf("ESP32-S3 pin map initialized.\n");
}

void loop() {
  static uint32_t lastTick = 0;
  static uint32_t lastEnvUpdate = 0;
  static uint32_t lastOledUpdate = 0;
  static bool lastFire = false;
  static bool lastHuman = false;
  static InputSnapshot input;

  WebManager::maintain();
  WebManager::handleClient();

  if (millis() - lastTick < 100) {
    return;
  }
  lastTick = millis();

  input = IOManager::readInputs();
  IOManager::applyOutputs(input);

  if (millis() - lastEnvUpdate >= 1000) {
    EnvManager::update();
    lastEnvUpdate = millis();
  }

  const EnvSnapshot env = EnvManager::snapshot();
  WebManager::updateStatus(input, env);

  if (millis() - lastOledUpdate >= 200) {
    DisplayManager::render(
        input, env, WebManager::currentTimeString(), WebManager::ip(), WebManager::isTimeSynced());
    lastOledUpdate = millis();
  }

  if (input.fireDetected != lastFire || input.humanDetected != lastHuman) {
    Logger::logPrintf("fire=%d human=%d k=[%d%d%d%d]\n",
                      input.fireDetected,
                      input.humanDetected,
                      input.key1Pressed,
                      input.key2Pressed,
                      input.key3Pressed,
                      input.key4Pressed);
    lastFire = input.fireDetected;
    lastHuman = input.humanDetected;
  }
}
