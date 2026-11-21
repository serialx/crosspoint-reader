#include <cassert>
#include <string>

#include "SleepCoverCache.h"

#define LOG_DBG(...) (void)0
#define LOG_ERR(...) (void)0

struct HalDisplay {
  enum class GrayscaleMode { Direct, Absolute };
  enum class Controller { SSD1677, Other } controller{};
  Controller getController() const { return controller; }
} display;
struct GfxRenderer {
  bool absolute = true;
  struct Caps {
    bool valid;
    bool supported() const { return valid; }
  };
  Caps grayscaleCapabilities(HalDisplay::GrayscaleMode mode) const {
    return {mode == HalDisplay::GrayscaleMode::Absolute && absolute};
  }
  int getScreenWidth() const { return 40; }
  int getScreenHeight() const { return 64; }
};
struct CrossPointSettings {
  enum class SLEEP_SCREEN_COVER_FILTER { NO_FILTER, BW } sleepScreenCoverFilter{};
  enum class SLEEP_SCREEN_COVER_MODE { FIT, CROP } sleepScreenCoverMode{};
} SETTINGS;
struct HalFile {
  bool opened = false;
  size_t size() const { return 200; }
  uint32_t modificationTime() const { return 123; }
  bool isOpen() const { return opened; }
  bool close() { return std::exchange(opened, false); }
};
struct Card {
  std::string cache;
  bool writable = true;
  bool valid = true;
  bool exists(const char* path) const { return cache == path; }
  bool openFileForRead(const char*, std::string_view path, HalFile& file) {
    return file.opened = path == "/sleep.bmp" || path == cache;
  }
  bool openFileForWrite(const char*, const char*, HalFile& file) { return file.opened = writable; }
  bool rename(const char*, const char* path) {
    cache = path;
    return true;
  }
  void remove(const char* path) {
    if (cache == path) cache.clear();
  }
} Storage;
struct BmpToBmpConverter {
  static inline int calls = 0;
  static inline bool original = false;
  static bool bmpFileToBmpStream(HalFile&, HalFile&, bool, bool thresholds) {
    ++calls;
    original = thresholds;
    return true;
  }
};
struct SleepActivity {
  GfxRenderer renderer;
  bool direct = false;
  bool original = false;
  enum class BmpProbe { Invalid, Rendered, NeedsConversion };
  BmpProbe renderScreenReadyBmp(HalFile&) { return BmpProbe::NeedsConversion; }
  enum class BmpSource { Cache, Original };
  bool renderBmpFile(HalFile&, BmpSource source, bool thresholds = false) {
    direct = source == BmpSource::Original;
    original = thresholds;
    return direct || Storage.valid;
  }
  bool renderCustomSleepBmp(const std::string&);
};
#include "sleep.inc"

int main() {
  SleepActivity sleep;
  for (const bool ssd : {false, true}) {
    display.controller = ssd ? HalDisplay::Controller::SSD1677 : HalDisplay::Controller::Other;
    for (const bool absolute : {false, true}) {
      sleep.renderer.absolute = absolute;
      for (const bool filtered : {false, true}) {
        SETTINGS.sleepScreenCoverFilter = filtered ? CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::BW
                                                   : CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
        Storage = {};
        const bool expected = ssd && absolute && !filtered;
        const std::string key = expected ? "/sleep.bmp|200|123|40x64|f|t" : "/sleep.bmp|200|123|40x64|f";
        const auto path = "/.crosspoint/sleep_" + std::to_string(std::hash<std::string>{}(key)) + ".bmp";
        const int before = BmpToBmpConverter::calls;
        assert(sleep.renderCustomSleepBmp("/sleep.bmp"));
        assert(!sleep.direct && Storage.cache == path && BmpToBmpConverter::original == expected);
        assert(sleep.renderCustomSleepBmp("/sleep.bmp") && BmpToBmpConverter::calls == before + 1);
        Storage.valid = false;
        assert(sleep.renderCustomSleepBmp("/sleep.bmp") && Storage.cache.empty() && sleep.direct &&
               sleep.original == expected);
        Storage.writable = false;
        assert(sleep.renderCustomSleepBmp("/sleep.bmp") && sleep.direct && sleep.original == expected);
      }
    }
  }
}
