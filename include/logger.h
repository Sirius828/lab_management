#pragma once

#include <Arduino.h>

namespace Logger {
void begin(uint32_t baudrate);
void logPrintf(const char *fmt, ...);
} // namespace Logger

