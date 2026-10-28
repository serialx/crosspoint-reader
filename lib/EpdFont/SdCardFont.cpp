#include "SdCardFont.h"

#include <FontPsram.h>  // PSRAM-preferring resident buffers (font memory lift)
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>

#include "EpdFontFamily.h"
#include "FontTableGeneration.h"

// Resident SD-font buffers (glyph/kern arenas, interval + advance tables, the
// overflow ring) are placed in PSRAM when the board has it — freeing scarce
// internal SRAM — via these helpers. On a no-PSRAM board they fall back to the
// same internal heap `new[]`/`delete[]` used before, so behavior (and the
// fragmentation-avoidance logic below) is unchanged there. Every buffer routed
// through psramNewArray MUST be released with psramDeleteArray.
using freeink::font::psramDeleteArray;
using freeink::font::psramNewArray;

static_assert(sizeof(EpdGlyph) == 16, "EpdGlyph must be 16 bytes to match .cpfont file layout");
static_assert(sizeof(EpdUnicodeInterval) == 12, "EpdUnicodeInterval must be 12 bytes to match .cpfont file layout");
static_assert(sizeof(EpdKernClassEntry) == 3, "EpdKernClassEntry must be 3 bytes to match .cpfont file layout");
static_assert(sizeof(EpdLigaturePair) == 8, "EpdLigaturePair must be 8 bytes to match .cpfont file layout");

namespace {

constexpr uint32_t HEADER_SIZE = 32;
constexpr uint32_t STYLE_TOC_ENTRY_SIZE = 32;
static_assert(CPFONT_VERSION == 4, "Rust SD font parser requires v4");

// Interval tables are shared by content across every loaded SdCardFont. The styles of one file and
// the sizes of one family almost always cover the same codepoints, and on a broad CJK font each
// copy is tens of KB (48 KB for a 4000-interval table above the BMP). Fonts load and unload on one
// task, as SdCardFontManager's own font maps do, so the registry takes no lock.
struct SharedIntervals {
  const void* table = nullptr;
  uint32_t count = 0;
  bool bmp16 = false;
  uint16_t refs = 0;
};
constexpr size_t MAX_SHARED_INTERVALS = 16;
SharedIntervals sharedIntervals[MAX_SHARED_INTERVALS];

}  // namespace

struct SdCardFont::PoolIo {
  SdCardFont* owner;
  HalFile files[2];
  uint8_t depth = 0;
  bool seekFailed = false;
};

