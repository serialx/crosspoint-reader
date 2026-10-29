#pragma once
#include <Arduino.h>
constexpr int WL_CONNECTED = 1, WIFI_MODE_NULL = 0;
inline bool wifiConnected = true;
struct FakeWiFi {
  int getMode() {
    puts("wifi mode");
    return wifiConnected;
  }
  int status() {
    puts("wifi status");
    return wifiConnected;
  }
};
inline FakeWiFi WiFi;
