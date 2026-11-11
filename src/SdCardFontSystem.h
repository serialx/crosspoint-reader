#pragma once

#include <Epub/blocks/TextBlock.h>
#include <HalStorage.h>  // HalFile (kept open for streamed TTFs)
#include <SdCardFontRegistry.h>
#include <VectorFontSupport.h>

#if CROSSPOINT_VECTOR_FONTS
#include <FontPsram.h>  // PsramVector for resident TTF bytes
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GfxRenderer;
class TtfEpdFont;

using SdSystemCatalog = SdFontCatalog;
using SdSystemFamily = SdFontFamily;

/// Native startup/UI facade for Rust SD catalog policy and synchronous effects.
/// TTF resources stay native until PT-VEC.
class SdCardFontSystem {
 public:
  // Constructor and destructor are out-of-line (defined in the .cpp where
  // TtfEpdFont is a complete type) so the std::unique_ptr<TtfEpdFont> member
  // can be constructed/destroyed with only a forward declaration visible here.
  SdCardFontSystem();
  ~SdCardFontSystem();
  SdCardFontSystem(const SdCardFontSystem&) = delete;
  SdCardFontSystem& operator=(const SdCardFontSystem&) = delete;
  /// Discover SD card fonts and load user's saved selection. Call once during setup.
  void begin(GfxRenderer& renderer);

  /// Ensure the correct SD font family is loaded for the current settings.
  /// Call before entering the reader or after settings change.
  /// Also re-discovers if the registry has been marked dirty (e.g. by web upload).
  void ensureLoaded(GfxRenderer& renderer);
  void fontEffect(XpSdAction action, XpSdSettings& settings, XpSdContinuation& token, GfxRenderer& renderer);

  /// Resolve an SD card font ID from family name + reader point size.
  /// Returns 0 if not found. Used by CrossPointSettings::getReaderFontId().
  int resolveFontId(const char* familyName, uint8_t pointSize) const;

  /// Access the registry (e.g. for settings UI to enumerate available fonts).
  const SdSystemCatalog& registry() const { return registry_; }

  /// Non-const access to the registry (for FontInstaller).
  SdSystemCatalog& registry() { return registry_; }

  /// Mark the registry as needing re-discovery.
  /// Thread-safe: can be called from the web server task.
  void markRegistryDirty() { registry_.markDirty(); }

  /// If the registry is dirty, re-scan the SD card now and clear the flag.
  /// Used by the web UI so uploaded/deleted fonts appear in the list
  /// without waiting for the reader activity to run ensureLoaded().
  void refreshIfDirty() { registry_.refreshIfDirty(); }

 private:
  // Execute Rust-selected settings/vector effects in their original order.
  void runEffects(GfxRenderer& renderer, bool begin);

#if CROSSPOINT_VECTOR_FONTS
  // --- Vector (.ttf/.otf) font path (FreeInkFont via TtfEpdFont) -------------
  // Load/refresh the selected TTF family at the current reader size, register
  // it with the renderer, and track it so ensureSdCardFontReady() rebuilds its
  // glyph set per page. registryWasDirty forces a reload even if unchanged.
  void loadTtfFamily(const SdSystemFamily& family, GfxRenderer& renderer, bool registryWasDirty);
  // Unregister + free the active TTF font (and its UI-size fallbacks), if any.
  void unloadTtf(GfxRenderer& renderer);
  // Register the loaded TTF at each built-in UI size as a script fallback, so UI
  // text (book titles, list rows, menus, status bar) in scripts the built-in
  // fonts lack renders in the chosen TTF.
  void setupTtfUiFallbacks(GfxRenderer& renderer);
  // Open one style source file (resident if small, streamed if large) into
  // ttfSources_[style]. Returns false on open/read failure.
  bool openTtfSource(uint8_t style, const std::string& path);
  // Register every present source with `font` (shared bytes / file handles).
  void addTtfSources(TtfEpdFont& font);
  // Close/free all style sources.
  void freeTtfSources();
  // ReadFn for streamed sources: serves the PSRAM prefix cache first, SD after.
  static unsigned long prefixRead(void* ctx, unsigned long offset, unsigned char* buffer, unsigned long count);
#endif  // CROSSPOINT_VECTOR_FONTS

  // --- Sized variants of the reader font (CSS font-size, headings, drop caps) ---
  // Served to the renderer through FontVariantProvider. Variant ids derive from the
  // reader font id and point size, so ids cached in section files can be reloaded after
  // eviction or a reboot. Least recently used variants are evicted at the caps below.
  int resolveVariant(int fontId, float scale);
  bool loadVariant(int fontId);
  static int variantFontId(int readerFontId, uint8_t pointSize);

#if CROSSPOINT_VECTOR_FONTS
  uint32_t variantClock_ = 0;
#endif
  GfxRenderer* renderer_ = nullptr;

  SdSystemCatalog registry_;

#if CROSSPOINT_VECTOR_FONTS
  // One style source file. SMALL files are read fully into `bytes` (resident,
  // PSRAM when present); LARGE files stream from `file` (kept open) so a multi-MB
  // file never sits in RAM. All faces (reader + UI sizes) share these sources.
  struct TtfSource {
    // Resident form: the whole file. Streamed form: a PSRAM prefix cache of the
    // file head (cmap/loca/hmtx) — empty when PSRAM couldn't fund it.
    freeink::font::PsramVector<uint8_t> bytes;
    HalFile file;  // open handle (streamed form)
    bool streamed = false;
    unsigned long size = 0;
    bool present = false;
  };

  // Active TTF font (at most one reader-size vector family loaded at a time).
  std::unique_ptr<TtfEpdFont> ttf_;
  // Up to 4 style sources: 0=regular (required), 1=bold, 2=italic, 3=bold-italic.
  TtfSource ttfSources_[4];
  std::string ttfFamily_;     // loaded vector family name ("" = none)
  int ttfFontId_ = 0;         // renderer font id for ttf_ (0 = none)
  uint8_t ttfPointSize_ = 0;  // size ttf_ was built at
  // UI-size TTF fallbacks (share ttfSources_); parallel to their renderer font ids.
  std::vector<std::unique_ptr<TtfEpdFont>> ttfUi_;
  std::vector<int> ttfUiIds_;

  // Reader-font faces at other sizes (share ttfSources_).
  struct TtfVariant {
    int fontId;
    uint32_t lastUse;
    std::unique_ptr<TtfEpdFont> font;
  };
  static constexpr size_t MAX_TTF_VARIANTS = TextBlock::MAX_WORD_FONTS + 2;
  std::vector<TtfVariant> ttfVariants_;
  bool loadTtfVariant(uint8_t pointSize, int fontId);
  void clearTtfVariants(GfxRenderer& renderer);
#endif  // CROSSPOINT_VECTOR_FONTS
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
