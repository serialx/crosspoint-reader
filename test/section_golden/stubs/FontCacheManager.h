#pragma once

class FontCacheManager {
 public:
  void releaseSdFontCaches() {}
  bool isScanning() const { return false; }
};
