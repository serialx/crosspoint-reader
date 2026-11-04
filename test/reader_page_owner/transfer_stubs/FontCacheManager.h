#pragma once
class FontCacheManager {
 public:
  class PrewarmScope {};
  void releaseSdFontCaches() {}
  bool isScanning() const { return false; }
};
