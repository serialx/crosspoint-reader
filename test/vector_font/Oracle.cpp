#include <TtfEpdFont.h>
#include <MemoryManager.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

static uint64_t trace = 14695981039346656037ULL;
static size_t peak, bytes, allocations;
static std::map<void*, size_t> blocks;
static void event(size_t value) {
  for (int i = 0; i < 8; ++i) { trace = (trace ^ (value & 255)) * 1099511628211ULL; value >>= 8; }
}
extern "C" void* fiFontMalloc(size_t size) {
  void* p = std::malloc(size ? size : 1);
  blocks[p] = size; bytes += size; peak = std::max(peak, bytes); ++allocations;
  event(1); event(size); return p;
}
extern "C" void fiFontFree(void* p) {
  if (p) {
    if (!blocks.count(p)) { std::fprintf(stderr, "unknown free %p\n", p); std::abort(); }
    event(2); event(blocks[p]); bytes -= blocks[p]; blocks.erase(p);
  }
  std::free(p);
}
extern "C" void* fiFontRealloc(void* p, size_t n) {
  // FreeInkFont does not use realloc for its PSRAM vectors in this workload.
  if (!p) return fiFontMalloc(n);
  event(3); event(n);
  bytes -= blocks[p]; blocks.erase(p);
  void* out = std::realloc(p, n); blocks[out] = n; bytes += n; peak = std::max(peak, bytes); return out;
}
static TtfEpdFont* owner;
static void snapshot(const char* label) {
  std::printf("%s alloc=%zu live=%zu peak=%zu trace=%llu\n", label, allocations, bytes, peak,
              static_cast<unsigned long long>(trace));
}
static void draw(TtfEpdFont& font, uint8_t style) {
  auto family = font.family();
  const auto st = static_cast<EpdFontFamily::Style>(style);
  for (uint32_t cp : {65u, 66u, 0xfb01u, 0xfffdu, 65u}) {
    const auto* glyph = family.getGlyph(cp, st);
    std::printf("glyph %u %u", style, cp);
    if (!glyph) { std::puts(" missing"); continue; }
    const auto* data = family.getData(st);
    const auto* bitmap = data->vectorBitmapHandler(data->glyphMissCtx, glyph);
    uint32_t hash = 2166136261u;
    if (bitmap) for (uint16_t i = 0; i < glyph->dataLength; ++i) hash = (hash ^ bitmap[i]) * 16777619u;
    std::printf(" %u %u %u %d %d %u %u %d %d %u\n", glyph->width, glyph->height, glyph->advanceX,
                glyph->left, glyph->top, glyph->dataLength, hash, data->ascender, data->descender, data->advanceY);
  }
  for (int i = 0; i < 2; ++i) std::printf("kern %d\n", family.getKerning(65, 86, st));
}
int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc != 2) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> source((std::istreambuf_iterator<char>(input)), {});
  if (source.empty()) return 2;
  size_t retained = 0;
  for (int mode = 0; mode < 4; ++mode) {
    std::printf("CASE %d\n", mode);
    trace = 14695981039346656037ULL; allocations = 0; peak = bytes;
    vectorLargest = mode == 1 || mode == 3 ? 1 : mode == 2 ? 2048 : 200 * 1024;
    vectorRecover = mode == 3;
    {
      TtfEpdFont font; owner = &font;
      vectorEvict = [] { owner->releaseResidentCaches(); };
      std::printf("empty %d\n", font.load(14));
      font.addResidentSource(0, source.data(), source.size());
      if (mode == 0) font.addResidentSource(2, source.data(), source.size());
      std::printf("load %d\n", font.load(14, true, 4096, 3));
      for (uint8_t style = 0; style < 4; ++style) draw(font, style);
      snapshot("first");
      font.clearCache(); draw(font, 0); snapshot("clear");
      font.releaseResidentCaches(); draw(font, 1); snapshot("release");
      std::printf("reload %d\n", font.load(18, false, 4096, 3));
      draw(font, 0); snapshot("reload");
      const bool built = font.build("ABBA");
      const bool added = font.addCoverage("fi");
      std::printf("prewarm %d %d\n", built, added);
      snapshot("prewarm");
      vectorEvict = nullptr;
    }
    snapshot("destroy");
    // FreeInkFont retains one shared FreeType library for the process lifetime.
    if (mode == 0) retained = bytes;
    else if (bytes != retained) std::abort();
  }
}
