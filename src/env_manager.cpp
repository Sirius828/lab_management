#include "env_manager.h"

#include <Adafruit_BME280.h>

#include "app_config.h"
#include "logger.h"

namespace {
TwoWire sensorBus(1);
Adafruit_BME280 bme280;
EnvSnapshot env;
} // namespace

namespace EnvManager {
void begin() {
  sensorBus.begin(AppConfig::Pins::SENSOR_SDA, AppConfig::Pins::SENSOR_SCL, 100000);
  env.ready = bme280.begin(0x76, &sensorBus);
  if (env.ready) {
    Logger::logPrintf("[BOOT] bme280 init ok (0x76)\n");
  } else {
    Logger::logPrintf("[BOOT] bme280 begin failed (0x76)\n");
  }
}

void update() {
  if (!env.ready) {
    return;
  }
  env.temperatureC = bme280.readTemperature();
  env.humidityPct = bme280.readHumidity();
  env.pressureHpa = bme280.readPressure() / 100.0f;
}

EnvSnapshot snapshot() {
  return env;
}

TwoWire &bus() {
  return sensorBus;
}
} // namespace EnvManager

