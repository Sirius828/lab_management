#include "logger.h"

#include <stdarg.h>

namespace Logger {
void begin(uint32_t baudrate) {
  Serial.begin(baudrate);
  Serial0.begin(baudrate);
  delay(300);
}

void logPrintf(const char *fmt, ...) {
  char buf[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
  Serial0.print(buf);
}
} // namespace Logger

