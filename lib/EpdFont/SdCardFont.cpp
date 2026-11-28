#include <FontPsram.h>
#include <Logging.h>
#include <xp_ffi.h>
#include <xp_sd_physical.h>

#include <memory>

using freeink::font::psramDeleteArray;
using freeink::font::psramNewArray;

static void logPool(const char* path, bool seekFailed, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  switch (static_cast<XpSdfontDiagnostic>(event)) {
    case XP_SDFONT_DIAG_LigatureOpen:
      LOG_ERR("SDCF", "Failed to open .cpfont for ligatures: %s", path);
      break;
    case XP_SDFONT_DIAG_LigatureAlloc:
      LOG_ERR("SDCF", "Failed to allocate ligature pairs");
      break;
    case XP_SDFONT_DIAG_LigatureRead:
      if (seekFailed) {
        LOG_ERR("SDCF", "Failed to seek to ligature data");
      } else {
        LOG_ERR("SDCF", "Failed to read ligature pairs");
      }
      break;
    case XP_SDFONT_DIAG_LigaturesLoaded:
      LOG_DBG("SDCF", "Ligatures loaded: %u", a);
      break;
    case XP_SDFONT_DIAG_KernOpen:
      LOG_ERR("SDCF", "Failed to open .cpfont for mini kern: %s", path);
      break;
    case XP_SDFONT_DIAG_KernScratch:
      LOG_ERR("SDCF", "Failed to allocate mini kern scratch (%u bytes)", a);
      break;
    case XP_SDFONT_DIAG_KernClasses:
      LOG_ERR("SDCF", "Failed to read kern classes");
      break;
    case XP_SDFONT_DIAG_KernAlloc:
      LOG_ERR("SDCF", "Failed to allocate mini kern (%u+%u+%u bytes)", a, b, c);
      break;
    case XP_SDFONT_DIAG_KernRow:
      if (seekFailed) {
        LOG_ERR("SDCF", "Failed to seek to kern row %u", a);
      } else {
        LOG_ERR("SDCF", "Failed to read kern row %u", a);
      }
      break;
    case XP_SDFONT_DIAG_KernBuilt:
      LOG_DBG("SDCF", "Built mini kern: %u×%u matrix (%u bytes, full was %u×%u = %u bytes)", a, b, a * b, c, d, c * d);
      break;
    case XP_SDFONT_DIAG_Codepoints:
      LOG_ERR("SDCF", "Failed to allocate codepoint buffer (%u bytes)", a);
      break;
    case XP_SDFONT_DIAG_ArenaRetry:
      LOG_DBG("SDCF", "Arena retry: %u -> %u glyphs (%uB/glyph, maxAlloc=%u)", a, b, c, d);
      break;
    case XP_SDFONT_DIAG_ResidentCodepoints:
      LOG_ERR("SDCF", "OOM: resident kern codepoints (%u) for style %u", a, b);
      break;
    case XP_SDFONT_DIAG_Underuse:
      LOG_DBG("SDCF", "mini release (underuse): used=%u cap=%u", a, b);
      break;
    case XP_SDFONT_DIAG_Mapping:
      LOG_ERR("SDCF", "Failed to allocate mapping array for style %u", a);
      break;
    case XP_SDFONT_DIAG_Intervals:
      LOG_ERR("SDCF", "Failed to allocate mini intervals for style %u", a);
      break;
    case XP_SDFONT_DIAG_Glyphs:
      LOG_ERR("SDCF", "Failed to allocate mini glyphs for style %u", a);
      break;
    case XP_SDFONT_DIAG_ReadOrder:
      LOG_ERR("SDCF", "Failed to allocate read order for style %u", a);
      break;
    case XP_SDFONT_DIAG_PrewarmOpen:
      LOG_ERR("SDCF", "Failed to reopen .cpfont for prewarm (style %u)", a);
      break;
    case XP_SDFONT_DIAG_PrewarmGlyph:
      if (seekFailed) {
        LOG_ERR("SDCF", "Prewarm: failed to seek to glyph %d (style %u)", static_cast<int>(a), b);
      } else {
        LOG_ERR("SDCF", "Prewarm: short glyph read (style %u, glyph %d)", b, static_cast<int>(a));
      }
      break;
    case XP_SDFONT_DIAG_RetryBitmap:
      LOG_DBG("SDCF", "Retrying bitmap after advance-cache release: maxAlloc=%u -> %u", a, b);
      break;
    case XP_SDFONT_DIAG_ReserveBitmap:
      LOG_ERR("SDCF", "Failed to reserve mini bitmap (%u bytes) for style %u", a, b);
      break;
    case XP_SDFONT_DIAG_ReservedBitmap:
      LOG_DBG("SDCF", "Reserved bitmap before metadata rebuild (%u bytes)", a);
      break;
    case XP_SDFONT_DIAG_Bitmap:
      LOG_ERR("SDCF", "Failed to allocate mini bitmap (%u bytes) for style %u", a, b);
      break;
    case XP_SDFONT_DIAG_BitmapOrder:
      LOG_ERR("SDCF", "Failed to allocate bitmap read order for style %u", a);
      break;
    case XP_SDFONT_DIAG_PrewarmBitmap:
      if (seekFailed) {
        LOG_ERR("SDCF", "Prewarm: failed to seek to bitmap (style %u)", a);
      } else {
        LOG_ERR("SDCF", "Prewarm: short bitmap read (style %u)", a);
      }
      break;
    case XP_SDFONT_DIAG_AdvanceMerge:
      LOG_ERR("SDCF", "mergeIntoAdvanceTable: alloc failed (%u entries) style %u", a, b);
      break;
    case XP_SDFONT_DIAG_AdvanceMapping:
      LOG_ERR("SDCF", "buildAdvanceTable: failed to allocate mappings for style %u", a);
      break;
    case XP_SDFONT_DIAG_AdvanceOpen:
      LOG_ERR("SDCF", "buildAdvanceTable: failed to open .cpfont for style %u", a);
      break;
    case XP_SDFONT_DIAG_AdvanceStaging:
      LOG_ERR("SDCF", "buildAdvanceTable: failed to allocate staging for style %u", a);
      break;
    case XP_SDFONT_DIAG_AdvanceGlyph:
      if (seekFailed) {
        LOG_ERR("SDCF", "buildAdvanceTable: failed to seek to glyph %d (style %u)", static_cast<int>(a), b);
      } else {
        LOG_ERR("SDCF", "buildAdvanceTable: short glyph read (style %u, glyph %d)", b, static_cast<int>(a));
      }
      break;
    case XP_SDFONT_DIAG_AdvanceLoaded:
      LOG_DBG("SDCF", "Advance table style %u: +%u from SD, total=%u/%u", a, b, c, d);
      break;
    case XP_SDFONT_DIAG_AdvanceGrow:
      LOG_DBG("SDCF", "Advance table pre-grow failed for style %u; the merge grows it", a);
      break;
    case XP_SDFONT_DIAG_AdvanceCodepoints:
      LOG_ERR("SDCF", "buildAdvanceTable: failed to allocate codepoint buffer (%u bytes)", a);
      break;
    case XP_SDFONT_DIAG_OverflowOpen:
      LOG_ERR("SDCF", "Overflow: failed to open .cpfont");
      break;
    case XP_SDFONT_DIAG_OverflowGlyph:
      if (seekFailed) {
        LOG_ERR("SDCF", "Overflow: failed to seek to glyph for U+%04X style %u", a, b);
      } else {
        LOG_ERR("SDCF", "Overflow: failed to read glyph metadata for U+%04X style %u", a, b);
      }
      break;
    case XP_SDFONT_DIAG_OverflowAlloc:
      LOG_ERR("SDCF", "Overflow: failed to allocate %u bytes for U+%04X bitmap", a, b);
      break;
    case XP_SDFONT_DIAG_OverflowBitmap:
      if (seekFailed) {
        LOG_ERR("SDCF", "Overflow: failed to seek to bitmap for U+%04X", a);
      } else {
        LOG_ERR("SDCF", "Overflow: failed to read bitmap for U+%04X", a);
      }
      break;
    case XP_SDFONT_DIAG_OverflowLoaded:
      LOG_DBG("SDCF", "Overflow: loaded U+%04X style %u on demand (slot %u/%u)", a, b, c, d);
      break;
  }
}

