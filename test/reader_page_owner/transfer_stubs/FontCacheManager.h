#pragma once
#include <cassert>
class FontCacheManager {
 public:
  unsigned scopes = 0;
  void releaseSdFontCaches() {}
  bool isScanning() const { return false; }
  struct PrewarmScope {
    FontCacheManager& cache;
    explicit PrewarmScope(FontCacheManager& cache) : cache(cache) { ++cache.scopes; }
    ~PrewarmScope() {
      assert(cache.scopes);
      --cache.scopes;
    }
    void endScanAndPrewarm() {}
  };
};
