#pragma once

#include <Wire.h>

#include "app_types.h"

namespace EnvManager {
void begin();
void update();
EnvSnapshot snapshot();
TwoWire &bus();
} // namespace EnvManager

