#pragma once
#include <Arduino.h>
inline int64_t storedFloor = 0;
inline bool prefsOk = true;
inline uint8_t deviceOverride = 0, deviceCached = 0;
struct Preferences {
  uint8_t getUChar(const char* key, uint8_t) {
    printf("getbyte %s\n", key);
    return key[4] == 'o' ? deviceOverride : deviceCached;
  }
  void putUChar(const char* key, uint8_t v) {
    printf("putbyte %s %u\n", key, v);
    deviceCached = v;
  }
  bool begin(const char* ns, bool readOnly) {
    printf("prefs %s %u\n", ns, readOnly);
    return prefsOk;
  }
  int64_t getLong64(const char* key, int64_t) {
    printf("get %s %lld\n", key, (long long)storedFloor);
    return storedFloor;
  }
  void putLong64(const char* key, int64_t v) {
    printf("put %s %lld\n", key, (long long)v);
    storedFloor = v;
  }
  void end() { puts("prefs close"); }
};