XpSdfontServices SdCardFont::cacheServices(PoolIo* io) {
  return {io,
          [](void* raw) {
            auto& io = *static_cast<PoolIo*>(raw);
            if (io.depth == 2) return false;
            if (!Storage.openFileForRead("SDCF", io.owner->filePath_, io.files[io.depth])) {
              io.files[io.depth] = HalFile{};
              return false;
            }
            ++io.depth;
            return true;
          },
          [](void* raw, bool release) {
            auto& io = *static_cast<PoolIo*>(raw);
            if (!io.depth) return;
            if (release)
              io.files[--io.depth] = HalFile{};
            else
              io.files[io.depth - 1].close();
          },
          [](void* raw, uint32_t offset, uint8_t* dest, size_t length, bool seek) {
            auto& io = *static_cast<PoolIo*>(raw);
            if (!io.depth) return false;
            auto& file = io.files[io.depth - 1];
            io.seekFailed = seek && !file.seekSet(offset);
            if (io.seekFailed) {
              return false;
            }
            const int read = file.read(dest, length);
            if (read != static_cast<int>(length)) {
              return false;
            }
            return true;
          },
          [](size_t bytes, bool resident) -> uint8_t* {
            // Rust owns the returned buffer and releases it through the paired callback.
            auto* data = resident ? psramNewArray<uint8_t>(bytes) : new (std::nothrow) uint8_t[bytes];
            return data;
          },
          [](uint8_t* data, bool resident) {
            if (resident)
              psramDeleteArray(data);
            else
              delete[] data;
          },
          [](bool largest) -> uint32_t { return largest ? ESP.getMaxAllocHeap() : ESP.getFreeHeap(); },
          []() -> uint32_t { return millis(); },
          [](void* raw, const void* glyph, uint32_t cp) {
            auto* self = static_cast<PoolIo*>(raw)->owner;
            const auto& g = *static_cast<const EpdGlyph*>(glyph);
            if (!self->corruptGlyphsLogged_) corruptGlyphNotice_ = true;
            if (self->corruptGlyphsLogged_ < 3) {
              ++self->corruptGlyphsLogged_;
              LOG_ERR("SDCF", "Corrupt glyph for U+%04X in %s (%ux%u, %u bytes at %u); drawing it as missing",
                      static_cast<unsigned>(cp), self->filePath_, g.width, g.height, g.dataLength,
                      static_cast<unsigned>(g.dataOffset));
            }
          },
          [](void* raw, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
            auto& io = *static_cast<PoolIo*>(raw);
            switch (static_cast<XpSdfontDiagnostic>(event)) {
              case XP_SDFONT_DIAG_LigatureOpen:
                LOG_ERR("SDCF", "Failed to open .cpfont for ligatures: %s", io.owner->filePath_);
                break;
              case XP_SDFONT_DIAG_LigatureAlloc:
                LOG_ERR("SDCF", "Failed to allocate ligature pairs");
                break;
              case XP_SDFONT_DIAG_LigatureRead:
                if (io.seekFailed) {
                  LOG_ERR("SDCF", "Failed to seek to ligature data");
                } else {
                  LOG_ERR("SDCF", "Failed to read ligature pairs");
                }
                break;
              case XP_SDFONT_DIAG_LigaturesLoaded:
                LOG_DBG("SDCF", "Ligatures loaded: %u", a);
                break;
              case XP_SDFONT_DIAG_KernOpen:
                LOG_ERR("SDCF", "Failed to open .cpfont for mini kern: %s", io.owner->filePath_);
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
                if (io.seekFailed) {
                  LOG_ERR("SDCF", "Failed to seek to kern row %u", a);
                } else {
                  LOG_ERR("SDCF", "Failed to read kern row %u", a);
                }
                break;
              case XP_SDFONT_DIAG_KernBuilt:
                LOG_DBG("SDCF", "Built mini kern: %u×%u matrix (%u bytes, full was %u×%u = %u bytes)", a, b, a * b, c,
                        d, c * d);
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
                if (io.seekFailed) {
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
                if (io.seekFailed) {
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
                if (io.seekFailed) {
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
                if (io.seekFailed) {
                  LOG_ERR("SDCF", "Overflow: failed to seek to glyph for U+%04X style %u", a, b);
                } else {
                  LOG_ERR("SDCF", "Overflow: failed to read glyph metadata for U+%04X style %u", a, b);
                }
                break;
              case XP_SDFONT_DIAG_OverflowAlloc:
                LOG_ERR("SDCF", "Overflow: failed to allocate %u bytes for U+%04X bitmap", a, b);
                break;
              case XP_SDFONT_DIAG_OverflowBitmap:
                if (io.seekFailed) {
                  LOG_ERR("SDCF", "Overflow: failed to seek to bitmap for U+%04X", a);
                } else {
                  LOG_ERR("SDCF", "Overflow: failed to read bitmap for U+%04X", a);
                }
                break;
              case XP_SDFONT_DIAG_OverflowLoaded:
                LOG_DBG("SDCF", "Overflow: loaded U+%04X style %u on demand (slot %u/%u)", a, b, c, d);
                break;
            }
          }};
}

XpSdfontPool SdCardFont::cacheView(const XpSdfontServices* services) const {
  auto* self = const_cast<SdCardFont*>(this);
  return {self->styles_.get(),
          self->styleSlots_,
          reinterpret_cast<void**>(self->advanceTable_),
          self->advanceTableSize_,
          self->advanceTableCapacity_,
          self->overflow_,
          &self->overflowCount_,
          &self->overflowNext_,
          &self->stats_,
          self->fileSize_,
          self->loaded_,
          services};
}

SdCardFont::~SdCardFont() { freeAll(); }

// --- Per-style free/cleanup ---

void SdCardFont::freeStyleMiniData(PerStyle& s) {
  auto services = cacheServices(nullptr);
  xp_sdfont_pool_free_mini(&s, &services);
}

void SdCardFont::freeStyleLigatures(PerStyle& s) {
  auto services = cacheServices(nullptr);
  xp_sdfont_pool_free_ligatures(&s, &services);
}

void SdCardFont::releaseIntervals(PerStyle& s) {
  const void* table = s.intervalsAreBmp16 ? static_cast<const void*>(s.bmpIntervals) : s.fullIntervals;
  // A table the full registry could not take is this style's alone.
  bool lastReference = true;
  for (auto& entry : sharedIntervals) {
    if (!table || entry.table != table) continue;
    lastReference = --entry.refs == 0;
    if (lastReference) entry = SharedIntervals{};
    break;
  }
  if (lastReference) {
    psramDeleteArray(s.fullIntervals);
    psramDeleteArray(s.bmpIntervals);
  }
  s.fullIntervals = nullptr;
  s.bmpIntervals = nullptr;
}

void SdCardFont::freeStyleAll(PerStyle& s) {
  freeStyleMiniData(s);
  releaseIntervals(s);
  s.intervalsAreBmp16 = false;
  freeStyleLigatures(s);
  psramDeleteArray(s.kernBlockIndex);
  s.kernBlockIndex = nullptr;
  s.kernBlockIndexReady = false;
}

// --- Global free/cleanup ---

void SdCardFont::releaseResidentCaches() {
  ++fontTableGeneration;
  clearOverflow();
  clearPersistentCache();
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (!hasStyle(i)) continue;
    freeStyleMiniData(styleData(i));  // also frees mini kern and restores the stub EpdFontData
    freeStyleLigatures(styleData(i));
    applyGlyphMissCallback(i);  // keep the on-demand miss path alive on the stub
  }
}

void SdCardFont::freeAll() {
  ++fontTableGeneration;
  clearOverflow();
  clearPersistentCache();
  for (uint8_t i = 0; i < styleCount_; i++) {
    freeStyleAll(styles_[i]);
  }
  styles_.reset();
  memset(styleSlots_, 0, sizeof(styleSlots_));
  styleCount_ = 0;
  contentHash_ = 0;
  loaded_ = false;
}

void SdCardFont::clearOverflow() {
  auto services = cacheServices(nullptr);
  auto pool = cacheView(&services);
  xp_sdfont_pool_clear_overflow(&pool);
}

namespace {
constexpr uint16_t KERN_CLASS_BLOCK = 64;
uint16_t kernClassBlocks(uint16_t count) { return (count + KERN_CLASS_BLOCK - 1) / KERN_CLASS_BLOCK; }
}  // namespace

// --- Glyph miss callback ---

void SdCardFont::applyGlyphMissCallback(uint8_t styleIdx) {
  overflowCtx_[styleIdx].self = this;
  overflowCtx_[styleIdx].styleIdx = styleIdx;

  auto& s = styleData(styleIdx);
  s.stubData.glyphMissHandler = &SdCardFont::onGlyphMiss;
  s.stubData.glyphMissCtx = &overflowCtx_[styleIdx];
  s.stubData.coverageHandler = &SdCardFont::onCoverageQuery;
}

bool SdCardFont::onCoverageQuery(void* ctx, const uint32_t codepoint) {
  const auto* oc = static_cast<OverflowContext*>(ctx);
  const auto pool = oc->self->cacheView();
  return xp_sdfont_pool_coverage(&pool, codepoint, oc->styleIdx);
}

// --- Load ---

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

bool SdCardFont::load(const char* path) {
  ++fontTableGeneration;
  freeAll();
  if (strlen(path) >= sizeof(filePath_)) {
    LOG_ERR("SDCF", "Path too long (%zu bytes, max %zu)", strlen(path), sizeof(filePath_) - 1);
    return false;
  }
  strncpy(filePath_, path, sizeof(filePath_) - 1);
  filePath_[sizeof(filePath_) - 1] = '\0';

  HalFile file;
  if (!Storage.openFileForRead("SDCF", path, file)) {
    LOG_ERR("SDCF", "Failed to open .cpfont: %s", path);
    return false;
  }
  fileSize_ = static_cast<uint32_t>(file.fileSize());
  corruptGlyphsLogged_ = 0;

  // Read and validate global header
  uint8_t headerBuf[HEADER_SIZE];
  if (file.read(headerBuf, HEADER_SIZE) != HEADER_SIZE) {
    LOG_ERR("SDCF", "Failed to read header");
    return false;
  }

  XpSdfontDirectory directory{};
  XpSdfontParseDiagnostic diagnostic{};
  if (!xp_sdfont_header_diagnostic(headerBuf, sizeof(headerBuf), &directory, &diagnostic)) {
    logParseFailure(diagnostic);
    return false;
  }
  const uint8_t styleCount = directory.styleCount;

  styles_ = makeUniqueNoThrow<PerStyle[]>(styleCount);
  if (!styles_) {
    LOG_ERR("SDCF", "Failed to allocate %zu bytes for style records", styleCount * sizeof(PerStyle));
    return false;
  }
  styleCount_ = styleCount;

  // Read style TOC
  for (uint8_t i = 0; i < styleCount; i++) {
    uint8_t tocBuf[STYLE_TOC_ENTRY_SIZE];
    if (file.read(tocBuf, STYLE_TOC_ENTRY_SIZE) != STYLE_TOC_ENTRY_SIZE) {
      LOG_ERR("SDCF", "Failed to read style TOC entry %u", i);
      freeAll();
      return false;
    }

    XpSdfontStyle parsed{};
    if (!xp_sdfont_style_diagnostic(tocBuf, sizeof(tocBuf), &directory, &parsed, &diagnostic)) {
      logParseFailure(diagnostic, tocBuf[0]);
      freeAll();
      return false;
    }
    styleSlots_[parsed.id] = i + 1;
    auto& s = styleData(parsed.id);
    s.header = parsed.header;
    s.intervalsFileOffset = parsed.offsets[0];
    s.glyphsFileOffset = parsed.offsets[1];
    s.kernLeftFileOffset = parsed.offsets[2];
    s.kernRightFileOffset = parsed.offsets[3];
    s.kernMatrixFileOffset = parsed.offsets[4];
    s.ligatureFileOffset = parsed.offsets[5];
    s.bitmapFileOffset = parsed.offsets[6];
  }

  contentHash_ = directory.hash;

  // Load full intervals into RAM for each present style. BMP-only fonts with
  // fewer than 65536 glyphs use a compact 6-byte interval table instead of the
  // on-disk 12-byte table; large sparse CJK subsets otherwise keep tens of KB
  // of always-resident heap just for lookup metadata.
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (!hasStyle(i)) continue;
    auto& s = styleData(i);

    if (!file.seekSet(s.intervalsFileOffset)) {
      LOG_ERR("SDCF", "Failed to seek to intervals for style %u", i);
      freeAll();
      return false;
    }

    // Validate interval contents before any later code (findGlobalGlyphIndex,
    // glyph reads) trusts them. A malformed file could otherwise drive
    // out-of-range glyph indices into bogus on-disk reads.
    XpSdfontIntervals intervalState{0, 0, s.header.glyphCount <= UINT16_MAX};
    EpdUnicodeInterval iv{};

    // A loaded table with this exact content -- another style of this file, or another size of
    // this family -- is shared instead of copied (see SharedIntervals). That saves the PEAK too:
    // allocating first and de-duplicating afterwards would still need both tables at once, which
    // is what fails on a tight heap. The comparison rides along with the validation read below,
    // so it costs no second pass and no buffer. A table stays a candidate only while it has
    // matched every record so far.
    uint16_t shareCandidates = 0;
    for (size_t k = 0; k < MAX_SHARED_INTERVALS; k++) {
      if (sharedIntervals[k].table && sharedIntervals[k].count == s.header.intervalCount) {
        shareCandidates |= static_cast<uint16_t>(1u << k);
      }
    }
    for (uint32_t j = 0; j < s.header.intervalCount; ++j) {
      if (file.read(reinterpret_cast<uint8_t*>(&iv), sizeof(iv)) != sizeof(iv)) {
        LOG_ERR("SDCF", "Failed to read interval %u for style %u", j, i);
        freeAll();
        return false;
      }
      if (!xp_sdfont_interval_diagnostic(reinterpret_cast<const uint8_t*>(&iv), sizeof(iv), s.header.glyphCount, j,
                                         &intervalState, &iv, &diagnostic)) {
        logParseFailure(diagnostic, i, j);
        freeAll();
        return false;
      }
      for (size_t k = 0; k < MAX_SHARED_INTERVALS && shareCandidates != 0; k++) {
        if ((shareCandidates & (1u << k)) == 0) continue;
        const auto& shared = sharedIntervals[k];
        // Compared by value, so an above-BMP record never equals a compact one and drops out here.
        bool same;
        if (shared.bmp16) {
          const auto& r = static_cast<const PerStyle::BmpInterval16*>(shared.table)[j];
          same = r.first == iv.first && r.last == iv.last && r.offset == iv.offset;
        } else {
          const auto& r = static_cast<const EpdUnicodeInterval*>(shared.table)[j];
          same = r.first == iv.first && r.last == iv.last && r.offset == iv.offset;
        }
        if (!same) shareCandidates &= static_cast<uint16_t>(~(1u << k));
      }
    }

    const bool canUseBmp16 = intervalState.bmp16;

    // Survived every record: share that table instead of allocating a copy.
    bool shared = false;
    for (size_t k = 0; k < MAX_SHARED_INTERVALS && shareCandidates != 0; k++) {
      if ((shareCandidates & (1u << k)) == 0) continue;
      auto& entry = sharedIntervals[k];
      // Identical content can still be held in the other resident form when the two fonts
      // disagree on glyph count; aliasing across forms would misread the table.
      if (entry.bmp16 != canUseBmp16) continue;
      // The tables are never written after loading, hence the const_cast.
      if (entry.bmp16) {
        s.bmpIntervals = static_cast<PerStyle::BmpInterval16*>(const_cast<void*>(entry.table));
      } else {
        s.fullIntervals = static_cast<EpdUnicodeInterval*>(const_cast<void*>(entry.table));
      }
      s.intervalsAreBmp16 = entry.bmp16;
      entry.refs++;
      shared = true;
      LOG_DBG("SDCF", "Style %u: sharing a loaded %u-interval table (%u B not allocated)", i, s.header.intervalCount,
              s.header.intervalCount * (canUseBmp16 ? 6u : static_cast<uint32_t>(sizeof(EpdUnicodeInterval))));
      break;
    }

    // Only the allocate-and-read path below needs the records again; a shared style is done.
    if (!shared && !file.seekSet(s.intervalsFileOffset)) {
      LOG_ERR("SDCF", "Failed to seek back to intervals for style %u", i);
      freeAll();
      return false;
    }

    if (shared) {
      // Aliased above; fall through to the stub/metadata setup without touching the table.
    } else if (canUseBmp16) {
      s.bmpIntervals = psramNewArray<PerStyle::BmpInterval16>(s.header.intervalCount);
      if (!s.bmpIntervals) {
        LOG_ERR("SDCF", "Failed to allocate compact intervals for style %u", i);
        freeAll();
        return false;
      }
      for (uint32_t j = 0; j < s.header.intervalCount; ++j) {
        if (file.read(reinterpret_cast<uint8_t*>(&iv), sizeof(iv)) != sizeof(iv)) {
          LOG_ERR("SDCF", "Failed to read compact interval %u for style %u", j, i);
          freeAll();
          return false;
        }
        s.bmpIntervals[j] = {static_cast<uint16_t>(iv.first), static_cast<uint16_t>(iv.last),
                             static_cast<uint16_t>(iv.offset)};
      }
      s.intervalsAreBmp16 = true;
    } else {
      s.fullIntervals = psramNewArray<EpdUnicodeInterval>(s.header.intervalCount);
      if (!s.fullIntervals) {
        LOG_ERR("SDCF", "Failed to allocate %u intervals for style %u", s.header.intervalCount, i);
        freeAll();
        return false;
      }
      size_t intervalsBytes = s.header.intervalCount * sizeof(EpdUnicodeInterval);
      if (file.read(reinterpret_cast<uint8_t*>(s.fullIntervals), intervalsBytes) != static_cast<int>(intervalsBytes)) {
        LOG_ERR("SDCF", "Failed to read intervals for style %u", i);
        freeAll();
        return false;
      }
    }
    if (!shared) {
      // Offer the complete table to later loads. With the registry full it stays private.
      for (auto& entry : sharedIntervals) {
        if (entry.table) continue;
        entry.table = s.intervalsAreBmp16 ? static_cast<const void*>(s.bmpIntervals) : s.fullIntervals;
        entry.count = s.header.intervalCount;
        entry.bmp16 = s.intervalsAreBmp16;
        entry.refs = 1;
        break;
      }
    }

    if (s.header.kernLeftEntryCount > 0 && s.header.kernRightEntryCount > 0) {
      // Optional: without it, every mini kern build reads the whole class tables.
      s.kernBlockIndex = psramNewArray<uint16_t>(kernClassBlocks(s.header.kernLeftEntryCount) + 1 +
                                                 kernClassBlocks(s.header.kernRightEntryCount) + 1);
      if (!s.kernBlockIndex) LOG_DBG("SDCF", "No kern block index for style %u; reading whole class tables", i);
    }

    // Initialize stub data
    memset(&s.stubData, 0, sizeof(s.stubData));
    s.stubData.advanceY = s.header.advanceY;
    s.stubData.ascender = s.header.ascender;
    s.stubData.descender = s.header.descender;
    s.stubData.is2Bit = s.header.is2Bit;

    s.epdFont.data = &s.stubData;
    applyGlyphMissCallback(i);
  }

  loaded_ = true;

  LOG_DBG("SDCF", "Loaded: %s (v%u, %u styles)", path, CPFONT_VERSION, styleCount_);
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (!hasStyle(i)) continue;
    const auto& h = styleData(i).header;
    LOG_DBG("SDCF", "  style[%u]: %u intervals, %u glyphs, advY=%u, asc=%d, desc=%d, kernL=%u, kernR=%u, ligs=%u", i,
            h.intervalCount, h.glyphCount, h.advanceY, h.ascender, h.descender, h.kernLeftEntryCount,
            h.kernRightEntryCount, h.ligaturePairCount);
  }
  return true;
}

