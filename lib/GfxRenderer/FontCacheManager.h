#pragma once
#include <EpdFontFamily.h>
#include <FontRegistry.h>
#include <xp_font_registry.h>

#ifdef XP_APP_TEST
#include <FontDecompressor.h>
#endif

#include <map>
#include <utility>

class SdCardFont;
class TtfEpdFont;

// Native callers borrow the Rust owner; no cache or scan state lives here.
class FontCacheManager {
  const XpLoadedFonts* owner_;

 public:
  explicit FontCacheManager(const FontRegistry& fonts) : owner_(fonts.raw()) {}
#ifdef XP_APP_TEST
  FontCacheManager(const FontRegistry& fonts, const std::map<int, SdCardFont*>&, const std::map<int, TtfEpdFont*>&)
      : owner_(fonts.raw()) {}
#endif
#ifdef XP_APP_TEST
  void setFontDecompressor(FontDecompressor* adapter) { adapter->bindOwner(owner_); }
#endif
  // Frame clear: XP_FONT_CLEAR_FRAME (0).
  void clearCache() { xp_font_cache_release(owner_, 0); }
  // Resident release: XP_FONT_CLEAR_RESIDENT (1), including SD mini/kerning/ligature/overflow/advance
  // tables and vector arenas. Faces stay registered; cache data reloads on demand.
  void releaseSdFontCaches() { xp_font_cache_release(owner_, 1); }
  bool isScanning() const { return xp_font_scanning(owner_) != 0; }
  void prewarmCache(int font, const char* text, uint8_t styles = 0x0f, bool accumulate = true) {
    xp_font_prewarm_text(owner_, font, text, styles, accumulate);
  }
  void recordText(const char* text, int font, EpdFontFamily::Style style) {
    xp_font_record_text(owner_, font, text, style);
  }
  class PrewarmScope {
    const XpLoadedFonts* owner_;
    bool active_ = true;

   public:
    explicit PrewarmScope(FontCacheManager& manager) : owner_(manager.owner_) { xp_font_frame_begin(owner_); }
    ~PrewarmScope() {
      if (active_) xp_font_frame_end(owner_);
    }
    void endScanAndPrewarm() { xp_font_frame_finish(owner_); }
    PrewarmScope(PrewarmScope&& other) noexcept : owner_(other.owner_), active_(other.active_) {
      other.active_ = false;
    }
    PrewarmScope& operator=(PrewarmScope&&) = delete;
    PrewarmScope(const PrewarmScope&) = delete;
    PrewarmScope& operator=(const PrewarmScope&) = delete;
  };
  PrewarmScope createPrewarmScope() { return PrewarmScope(*this); }
};
