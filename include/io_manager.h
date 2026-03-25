#pragma once

#include "app_types.h"

namespace IOManager {
void begin();
InputSnapshot readInputs();
void applyOutputs(const InputSnapshot &input);
void setLightState(const LightState &state);
LightState getLightState();
} // namespace IOManager