// --- Prewarm ---

namespace {
const char* singleTextGetter(const void* ctx, uint32_t) { return static_cast<const char*>(ctx); }
}  // namespace

int SdCardFont::prewarm(const char* utf8Text, uint8_t styleMask, bool metadataOnly, bool loadKernLig, bool accumulate) {
  ++fontTableGeneration;
  return prewarm(&singleTextGetter, utf8Text, 1, styleMask, metadataOnly, loadKernLig, accumulate);
}

int SdCardFont::prewarm(TextGetter getter, const void* ctx, uint32_t textCount, uint8_t styleMask, bool metadataOnly,
                        bool loadKernLig, bool accumulate) {
  ++fontTableGeneration;
  PoolIo io{this};
  auto services = cacheServices(&io);
  auto pool = cacheView(&services);
  return xp_sdfont_pool_prewarm(&pool, getter, ctx, textCount, styleMask, metadataOnly, loadKernLig, accumulate);
}

// --- Cache management ---

void SdCardFont::clearCache() {
  ++fontTableGeneration;
  auto services = cacheServices(nullptr);
  auto pool = cacheView(&services);
  xp_sdfont_pool_clear_cache(&pool);
}

// --- Advance table ---

void SdCardFont::clearPersistentCache() {
  ++fontTableGeneration;
  auto services = cacheServices(nullptr);
  auto pool = cacheView(&services);
  xp_sdfont_pool_clear_advances(&pool);
}

