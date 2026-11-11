#pragma once

#include <SdCardFontRegistry.h>

#include <cstdint>
class GfxRenderer;

using SdSystemCatalog = SdFontCatalog;
using SdSystemFamily = SdFontFamily;

/// Native startup/UI facade for Rust SD catalog policy and synchronous effects.
class SdCardFontSystem {
 public:
  SdCardFontSystem() = default;
  ~SdCardFontSystem() = default;
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
  void runEffects(GfxRenderer& renderer, bool begin);
  SdSystemCatalog registry_;
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
