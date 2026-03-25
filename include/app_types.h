#pragma once

#include <math.h>

struct InputSnapshot {
  bool fireDetected = false;
  bool humanDetected = false;
  bool key1Pressed = false;
  bool key2Pressed = false;
  bool key3Pressed = false;
  bool key4Pressed = false;
};

struct EnvSnapshot {
  bool ready = false;
  float temperatureC = NAN;
  float humidityPct = NAN;
  float pressureHpa = NAN;
};

struct LightState {
  uint8_t brightness = 128; // 0..255
  uint8_t warmth = 128;     // 0..255, 255=全暖 0=全冷
};

struct FaceEventSnapshot {
  bool online = false;
  bool hasEvent = false;
  bool known = false;
  int personId = -1;
  float score = 0.0f;
  uint32_t timestampMs = 0;
  char label[32] = {0};
};
