#pragma once
#include <Arduino.h>
inline bool chargerOk = false, batteryOk = true;
inline uint16_t batteryPercent = 50;
struct BatteryMonitor {
 static bool configureCharger() { puts("charger"); return chargerOk; }
 bool readPercentageChecked(uint16_t& p) const { printf("gauge %u %u\n", batteryPercent,batteryOk); p = batteryPercent; return batteryOk; }
 uint16_t readPercentage() const { printf("battery %u\n", batteryPercent); return batteryPercent; }
};
