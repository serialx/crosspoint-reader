#pragma once
#include <ArduinoJson.h>
#include <xp_ffi.h>

#include <string>
struct SyncJson {
  JsonVariantConst input[4];
  JsonVariant output[4];
  std::string text;
  XpSyncJson api();
};