namespace {
void logParseFailure(const XpSdfontParseDiagnostic& detail, uint32_t style = 0, uint32_t index = 0) {
  const auto* a = detail.args;
  switch (detail.kind) {
    case XpSdfontParseError::ShortInput:
      break;  // The HAL read guards report short records before parsing.
    case XpSdfontParseError::Magic:
      LOG_ERR("SDCF", "Invalid magic bytes");
      break;
    case XpSdfontParseError::Version:
      LOG_ERR("SDCF", "Unsupported version: %u (expected %u)", a[0], a[1]);
      break;
    case XpSdfontParseError::StyleCount:
      LOG_ERR("SDCF", "Invalid style count: %u", a[0]);
      break;
    case XpSdfontParseError::StyleId:
      LOG_ERR("SDCF", "Invalid styleId %u in TOC", a[0]);
      break;
    case XpSdfontParseError::DuplicateStyle:
      LOG_ERR("SDCF", "Duplicate styleId %u in TOC", a[0]);
      break;
    case XpSdfontParseError::Counts:
      LOG_ERR("SDCF", "Style %u: unreasonable counts (iv=%u, gl=%u, kL=%u, kR=%u)", style, a[0], a[1], a[2], a[3]);
      break;
    case XpSdfontParseError::IntervalOrder:
      LOG_ERR("SDCF", "Style %u: invalid interval %u (first 0x%lX > last 0x%lX)", style, index,
              static_cast<unsigned long>(a[0]), static_cast<unsigned long>(a[1]));
      break;
    case XpSdfontParseError::IntervalLayout:
      LOG_ERR("SDCF", "Style %u: invalid interval layout at %u (overlap=%d span=%u offMis=%d offOver=%d)", style, index,
              static_cast<int>(a[0]), a[1], static_cast<int>(a[2]), static_cast<int>(a[3]));
      break;
  }
}
}  // namespace

