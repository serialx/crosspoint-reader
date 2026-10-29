#pragma once
#include <Arduino.h>
constexpr int WL_CONNECTED = 1;
inline bool wifiConnected = true;
struct FakeWiFi { int status() { puts("wifi status"); return wifiConnected; } };
inline FakeWiFi WiFi;
