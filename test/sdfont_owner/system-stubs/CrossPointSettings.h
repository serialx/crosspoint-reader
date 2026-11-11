#pragma once
#include <cstdint>
class CrossPointSettings {
 public:
  char sdFontFamilyName[64]{};
  uint8_t fontPointSize = 14;
  int (*sdFontIdResolver)(void*, const char*, uint8_t) = nullptr;
  void* sdFontResolverCtx = nullptr;
  bool saveToFile() const;
  void clearSdFontFamily();
};
extern CrossPointSettings systemSettings;
#define SETTINGS systemSettings
