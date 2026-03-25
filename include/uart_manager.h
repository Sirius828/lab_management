#pragma once

#include "app_types.h"

namespace UartManager {
void begin();
void update();
FaceEventSnapshot latestFaceEvent();
} // namespace UartManager