bool SdCardFont::hasAdvanceTable() const {
  for (uint8_t i = 0; i < MAX_STYLES; ++i)
    if (advanceTableSize_[i]) return true;
  return false;
}

bool SdCardFont::advanceTableLookup(uint8_t style, uint32_t cp, uint16_t* outAdvance) const {
  const auto pool = cacheView();
  return xp_sdfont_pool_lookup(&pool, cp, style, outAdvance);
}

uint16_t SdCardFont::getAdvance(uint32_t codepoint, uint8_t style) const {
  const auto pool = cacheView();
  return xp_sdfont_pool_advance(&pool, codepoint, style);
}

int SdCardFont::buildAdvanceTablePacked(const char* const* segments, const size_t* segmentLens, size_t segmentCount,
                                        bool includeSpace, bool includeHyphen, uint8_t styleMask, const char* extraText,
                                        bool includeSmallCaps) {
  ++fontTableGeneration;
  PoolIo io{this};
  auto services = cacheServices(&io);
  auto pool = cacheView(&services);
  const XpSdfontTexts texts{segments,     segmentLens,   segmentCount,    extraText,
                            includeSpace, includeHyphen, includeSmallCaps};
  return xp_sdfont_pool_advances(&pool, &texts, styleMask);
}

int SdCardFont::buildAdvanceTable(const char* utf8Text, uint8_t styleMask, const char* extraText) {
  ++fontTableGeneration;
  const size_t len = strlen(utf8Text);
  return buildAdvanceTablePacked(&utf8Text, &len, 1, false, false, styleMask, extraText);
}

