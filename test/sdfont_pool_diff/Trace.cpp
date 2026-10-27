#include <HalStorage.h>
#include <SdCardFont.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

namespace {
constexpr uint64_t SEED = 14695981039346656037ULL;
uint64_t state = SEED, allocationTrace = SEED;
size_t live = 0, peak = 0, largest = 0, attempts = 0, failAt = 0;
bool tracking = false;
struct alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) Allocation {
  size_t size, id;
  bool tracked;
};
void hash(uint64_t& h, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    h = (h ^ (v & 255)) * 1099511628211ULL;
    v >>= 8;
  }
}
void value(uint64_t v) { hash(state, v); }
void bytes(const void* p, size_t count) {
  const auto* b = static_cast<const uint8_t*>(p);
  value(count);
  for (size_t i = 0; i < count; ++i) value(b[i]);
}
void glyph(const EpdGlyph* g, bool resident = false) {
  value(g != nullptr);
  if (!g) return;
  value(g->width);
  value(g->height);
  value(g->advanceX);
  value(g->left);
  value(g->top);
  value(g->dataLength);
  const bool canonical = resident && !g->dataLength && !std::getenv("SDPOOL_EXACT_ZERO_OFFSETS");
  value(canonical ? 0 : g->dataOffset);
}
void snapshot(SdCardFont& font, const std::vector<uint32_t>& cps) {
  value(font.styleCount());
  value(font.contentHash());
  value(font.hasAdvanceTable());
  const auto& stats = font.getStats();
  value(stats.prewarmTotalMs);
  value(stats.sdReadTimeMs);
  value(stats.seekCount);
  value(stats.uniqueGlyphs);
  value(stats.bitmapBytes);
  value(SdCardFont::takeCorruptGlyphNotice());
  for (unsigned mask = 0; mask < 16; ++mask) value(font.resolveStyleMask(mask));
  for (unsigned style = 0; style < 4; ++style) {
    value(font.hasStyle(style));
    value(font.resolveStyle(style));
    auto* f = font.getEpdFont(style);
    if (!f) continue;
    const auto* d = f->data;
    value(d->advanceY);
    value(d->ascender);
    value(d->descender);
    value(d->is2Bit);
    value(d->intervalCount);
    XpGlyphSource source{};
    font.bitmapView(d, source);
    for (uint32_t i = 0; i < d->intervalCount; ++i) {
      const auto& iv = d->intervals[i];
      value(iv.first);
      value(iv.last);
      value(iv.offset);
      for (uint32_t j = 0; j <= iv.last - iv.first; ++j) {
        const auto* g = d->glyph + iv.offset + j;
        glyph(g, true);
        if (d->bitmap && uint64_t(g->dataOffset) + g->dataLength <= source.bitmapLen)
          bytes(d->bitmap + g->dataOffset, g->dataLength);
      }
    }
    value(d->kernLeftEntryCount);
    value(d->kernRightEntryCount);
    value(d->kernLeftClassCount);
    value(d->kernRightClassCount);
    if (d->kernLeftClasses) bytes(d->kernLeftClasses, d->kernLeftEntryCount * 3);
    if (d->kernRightClasses) bytes(d->kernRightClasses, d->kernRightEntryCount * 3);
    if (d->kernMatrix) bytes(d->kernMatrix, d->kernLeftClassCount * d->kernRightClassCount);
    value(d->ligaturePairCount);
    if (d->ligaturePairs) bytes(d->ligaturePairs, d->ligaturePairCount * 8);
    for (uint32_t cp : cps) {
      value(f->hasCodepoint(cp));
      value(font.getAdvance(cp, style));
    }
    for (uint32_t a : {65u, 66u, 69u, 70u, 102u, 105u, 0xac00u})
      for (uint32_t b : {65u, 66u, 69u, 70u, 102u, 105u}) {
        value(f->getKerning(a, b));
        value(f->getLigature(a, b));
      }
  }
}
void record(const char* op, int result, SdCardFont& font, const std::vector<uint32_t>& cps) {
  value(result);
  snapshot(font, cps);
  std::printf("%s %d state=%016llx io=%016llx alloc=%016llx live=%zu peak=%zu largest=%zu requests=%zu\n", op, result,
              (unsigned long long)state, (unsigned long long)sdTrace, (unsigned long long)allocationTrace, live, peak,
              largest, attempts);
}
std::string text(const std::vector<uint32_t>& cps, size_t start, size_t count) {
  std::string result;
  for (size_t i = start; i < std::min(cps.size(), start + count); ++i) {
    char encoded[5]{};
    utf8AppendCodepoint(encoded, cps[i]);
    result += encoded;
  }
  return result;
}
const char* getter(const void* raw, uint32_t i) {
  const auto& texts = *static_cast<const std::vector<std::string>*>(raw);
  return i == 1 ? nullptr : texts[i].c_str();
}
size_t run(const std::vector<uint32_t>& cps, const std::vector<std::string>& texts, size_t failure, bool pressure) {
  state = allocationTrace = sdTrace = SEED;
  live = peak = largest = attempts = 0;
  failAt = failure;
  ESP.freeHeap = pressure ? 30 * 1024 : 200 * 1024;
  ESP.largestBlock = pressure ? 12 * 1024 : 200 * 1024;
  sdFontTestReads = 0;
  SdCardFont::takeCorruptGlyphNotice();
  tracking = true;
  {
    SdCardFont font;
    const bool loaded = font.load("trace.cpfont");
    record("load", loaded, font, cps);
    if (loaded) {
      record("metadata", font.prewarm(texts[0].c_str(), 15, true, true, false), font, cps);
      record("full", font.prewarm(texts[0].c_str(), 15, false, false, false), font, cps);
      record("kern-topup", font.prewarm(texts[0].c_str(), 15, false, true, false), font, cps);
      font.clearCache();
      record("clear", 0, font, cps);
      record("subset", font.prewarm(texts[0].c_str(), 15, false, true, false), font, cps);
      record("union", font.prewarm(texts[2].c_str(), 5, false, true, true), font, cps);
      record("getter", font.prewarm(getter, &texts, texts.size(), 10, false, true, false), font, cps);
      record("advance", font.buildAdvanceTable(texts[3].c_str(), 15, " fi 한字"), font, cps);
      const char packed[] = "aBc\0def\0한글\0";
      const char* segments[] = {packed, texts[0].c_str()};
      const size_t sizes[] = {sizeof(packed) - 1, texts[0].size()};
      record("packed", font.buildAdvanceTablePacked(segments, sizes, 2, true, true, 15, "각字", true), font, cps);
      font.clearCache();
      record("clear-advances", 0, font, cps);
      font.clearPersistentCache();
      record("persistent", 0, font, cps);
      for (unsigned pass = 0; pass < 3; ++pass) {
        for (unsigned style = 0; style < 4; ++style) {
          auto* f = font.getEpdFont(style);
          if (!f) continue;
          for (size_t i = 0; i < std::min<size_t>(18, cps.size()); ++i) {
            const auto* g = f->data->glyphMissHandler(f->data->glyphMissCtx, cps[i]);
            glyph(g);
            value(font.isOverflowGlyph(g));
            if (g && font.getOverflowBitmap(g)) bytes(font.getOverflowBitmap(g), g->dataLength);
          }
        }
        record("overflow", 0, font, cps);
      }
      font.releaseResidentCaches();
      record("release", 0, font, cps);
      for (unsigned pass = 0; pass < 5; ++pass) {
        record("rebuild", font.prewarm(texts[pass % texts.size()].c_str(), 15, false, true, false), font, cps);
        font.clearCache();
      }
      font.resetStats();
      record("stats-reset", 0, font, cps);
      // A second instance tests sharing and release order while the first remains loaded.
      {
        SdCardFont shared;
        record("shared-load", shared.load("trace.cpfont"), shared, cps);
      }
      record("shared-release", 0, font, cps);
    }
  }
  std::printf("destroy alloc=%016llx live=%zu peak=%zu largest=%zu requests=%zu\n", (unsigned long long)allocationTrace,
              live, peak, largest, attempts);
  tracking = false;
  if (live) std::exit(3);
  return attempts;
}
size_t missCalls = 0;
const EpdGlyph* (*originalMiss)(void*, uint32_t) = nullptr;
const EpdGlyph* countMiss(void* context, uint32_t cp) {
  ++missCalls;
  return originalMiss(context, cp);
}
void usage(const std::vector<std::string>& texts) {
  SdCardFont font;
  if (!font.load("trace.cpfont")) std::exit(2);
  for (size_t page = 0; page < texts.size(); ++page) {
    font.clearCache();
    font.prewarm(texts[page].c_str(), 15, false, true, false);
    for (uint8_t style = 0; style < 4; ++style) {
      auto* face = font.getEpdFont(style);
      if (!face) continue;
      auto* data = const_cast<EpdFontData*>(face->data);
      originalMiss = data->glyphMissHandler;
      data->glyphMissHandler = countMiss;
      missCalls = 0;
      const auto* p = reinterpret_cast<const unsigned char*>(texts[page].c_str());
      size_t glyphs = 0;
      while (*p) {
        const auto cp = utf8NextCodepoint(&p);
        if (!cp) break;
        face->getGlyph(cp);
        ++glyphs;
      }
      data->glyphMissHandler = originalMiss;
      std::printf("page=%zu style=%u glyphs=%zu miss_entries=%zu\n", page, style, glyphs, missCalls);
    }
  }
}
uint32_t read32(size_t at) {
  if (at + 4 > sdFontTestFile.size()) return 0;
  uint32_t v = 0;
  for (unsigned i = 0; i < 4; ++i) v |= uint32_t(sdFontTestFile[at + i]) << (8 * i);
  return v;
}
}  // namespace
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  const size_t id = tracking ? ++attempts : 0;
  if (tracking) {
    hash(allocationTrace, 1);
    hash(allocationTrace, id);
    hash(allocationTrace, size);
  }
  if (tracking && id == failAt) {
    hash(allocationTrace, 0);
    return nullptr;
  }
  auto* a = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
  if (!a) return nullptr;
  *a = {size, id, tracking};
  if (tracking) {
    hash(allocationTrace, 1);
    live += size;
    peak = std::max(peak, live);
    largest = std::max(largest, size);
  }
  return a + 1;
}
void* operator new[](size_t size) {
  auto* p = ::operator new[](size, std::nothrow);
  if (!p) std::abort();
  return p;
}
void operator delete[](void* p) noexcept {
  if (!p) return;
  auto* a = static_cast<Allocation*>(p) - 1;
  if (a->tracked) {
    hash(allocationTrace, 2);
    hash(allocationTrace, a->id);
    hash(allocationTrace, a->size);
    live -= a->size;
  }
  std::free(a);
}
void operator delete[](void* p, size_t) noexcept { ::operator delete[](p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete[](p); }
int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) return 2;
  sdFontTestFile.assign(std::istreambuf_iterator<char>(input), {});
  std::vector<uint32_t> cps;
  const uint32_t count = read32(36), offset = read32(56);
  if (count <= 4096 && uint64_t(offset) + uint64_t(count) * 12 <= sdFontTestFile.size()) {
    for (uint32_t i = 0; i < count && cps.size() < 900; ++i) {
      const uint32_t first = read32(offset + i * 12), last = read32(offset + i * 12 + 4);
      for (uint64_t cp = first; cp <= last && cps.size() < 900; ++cp)
        if (cp && cp <= 0x10ffff)
          cps.push_back(cp);
        else
          break;
    }
  }
  for (uint32_t cp : {32u, 45u, 65u, 0xac00u, 0x4e00u, 0xfffdu, 0x10ffffu}) cps.push_back(cp);
  std::vector<std::string> texts{text(cps, 0, 60), "", text(cps, 50, 70), text(cps, 0, 850), text(cps, 120, 10)};
  if (argc > 2) {
    std::ifstream extra(argv[2]);
    std::string line;
    while (std::getline(extra, line))
      if (!line.empty()) texts.push_back(line);
  }
  if (std::getenv("SDPOOL_USAGE")) {
    usage(texts);
    return 0;
  }
  const size_t allocations = run(cps, texts, 0, false);
  if (std::getenv("SDPOOL_BASE_ONLY")) return 0;
  for (size_t i = 1; i <= allocations; ++i) {
    std::printf("OOM %zu\n", i);
    run(cps, texts, i, false);
  }
  run(cps, texts, 0, true);
  // Mutation after load is modelled by reloading the still-valid header with truncated/changed glyph data.
  const auto original = sdFontTestFile;
  const size_t glyphOffset = size_t(offset) + size_t(count) * 12;
  if (glyphOffset + 16 <= original.size()) {
    for (size_t at = glyphOffset; at < glyphOffset + 16; ++at) {
      for (unsigned bit = 0; bit < 8; ++bit) {
        sdFontTestFile[at] ^= 1u << bit;
        std::printf("mutate %zu %u\n", at, bit);
        run(cps, texts, 0, false);
        sdFontTestFile[at] ^= 1u << bit;
      }
    }
  }
  const size_t truncEnd = original.size() <= 1024 ? original.size() : std::min(glyphOffset + 32, original.size());
  for (size_t n = 0; n < truncEnd; ++n) {
    sdFontTestFile.assign(original.begin(), original.begin() + n);
    std::printf("truncate %zu\n", n);
    run(cps, texts, 0, false);
  }
}
