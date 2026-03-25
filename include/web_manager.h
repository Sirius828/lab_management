#pragma once

#include <Arduino.h>

#include "app_types.h"

namespace WebManager {
void connectWifi();
void begin();
void handleClient();
void maintain();
void updateStatus(const InputSnapshot &input, const EnvSnapshot &env);
String ip();
String currentTimeString();
bool isTimeSynced();
bool isConnected();
String currentSsid();

int scanWifiNetworks();
int wifiNetworkCount();
String wifiNetworkSsid(int index);
int32_t wifiNetworkRssi(int index);
bool wifiNetworkOpen(int index);

bool connectToWifi(const String &ssid, const String &password);
} // namespace WebManager