// --- Stats ---

void SdCardFont::logStats(const char* label) {
  LOG_DBG("SDCF", "[%s] total=%ums sd_read=%ums seeks=%u glyphs=%u bitmap=%u bytes", label, stats_.prewarmTotalMs,
          stats_.sdReadTimeMs, stats_.seekCount, stats_.uniqueGlyphs, stats_.bitmapBytes);
}

void SdCardFont::resetStats() { stats_ = Stats{}; }

// --- Public accessors ---

EpdFont* SdCardFont::getEpdFont(uint8_t style) {
  style &= (MAX_STYLES - 1);
  if (!hasStyle(style)) return nullptr;
  return &styleData(style).epdFont;
}

bool SdCardFont::hasStyle(uint8_t style) const { return styleSlots_[style & (MAX_STYLES - 1)] != 0; }

uint8_t SdCardFont::resolveStyle(uint8_t style) const {
  const auto pool = cacheView();
  return xp_sdfont_pool_resolve(&pool, style);
}

uint8_t SdCardFont::resolveStyleMask(uint8_t styleMask) const {
  const auto pool = cacheView();
  return xp_sdfont_pool_mask(&pool, styleMask);
}

// --- On-demand glyph loading (overflow buffer) ---

const EpdGlyph* SdCardFont::onGlyphMiss(void* ctx, uint32_t codepoint) {
  ++fontTableGeneration;
  const auto* oc = static_cast<OverflowContext*>(ctx);
  PoolIo io{oc->self};
  auto services = cacheServices(&io);
  auto pool = oc->self->cacheView(&services);
  return static_cast<const EpdGlyph*>(xp_sdfont_pool_miss(&pool, codepoint, oc->styleIdx));
}