static void logLoad(uint32_t event, uint32_t a, uint32_t b, uint32_t c) {
  switch (static_cast<XpSdfontLoadEvent>(event)) {
    case XpSdfontLoadEvent::HeaderRead:
      LOG_ERR("SDCF", "Failed to read header");
      break;
    case XpSdfontLoadEvent::StyleAllocation:
      LOG_ERR("SDCF", "Failed to allocate %zu bytes for style records", static_cast<size_t>(a));
      break;
    case XpSdfontLoadEvent::TocRead:
      LOG_ERR("SDCF", "Failed to read style TOC entry %u", a);
      break;
    case XpSdfontLoadEvent::IntervalSeek:
      LOG_ERR("SDCF", "Failed to seek to intervals for style %u", a);
      break;
    case XpSdfontLoadEvent::IntervalRead:
      LOG_ERR("SDCF", "Failed to read interval %u for style %u", a, b);
      break;
    case XpSdfontLoadEvent::Shared:
      LOG_DBG("SDCF", "Style %u: sharing a loaded %u-interval table (%u B not allocated)", a, b, c);
      break;
    case XpSdfontLoadEvent::IntervalRewind:
      LOG_ERR("SDCF", "Failed to seek back to intervals for style %u", a);
      break;
    case XpSdfontLoadEvent::CompactAllocation:
      LOG_ERR("SDCF", "Failed to allocate compact intervals for style %u", a);
      break;
    case XpSdfontLoadEvent::CompactRead:
      LOG_ERR("SDCF", "Failed to read compact interval %u for style %u", a, b);
      break;
    case XpSdfontLoadEvent::IntervalAllocation:
      LOG_ERR("SDCF", "Failed to allocate %u intervals for style %u", a, b);
      break;
    case XpSdfontLoadEvent::IntervalsRead:
      LOG_ERR("SDCF", "Failed to read intervals for style %u", a);
      break;
    case XpSdfontLoadEvent::KernIndexAllocation:
      LOG_DBG("SDCF", "No kern block index for style %u; reading whole class tables", a);
      break;
  }
}

