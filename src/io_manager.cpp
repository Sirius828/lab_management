#include "io_manager.h"

#include <Arduino.h>

#include "app_config.h"

namespace {
bool buzzerPwmAttached = false;
LightState lightState;

void buzzerOn() {
  if (!buzzerPwmAttached) {
    ledcSetup(AppConfig::Pwm::CHANNEL_BUZZER,
              AppConfig::Pwm::BUZZER_FREQ,
              AppConfig::Pwm::RESOLUTION_BITS);
    ledcAttachPin(AppConfig::Pins::BUZZER, AppConfig::Pwm::CHANNEL_BUZZER);
    buzzerPwmAttached = true;
  }
  ledcWrite(AppConfig::Pwm::CHANNEL_BUZZER, 127);
}

void buzzerOff() {
  if (buzzerPwmAttached) {
    ledcWrite(AppConfig::Pwm::CHANNEL_BUZZER, 0);
    ledcDetachPin(AppConfig::Pins::BUZZER);
    buzzerPwmAttached = false;
  }
  pinMode(AppConfig::Pins::BUZZER, OUTPUT);
  digitalWrite(AppConfig::Pins::BUZZER, LOW);
}

void setCctLight(uint8_t warm, uint8_t cool) {
  // 现场验证通道实际接反：将输出对调后，Warm/Cool 控件语义与实际光色一致。
  ledcWrite(AppConfig::Pwm::CHANNEL_LED_W, cool);
  ledcWrite(AppConfig::Pwm::CHANNEL_LED_C, warm);
}

void applyLightPwm() {
  // 语义定义：warmth 越大 -> 暖光越强；warmth 越小 -> 冷光越强。
  const uint16_t warm = (static_cast<uint16_t>(lightState.brightness) * lightState.warmth) / 255;
  const uint16_t cool = (static_cast<uint16_t>(lightState.brightness) * (255 - lightState.warmth)) / 255;
  setCctLight(static_cast<uint8_t>(warm), static_cast<uint8_t>(cool));
}
} // namespace

namespace IOManager {
void begin() {
  pinMode(AppConfig::Pins::BUZZER, OUTPUT);
  digitalWrite(AppConfig::Pins::BUZZER, LOW);

  pinMode(AppConfig::Pins::FIRE_IN, INPUT_PULLUP);
  pinMode(AppConfig::Pins::HUMAN, INPUT);

  pinMode(AppConfig::Pins::BUTTON1, INPUT_PULLUP);
  pinMode(AppConfig::Pins::BUTTON2, INPUT_PULLUP);
  pinMode(AppConfig::Pins::BUTTON3, INPUT_PULLUP);
  pinMode(AppConfig::Pins::BUTTON4, INPUT_PULLUP);

  ledcSetup(AppConfig::Pwm::CHANNEL_LED_W,
            AppConfig::Pwm::LED_FREQ,
            AppConfig::Pwm::RESOLUTION_BITS);
  ledcAttachPin(AppConfig::Pins::LED_W, AppConfig::Pwm::CHANNEL_LED_W);
  ledcWrite(AppConfig::Pwm::CHANNEL_LED_W, 0);

  ledcSetup(AppConfig::Pwm::CHANNEL_LED_C,
            AppConfig::Pwm::LED_FREQ,
            AppConfig::Pwm::RESOLUTION_BITS);
  ledcAttachPin(AppConfig::Pins::LED_C, AppConfig::Pwm::CHANNEL_LED_C);
  ledcWrite(AppConfig::Pwm::CHANNEL_LED_C, 0);

  buzzerOff();
}

InputSnapshot readInputs() {
  InputSnapshot s;
  s.fireDetected = digitalRead(AppConfig::Pins::FIRE_IN) == LOW;
  s.humanDetected = digitalRead(AppConfig::Pins::HUMAN) == HIGH;
  s.key1Pressed = digitalRead(AppConfig::Pins::BUTTON1) == LOW;
  s.key2Pressed = digitalRead(AppConfig::Pins::BUTTON2) == LOW;
  s.key3Pressed = digitalRead(AppConfig::Pins::BUTTON3) == LOW;
  s.key4Pressed = digitalRead(AppConfig::Pins::BUTTON4) == LOW;
  return s;
}

void applyOutputs(const InputSnapshot &input) {
  (void)input;
  // 按键用于菜单交互，蜂鸣器默认关闭，灯光由 LightState 控制。
  buzzerOff();
  applyLightPwm();
}

void setLightState(const LightState &state) {
  lightState = state;
  applyLightPwm();
}

LightState getLightState() {
  return lightState;
}
} // namespace IOManager