bool SdCardFont::isOverflowGlyph(const EpdGlyph* glyph) const {
  for (uint32_t i = 0; i < overflowCount_; i++) {
    if (&overflow_[i].glyph == glyph) return true;
  }
  return false;
}

const uint8_t* SdCardFont::getOverflowBitmap(const EpdGlyph* glyph) const {
  for (uint32_t i = 0; i < overflowCount_; i++) {
    if (&overflow_[i].glyph == glyph) {
      return overflow_[i].bitmap;
    }
  }
  return nullptr;
}

SdCardFont* SdCardFont::fromMissCtx(void* ctx) { return static_cast<OverflowContext*>(ctx)->self; }

void SdCardFont::metricViews(XpNativeFamily& family) const {
  family.sd = true;
  family.advanceTable = hasAdvanceTable();
  for (uint8_t style = 0; style < MAX_STYLES; ++style) {
    const auto resolved = resolveStyle(style);
    family.resolved[style] = resolved;
    if (!hasStyle(resolved)) continue;
    const auto& s = styleData(resolved);
    family.styles[style] = {s.epdFont.data,
                            advanceTable_[resolved],
                            advanceTableSize_[resolved],
                            s.fullIntervals,
                            s.intervalsAreBmp16 ? s.bmpIntervals : nullptr,
                            s.header.intervalCount};
  }
}

void SdCardFont::bitmapView(const EpdFontData* font, XpGlyphSource& source) const {
  source.overflowFirst = &overflow_[0].glyph;
  source.overflowStride = sizeof(OverflowEntry);
  source.overflowCount = overflowCount_;
  for (uint8_t style = 0; style < MAX_STYLES; ++style) {
    if (!hasStyle(style)) continue;
    const auto& s = styleData(style);
    if (font != &s.miniData) continue;
    source.glyphCount = s.miniGlyphCount;
    source.bitmapLen = s.miniBitmapCapacity;
    return;
  }
}
