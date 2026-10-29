#pragma once
#include <Arduino.h>
inline int64_t storedFloor = 0;
inline bool prefsOk = true;
struct Preferences {
 bool begin(const char* ns, bool readOnly) { printf("prefs %s %u\n", ns, readOnly); return prefsOk; }
 int64_t getLong64(const char* key, int64_t) { printf("get %s %lld\n", key, (long long)storedFloor); return storedFloor; }
 void putLong64(const char* key, int64_t v) { printf("put %s %lld\n", key, (long long)v); storedFloor = v; }
 void end() { puts("prefs close"); }
};
