#include "uart_manager.h"

#include <Arduino.h>

#include "app_config.h"
#include "logger.h"

namespace {
HardwareSerial k210Serial(1);
FaceEventSnapshot latest;
char lineBuf[160];
size_t lineLen = 0;
uint32_t lastHelloTxMs = 0;
uint32_t lastRxMs = 0;

void sendHello() {
  k210Serial.println("ESP_HELLO 1");
  lastHelloTxMs = millis();
}

void parseLine(char *line) {
  if (line[0] == '\0') {
    return;
  }

  lastRxMs = millis();
  latest.online = true;

  if (strncmp(line, "HELLO", 5) == 0) {
    Logger::logPrintf("[K210] %s\n", line);
    k210Serial.println("ACK HELLO");
    return;
  }

  int personId = -1;
  float score = 0.0f;
  if (sscanf(line, "RECOG %d %f", &personId, &score) == 2) {
    latest.hasEvent = true;
    latest.known = true;
    latest.personId = personId;
    latest.score = score;
    latest.timestampMs = millis();
    snprintf(latest.label, sizeof(latest.label), "ID_%d", personId);
    return;
  }

  if (sscanf(line, "UNKNOWN %f", &score) == 1) {
    latest.hasEvent = true;
    latest.known = false;
    latest.personId = -1;
    latest.score = score;
    latest.timestampMs = millis();
    snprintf(latest.label, sizeof(latest.label), "UNKNOWN");
    return;
  }
}
} // namespace

namespace UartManager {
void begin() {
  k210Serial.begin(115200, SERIAL_8N1, AppConfig::Pins::K210_UART_RX, AppConfig::Pins::K210_UART_TX);
  lineLen = 0;
  latest = FaceEventSnapshot{};
  sendHello();
  Logger::logPrintf("[BOOT] K210 UART ready RX=%u TX=%u\n",
                    AppConfig::Pins::K210_UART_RX,
                    AppConfig::Pins::K210_UART_TX);
}

void update() {
  const uint32_t nowMs = millis();
  if (nowMs - lastHelloTxMs > 3000) {
    sendHello();
  }

  while (k210Serial.available() > 0) {
    const char ch = static_cast<char>(k210Serial.read());
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      lineBuf[lineLen] = '\0';
      parseLine(lineBuf);
      lineLen = 0;
      continue;
    }
    if (lineLen + 1 < sizeof(lineBuf)) {
      lineBuf[lineLen++] = ch;
    } else {
      lineLen = 0;
    }
  }

  if (latest.online && nowMs - lastRxMs > 3000) {
    latest.online = false;
  }
}

FaceEventSnapshot latestFaceEvent() {
  FaceEventSnapshot out = latest;
  if (lastRxMs == 0) {
    out.online = false;
  } else {
    out.online = (millis() - lastRxMs) <= 3000;
  }
  return out;
}
} // namespace UartManager
