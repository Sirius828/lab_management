#pragma once

#include <Arduino.h>

#include "app_types.h"

namespace DisplayManager {
void begin();
void render(const InputSnapshot &input,
            const EnvSnapshot &env,
            const String &timeText,
            const String &ipText,
            bool timeSynced);
} // namespace DisplayManager