#ifdef XP_SD_FONT_CATALOG
namespace {
enum class CatalogDiagnostic : uint8_t {
  MissingRoot = 1,
  NotDirectory,
  Duplicate,
  Family,
  Vector,
  NameOom,
  BadPath,
  Promoted,
  Selected,
  Complete
};
enum class SystemDiagnostic : uint8_t {
  Snap = 1,
  Ready,
  LoadedBoot,
  FailedBoot,
  MissingBoot,
  Dirty,
  Disappeared,
  Reload,
  Loaded,
  Failed,
  Missing,
  NoCoverage,
  MissingUi,
  SkipVariant,
  VariantLoaded
};
}  // namespace
extern "C" uint8_t* xp_native_sd_allocate(size_t bytes, uint8_t domain) {
  switch (static_cast<XpSdAllocation>(domain)) {
    case XP_SD_INTERNAL:
      return new (std::nothrow) uint8_t[bytes];
    case XP_SD_RESIDENT:
      return psramNewArray<uint8_t>(bytes);
    case XP_SD_REQUIRED:
      return new uint8_t[bytes];
  }
  return nullptr;
}
extern "C" void xp_native_sd_free(uint8_t* data, bool resident) {
  if (resident)
    psramDeleteArray(data);
  else
    delete[] data;
}
extern "C" void xp_native_sd_diagnostic(const char* path, uint32_t event, const uint32_t* a, uint32_t style,
                                        uint32_t index, bool seekFailed, const char* other) {
#ifdef BOARD_HAS_PSRAM
  if (event == 0x600) {
    if (a[1]) LOG_DBG("SDFS", "Cached %u KB TTF prefix in PSRAM", a[1]);
    LOG_DBG("SDFS", "Streaming TTF %s (%u KB) from SD", path, a[0]);
    return;
  }
#endif
  if (event >= 0x500) {
    switch (static_cast<SystemDiagnostic>(event - 0x500)) {
      case SystemDiagnostic::Snap:
        LOG_DBG("SDFS", "Font size %u unavailable, snapping to %u", a[0], a[1]);
        break;
      case SystemDiagnostic::Ready:
        LOG_DBG("SDFS", "SD font system ready (%d families discovered)", static_cast<int>(a[0]));
        break;
      case SystemDiagnostic::LoadedBoot:
        LOG_DBG("SDFS", "Loaded SD card font family: %s", path);
        break;
      case SystemDiagnostic::FailedBoot:
        LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", path);
        break;
      case SystemDiagnostic::MissingBoot:
        LOG_DBG("SDFS", "SD font family not found on card: %s (clearing)", path);
        break;
      case SystemDiagnostic::Dirty:
        LOG_DBG("SDFS", "Registry dirty — re-discovering fonts");
        break;
      case SystemDiagnostic::Disappeared:
        LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", path);
        break;
      case SystemDiagnostic::Reload:
        LOG_DBG("SDFS", "Reloading %s: size %u -> %u%s", path, a[0], a[1], a[2] ? " [registry dirty]" : "");
        break;
      case SystemDiagnostic::Loaded:
        LOG_DBG("SDFS", "Loaded SD font family: %s", path);
        break;
      case SystemDiagnostic::Failed:
        LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", path);
        break;
      case SystemDiagnostic::Missing:
        LOG_DBG("SDFS", "SD font family not found: %s (clearing)", path);
        break;
      case SystemDiagnostic::NoCoverage:
        LOG_DBG("SDFS", "%s has no fallback-script coverage - skipping UI fallback sizes", path);
        break;
      case SystemDiagnostic::MissingUi:
        LOG_DBG("SDFS", "No %u pt SD glyphs for UI fallback in %s", a[0], path);
        break;
      case SystemDiagnostic::SkipVariant:
        LOG_DBG("SDFS", "Skipping %upt variant: largest free block %u", a[0], a[1]);
        break;
      case SystemDiagnostic::VariantLoaded:
        LOG_DBG("SDFS", "Loaded %s %upt variant (heap free %u)", path, a[0], a[1]);
        break;
    }
    return;
  }
  if (event >= 0x400) {
    switch (static_cast<CatalogDiagnostic>(event - 0x400)) {
      case CatalogDiagnostic::MissingRoot:
        LOG_DBG("SDREG", "Fonts directory not found: %s", path);
        break;
      case CatalogDiagnostic::NotDirectory:
        LOG_ERR("SDREG", "Fonts path is not a directory: %s", path);
        break;
      case CatalogDiagnostic::Duplicate:
        LOG_ERR("SDREG", "Duplicate font %s in %s — skipping", path, other);
        break;
      case CatalogDiagnostic::Family:
        LOG_DBG("SDREG", "Found family: %s (%d files) in %s", path, static_cast<int>(a[0]), other);
        break;
      case CatalogDiagnostic::Vector:
        LOG_DBG("SDREG", "Found vector font: %s in %s", path, other);
        break;
      case CatalogDiagnostic::NameOom:
        LOG_ERR("SDREG", "OOM: %zu byte filename %s", static_cast<size_t>(a[0]), path);
        break;
      case CatalogDiagnostic::BadPath:
        LOG_ERR("SDREG", "Path too long for %s in %s — skipping", path, other);
        break;
      case CatalogDiagnostic::Promoted:
        LOG_DBG("SDREG", "No upright face in %s — promoting %s", path, other);
        break;
      case CatalogDiagnostic::Selected:
        LOG_DBG("SDREG", "%s: %u of %u faces selected by weight", path, a[0], a[1]);
        break;
      case CatalogDiagnostic::Complete:
        LOG_DBG("SDREG", "Discovery complete: %d families", static_cast<int>(a[0]));
        break;
    }
    return;
  }
  if (event < 0x100) {
    logPool(path, seekFailed, event, a[0], a[1], a[2], a[3]);
    return;
  }
  if (event < 0x200) {
    logLoad(event - 0x100, a[0], a[1], a[2]);
    return;
  }
  if (event < 0x300) {
    XpSdfontParseDiagnostic detail{static_cast<XpSdfontParseError>(event - 0x200), {a[0], a[1], a[2], a[3]}};
    logParseFailure(detail, style, index);
    return;
  }
  LOG_ERR("SDCF", "Corrupt glyph for U+%04X in %s (%ux%u, %u bytes at %u); drawing it as missing",
          static_cast<unsigned>(a[0]), path, a[1] >> 16, a[1] & 65535, a[2], static_cast<unsigned>(a[3]));
}
#else
extern "C" uint8_t* xp_native_sd_allocate(size_t bytes, bool resident) {
  return resident ? psramNewArray<uint8_t>(bytes) : new (std::nothrow) uint8_t[bytes];
}
extern "C" void xp_native_sd_free(uint8_t* data, bool resident) {
  if (resident)
    psramDeleteArray(data);
  else
    delete[] data;
}
extern "C" void xp_native_sd_diagnostic(const char* path, uint32_t event, const uint32_t* a, uint32_t style,
                                        uint32_t index, bool seekFailed) {
  if (event < 0x100) {
    logPool(path, seekFailed, event, a[0], a[1], a[2], a[3]);
    return;
  }
  if (event < 0x200) {
    logLoad(event - 0x100, a[0], a[1], a[2]);
    return;
  }
  if (event < 0x300) {
    XpSdfontParseDiagnostic detail{static_cast<XpSdfontParseError>(event - 0x200), {a[0], a[1], a[2], a[3]}};
    logParseFailure(detail, style, index);
    return;
  }
  LOG_ERR("SDCF", "Corrupt glyph for U+%04X in %s (%ux%u, %u bytes at %u); drawing it as missing",
          static_cast<unsigned>(a[0]), path, a[1] >> 16, a[1] & 65535, a[2], static_cast<unsigned>(a[3]));
}
#endif
