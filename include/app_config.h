#pragma once

#include <Arduino.h>

namespace AppConfig {
namespace Pins {
constexpr uint8_t FIRE_IN = 1;
constexpr uint8_t BUZZER = 2;
constexpr uint8_t SERVO = 3;

constexpr uint8_t BUTTON1 = 7;
constexpr uint8_t BUTTON2 = 6;
constexpr uint8_t BUTTON3 = 5;
constexpr uint8_t BUTTON4 = 4;

constexpr uint8_t HUMAN = 8;

constexpr uint8_t SENSOR_SDA = 9;
constexpr uint8_t SENSOR_SCL = 10;

constexpr uint8_t TF_CS = 11;
constexpr uint8_t TF_MOSI = 12;
constexpr uint8_t TF_CLK = 13;
constexpr uint8_t TF_MISO = 14;

constexpr uint8_t OLED_SDA = 17;
constexpr uint8_t OLED_SCL = 18;

constexpr uint8_t LED_W = 38;
constexpr uint8_t LED_C = 39;
constexpr uint8_t K210_UART_RX = 47; // ESP RX <- K210 TX
constexpr uint8_t K210_UART_TX = 48; // ESP TX -> K210 RX
} // namespace Pins

namespace Pwm {
constexpr uint8_t CHANNEL_BUZZER = 0;
constexpr uint8_t CHANNEL_LED_W = 1;
constexpr uint8_t CHANNEL_LED_C = 2;

constexpr uint32_t BUZZER_FREQ = 2700;
constexpr uint32_t LED_FREQ = 5000;
constexpr uint8_t RESOLUTION_BITS = 8;
} // namespace Pwm

namespace Wifi {
constexpr const char *SSID = "Xiaomi_30AC"; // wifi名称
constexpr const char *PASSWORD = "72779673";
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
} // namespace Wifi
} // namespace AppConfig
