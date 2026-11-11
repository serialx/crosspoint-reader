#include <HalStorage.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "TtfEpdFont.h"
#include "physical.h"

#define CHECK(x)                                                                \
  do {                                                                          \
    if (!(x)) {                                                                 \
      std::fprintf(stderr, "check failed %s:%d: %s\n", __FILE__, __LINE__, #x); \
      std::abort();                                                             \
    }                                                                           \
  } while (0)
extern "C" {
void* vector_owner_new(const XpFtSource*);
void vector_owner_drop(void*);
bool vector_owner_load(void*, uint16_t, bool, size_t, uint16_t);
void vector_owner_clear(void*, bool);
int32_t vector_owner_glyph(void*, uint8_t, uint32_t, EpdGlyph*, uint8_t*, size_t);
int8_t vector_owner_kern(void*, uint8_t, uint32_t, uint32_t);
void vector_owner_metrics(void*, uint8_t, int32_t*, int32_t*, uint8_t*);
bool vector_owner_coverage(void*, uint32_t);
bool vector_owner_prewarm(void*, const uint8_t*, size_t, bool);
uint32_t vector_owner_ligature(void*, uint8_t, uint32_t);
size_t vector_owner_size();
}
static std::map<void*, size_t> blocks;
static size_t live, peak, requests;
static size_t freeDefault = 200 * 1024;
static bool recover, failAllocation;
static TtfEpdFont* activeOracle;
static std::vector<uint8_t> sinks;
extern "C" void* fiFontMalloc(size_t n) {
  if (failAllocation) return nullptr;
  void* p = std::malloc(n ? n : 1);
  CHECK(p);
  blocks[p] = n;
  live += n;
  peak = std::max(peak, live);
  ++requests;
  return p;
}
extern "C" void fiFontFree(void* p) {
  if (!p) return;
  CHECK(blocks.count(p));
  live -= blocks[p];
  blocks.erase(p);
  std::free(p);
}
extern "C" void* fiFontRealloc(void* p, size_t n) {
  if (!p) return fiFontMalloc(n);
  CHECK(blocks.count(p));
  const size_t before = blocks[p];
  void* next = fiFontMalloc(n);
  if (next) std::memcpy(next, p, std::min(before, n));
  fiFontFree(p);
  return next;
}
extern "C" size_t vector_owner_heap(uint8_t) { return vectorLargest; }
extern "C" size_t vector_owner_pressure(uint8_t sink) {
  if (sink == 0) return freeDefault;
  sinks.push_back(sink);
  if (sink == 3) {
    // The inherited native guard must suppress eviction of the active family.
    if (activeOracle) activeOracle->releaseResidentCaches();
    if (recover) {
      vectorLargest = freeDefault = 200 * 1024;
      return freeDefault;
    }
  }
  return 0;
}
// Frozen prefixRead algorithm, independent of the candidate physical leaf.
static unsigned long oracleRead(void* context, unsigned long offset, unsigned char* out, unsigned long count) {
  auto& s = *static_cast<XpFtSource*>(context);
  if (count == 0) return 0;
  unsigned long copied = 0;
  if (offset < s.length) {
    copied = std::min(count, static_cast<unsigned long>(s.length) - offset);
    std::memcpy(out, s.data + offset, copied);
    if (copied == count) return count;
  }
  auto& file = *static_cast<HalFile*>(s.file);
  if (!file || !file.seek(offset + copied)) return copied;
  int got = file.read(out + copied, count - copied);
  return copied + (got < 0 ? 0 : static_cast<unsigned long>(got));
}
struct Trace {
  std::vector<int64_t> values;
  std::vector<uint8_t> bitmap, pressure;
  size_t peakBytes = 0, allocationRequests = 0, retained = 0, reads = 0, seeks = 0;
};
struct Subject {
  bool rust;
  std::unique_ptr<TtfEpdFont> native;
  void* owner = nullptr;
  Subject(bool rust, XpFtSource* sources) : rust(rust) {
    if (rust)
      owner = vector_owner_new(sources);
    else {
      native = std::make_unique<TtfEpdFont>();
      activeOracle = native.get();
      for (uint8_t i = 0; i < 4; ++i) {
        auto& s = sources[i];
        if (s.mode == 1) native->addResidentSource(i, s.data, s.length);
        if (s.mode == 2) native->addStreamSource(i, oracleRead, &s, s.size);
      }
    }
  }
  ~Subject() {
    if (owner) vector_owner_drop(owner);
    activeOracle = nullptr;
  }
  bool load(uint16_t size, bool two, size_t cap, uint16_t max) {
    return rust ? vector_owner_load(owner, size, two, cap, max) : native->load(size, two, cap, max);
  }
  void clear(bool resident) {
    if (rust)
      vector_owner_clear(owner, resident);
    else if (resident)
      native->releaseResidentCaches();
    else
      native->clearCache();
  }
  bool prewarm(const char* text, bool reset) {
    return rust    ? vector_owner_prewarm(owner, reinterpret_cast<const uint8_t*>(text), text ? strlen(text) : 0, reset)
           : reset ? native->build(text)
                   : native->addCoverage(text);
  }
  const EpdFontData* data(uint8_t style = 0) const {
    return static_cast<const EpdFontData*>(*native->family().registration(1).slots[style]);
  }
  void glyph(Trace& trace, uint8_t style, uint32_t cp) {
    EpdGlyph result{};
    std::vector<uint8_t> bytes(65536);
    int32_t count = -1;
    if (rust)
      count = vector_owner_glyph(owner, style, cp, &result, bytes.data(), bytes.size());
    else {
      const auto* data = this->data(style);
      if (const auto* g = data->glyphMissHandler(data->glyphMissCtx, cp)) {
        result = *g;
        count = g->dataLength;
        if (count) std::memcpy(bytes.data(), data->vectorBitmapHandler(data->glyphMissCtx, g), count);
      }
    }
    trace.values.insert(trace.values.end(), {style, cp, count});
    if (count < 0) return;
    trace.values.insert(trace.values.end(), {result.width, result.height, result.advanceX, result.left, result.top,
                                             result.dataLength, result.dataOffset});
    trace.bitmap.insert(trace.bitmap.end(), bytes.begin(), bytes.begin() + count);
  }
  void metrics(Trace& trace, uint8_t style) {
    int32_t a, d;
    uint8_t h;
    if (rust)
      vector_owner_metrics(owner, style, &a, &d, &h);
    else {
      const auto* data = this->data(style);
      a = data->ascender;
      d = data->descender;
      h = data->advanceY;
    }
    trace.values.insert(trace.values.end(), {a, d, h});
  }
  void coverage(Trace& trace, uint32_t cp) {
    trace.values.push_back(rust ? vector_owner_coverage(owner, cp) : data()->coverageHandler(data()->glyphMissCtx, cp));
  }
  void kern(Trace& trace, uint8_t style, uint32_t left, uint32_t right) {
    trace.values.push_back(rust ? vector_owner_kern(owner, style, left, right)
                                : data(style)->kernHandler(data(style)->glyphMissCtx, left, right));
  }
  void ligature(Trace& trace, uint8_t style, uint32_t pair) {
    uint32_t result = 0;
    if (rust)
      result = vector_owner_ligature(owner, style, pair);
    else {
      const auto* data = this->data(style);
      for (size_t i = 0; i < data->ligaturePairCount; ++i)
        if (data->ligaturePairs[i].pair == pair) result = data->ligaturePairs[i].ligatureCp;
    }
    trace.values.push_back(result);
  }
};
struct Case {
  uint16_t size = 14;
  uint8_t mask = 1;
  bool two = true;
  size_t cap = 4096;
  uint16_t maximum = 3;
  int stream = -1, damage = 0, pressure = 0;
};
static Trace run(bool rust, const std::array<std::vector<uint8_t>, 4>& original, Case c, bool abortCase = false) {
  auto bytes = original;
  for (auto& source : bytes) {
    if (c.damage == 1) std::fill(source.begin(), source.end(), 0);
    if (c.damage == 2) source.resize(32);
    if (c.damage == 3) source.clear();
  }
  HalFile files[4]{{bytes[0]}, {bytes[1]}, {bytes[2]}, {bytes[3]}};
  XpFtSource sources[4]{};
  for (uint8_t i = 0; i < 4; ++i) {
    files[i].failSeek = c.damage == 4;
    files[i].shortRead = c.damage == 5;
    if (c.mask & (1 << i)) {
      sources[i] = {static_cast<uint8_t>(c.stream < 0 ? 1 : 2), bytes[i].data(),
                    static_cast<uint32_t>(c.stream < 0 ? bytes[i].size() : std::min(bytes[i].size(), size_t(c.stream))),
                    &files[i], static_cast<uint32_t>(bytes[i].size())};
      if (bytes[i].empty()) sources[i].mode = 0;
    }
  }
  vectorLargest = c.pressure == 1 || c.pressure == 3 ? 1 : c.pressure == 2 ? 2048 : 200 * 1024;
  freeDefault = c.pressure ? 0 : 200 * 1024;
  recover = c.pressure == 3;
  sinks.clear();
  peak = live;
  requests = 0;
  Trace result;
  {
    Subject subject(rust, sources);
    bool loaded = subject.load(c.size, c.two, c.cap, c.maximum);
    result.values.push_back(loaded);
    if (!loaded && (c.damage == 4 || c.damage == 5)) {
      for (auto& file : files) {
        file.failSeek = false;
        file.shortRead = false;
      }
      loaded = subject.load(c.size, c.two, c.cap, c.maximum);
      result.values.push_back(loaded);
    }
    if (loaded) {
      if (abortCase) {  // Warm the SDK raster buffer; fail the next cache allocation after preflight.
        if (rust) {
          EpdGlyph g{};
          uint8_t dst[65536];
          vector_owner_glyph(subject.owner, 0, 65, &g, dst, sizeof(dst));
        } else {
          const auto* d = subject.data();
          d->glyphMissHandler(d->glyphMissCtx, 65);
        }
        subject.clear(true);
        failAllocation = true;
        subject.glyph(result, 0, 65);
        CHECK(false);
      }
      for (uint8_t style = 0; style < 4; ++style) {
        subject.metrics(result, style);
        for (uint32_t cp : {32u, 65u, 86u, 0xe9u, 0x301u, 0x3b1u, 0xfb00u, 0xfb01u, 0xfb02u, 0xfb03u, 0xfb04u, 0xfffdu,
                            0x4e00u, 0x10ffffu, 65u})
          subject.glyph(result, style, cp);
        subject.metrics(result, style);
        for (uint32_t pair : {0x00660066u, 0x00660069u, 0x0066006cu, 0xfb000069u, 0xfb00006cu})
          subject.ligature(result, style, pair);
        subject.kern(result, style, 65, 86);
        subject.kern(result, style, 65, 86);
        subject.kern(result, style, 0, 86);
      }
      for (uint32_t cp : {65u, 0xfb01u, 0x4e00u, 0x10ffffu}) subject.coverage(result, cp);
      if (c.cap == 32768) {
        for (uint32_t cp = 33; cp < 140; ++cp) subject.glyph(result, 0, cp);
        for (uint32_t cp = 1; cp < 516; ++cp) subject.kern(result, 0, cp, 86);
      }
      subject.clear(false);
      subject.glyph(result, 0, 65);
      subject.clear(true);
      subject.coverage(result, 65);
      subject.glyph(result, 1, 65);
      result.values.push_back(subject.load(c.size == 72 ? 6 : c.size + 1, !c.two, c.cap, c.maximum));
      subject.glyph(result, 0, 65);
      subject.glyph(result, 3, 86);
      result.values.push_back(subject.prewarm("Affi\xc3\xa9", true));
      result.values.push_back(subject.prewarm("AV", false));
      result.values.push_back(subject.prewarm(nullptr, true));
      subject.glyph(result, 0, 65);
      vectorLargest = 200 * 1024;
      subject.clear(false);
      subject.glyph(result, 0, 65);
    }
  }
  result.pressure = sinks;
  result.peakBytes = peak;
  result.allocationRequests = requests;
  result.retained = live;
  for (const auto& file : files) {
    result.reads += file.reads;
    result.seeks += file.seeks;
  }
  return result;
}
int main(int argc, char** argv) {
  CHECK(argc >= 2);
  std::array<std::vector<uint8_t>, 4> bytes;
  const auto dir = std::filesystem::path(argv[1]).parent_path();
  size_t style = 0;
  for (const char* name :
       {"NotoSans-Regular.ttf", "NotoSans-Bold.ttf", "NotoSans-Italic.ttf", "NotoSans-BoldItalic.ttf"}) {
    std::ifstream input(dir / name, std::ios::binary);
    bytes[style] = std::vector<uint8_t>((std::istreambuf_iterator<char>(input)), {});
    CHECK(!bytes[style].empty());
    ++style;
  }
  if (argc == 3) {
    run(std::string(argv[2]) == "rust-abort", bytes, Case{}, true);
    return 3;
  }
  // Warm the process-wide SDK library before comparing retained/peak bytes.
  run(false, bytes, Case{});
  const size_t retained = live;
  std::vector<Case> cases;
  for (uint16_t size = 6; size <= 72; ++size)
    for (bool two : {false, true}) cases.push_back(Case{size, 1, two});
  for (uint8_t mask = 1; mask < 16; mask += 2) cases.push_back(Case{14, mask});
  for (int prefix : {0, 1, 11, 12, 31, 4095, 4096, 32768, 1000000}) {
    Case c;
    c.stream = prefix;
    cases.push_back(c);
  }
  for (int damage = 1; damage <= 5; ++damage)
    for (int stream : {-1, 0, 12}) {
      if (damage > 3 && stream < 0) continue;
      Case c;
      c.damage = damage;
      c.stream = stream;
      cases.push_back(c);
    }
  cases.push_back(Case{14, 0});
  for (size_t cap : {size_t(0), size_t(1), size_t(64), size_t(32768)}) {
    Case c;
    c.cap = cap;
    c.maximum = 768;
    cases.push_back(c);
  }
  for (int pressure = 1; pressure <= 3; ++pressure) {
    Case c;
    c.pressure = pressure;
    cases.push_back(c);
  }
  size_t index = 0, totalGlyphBytes = 0;
  for (auto c : cases) {
    Trace a = run(false, bytes, c), b = run(true, bytes, c);
    if (a.values != b.values || a.bitmap != b.bitmap || a.pressure != b.pressure || a.reads != b.reads ||
        a.seeks != b.seeks) {
      std::fprintf(stderr,
                   "differential case %zu size=%u mask=%u two=%d cap=%zu stream=%d damage=%d pressure=%d values=%d "
                   "bitmap=%d pressure=%d reads=%zu/%zu seeks=%zu/%zu\n",
                   index, c.size, c.mask, c.two, c.cap, c.stream, c.damage, c.pressure, a.values == b.values,
                   a.bitmap == b.bitmap, a.pressure == b.pressure, a.reads, b.reads, a.seeks, b.seeks);
      for (size_t i = 0; i < std::min(a.values.size(), b.values.size()); ++i)
        if (a.values[i] != b.values[i]) {
          std::fprintf(stderr, "value[%zu]=%lld/%lld\n", i, (long long)a.values[i], (long long)b.values[i]);
          break;
        }
      return 1;
    }
    CHECK(a.retained == retained && b.retained == retained);
    std::printf(
        "case=%zu native_alloc=%zu rust_alloc=%zu native_peak=%zu rust_peak=%zu retained=%zu reads=%zu bitmap=%zu\n",
        index, a.allocationRequests, b.allocationRequests, a.peakBytes, b.peakBytes, retained, a.reads,
        a.bitmap.size());
    totalGlyphBytes += a.bitmap.size();
    ++index;
  }
  std::printf("PASS cases=%zu exact_bitmap_bytes=%zu native_owner=%zu rust_owner=%zu retained_sdk=%zu\n", index,
              totalGlyphBytes, sizeof(TtfEpdFont), vector_owner_size(), retained);
}
// Unused xp-core asset code shares archive objects; this fixture must not enter it.
extern "C" void* xp_assets_platform_alloc(size_t, uint32_t) { std::abort(); }
extern "C" void* xp_assets_platform_realloc(void*, size_t, uint32_t) { std::abort(); }
extern "C" void xp_assets_platform_free(void*) { std::abort(); }
