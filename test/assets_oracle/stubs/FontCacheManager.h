#pragma once
struct FontCacheManager {
  bool scanning = false;
  bool isScanning() const { return scanning; }
};
