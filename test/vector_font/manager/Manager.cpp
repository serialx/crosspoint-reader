#include <CrossPointSettings.h>
#include <FtFont.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <ReaderFontSizes.h>
#include <SdCardFontSystem.h>
#include <esp_heap_caps.h>
#include <xp_sd_physical.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
namespace manager_test {
std::vector<uint8_t> font;
size_t fileSize, opens, closes, reads, seeks;
int mode;
GfxRenderer* renderer;
struct Block {
  void* p;
  size_t bytes;
  bool font;
};
std::map<void*, Block> blocks;
size_t live, peak, allocations, pressureStart;
bool forcingPressure;
void* allocate(size_t bytes, bool isFont) {
  void* p = malloc(bytes ? bytes : 1);
  assert(p);
  if (isFont) {
    live += bytes;
    peak = std::max(peak, live);
    ++allocations;
  }
  blocks.emplace(p, Block{p, bytes, isFont});
  return p;
}
void release(void* p) {
  if (!p) return;
  const auto found = blocks.find(p);
  assert(found != blocks.end());
  if (found->second.font) live -= found->second.bytes;
  blocks.erase(found);
  free(p);
}
bool pressure(size_t bytes) {
  const size_t have = heap_caps_get_free_size(1 << 12);
  if (have >= bytes) return true;
  size_t freed = 0;
  for (uint8_t sink : {2, 3, 4}) {
    freed += xp_font_cache_release(renderer->getFontMap().raw(), sink);
    if (freed >= bytes - have) break;
  }
  return heap_caps_get_free_size(1 << 12) >= bytes;
}
void snapshot(const char* tag, SdCardFontSystem& system) {
  const int id = system.resolveFontId(SETTINGS.sdFontFamilyName, SETTINGS.fontPointSize);
  printf("STATE %s %s %u %d %u %zu %zu %zu %zu\n", tag, SETTINGS.sdFontFamilyName, SETTINGS.fontPointSize, id,
         renderer->getFontMap().size(), allocations, live, peak, opens);
  printf("HAL %zu %zu %zu\n", impls, implPeak, implAllocations);
  for (auto entry : renderer->getFontMap())
    printf("ENTRY %d %d\n", entry.first, renderer->getFontMap().fallback(entry.first));
}
void draw(int id) {
  for (uint8_t style = 0; style != 4; ++style) {
    auto family = renderer->getFontMap().at(id);
    auto st = static_cast<EpdFontFamily::Style>(style);
    for (uint32_t cp : {65u, 86u, 0x3b1u, 0xfb01u}) {
      const auto* glyph = family.getGlyph(cp, st);
      assert(glyph);
      const auto* data = family.getData(st);
      const auto* bytes = data->vectorBitmapHandler(data->glyphMissCtx, glyph);
      uint32_t hash = 2166136261u;
      for (unsigned i = 0; i != glyph->dataLength; ++i) hash = (hash ^ bytes[i]) * 16777619u;
      printf("GLYPH %u %u %u %u %u %d %d %u %u\n", style, cp, glyph->width, glyph->height, glyph->advanceX, glyph->left,
             glyph->top, glyph->dataLength, hash);
    }
    printf("KERN %u %d\n", style, family.getKerning(65, 86, st));
  }
}
}  // namespace manager_test
CrossPointSettings systemSettings;
bool CrossPointSettings::saveToFile() const {
  printf("SAVE %s %u\n", sdFontFamilyName, fontPointSize);
  return true;
}
void CrossPointSettings::clearSdFontFamily() {
  sdFontFamilyName[0] = 0;
  fontPointSize = 14;
  saveToFile();
}
extern "C" size_t heap_caps_get_largest_free_block(uint32_t cap) {
  using namespace manager_test;
  if (forcingPressure && (live >= pressureStart || pressureStart - live < 16 * 1024)) return 0;
  if (cap == MALLOC_CAP_SPIRAM) {
    if (mode == 2 || mode == 7 || mode == 8) return 0;
    if (mode == 5 || mode == 6) return 1024 * 1024 + 256 * 1024 + (mode == 6);
    return 8 * 1024 * 1024;
  }
  if (mode == 7 || mode == 8) return font.size() + 96 * 1024 - (mode == 8);
  return mode == 2 ? 100 * 1024 : 200 * 1024;
}
extern "C" size_t heap_caps_get_free_size(uint32_t cap) {
  using namespace manager_test;
  if (cap == 1 << 12 && forcingPressure) return live >= pressureStart ? 0 : pressureStart - live;
  return heap_caps_get_largest_free_block(cap);
}
extern "C" void* fiFontMalloc(size_t bytes) { return manager_test::allocate(bytes, true); }
extern "C" void fiFontFree(void* p) { manager_test::release(p); }
extern "C" void* fiFontRealloc(void* p, size_t bytes) {
  using namespace manager_test;
  if (!p) return fiFontMalloc(bytes);
  const auto found = blocks.find(p);
  assert(found != blocks.end() && found->second.font);
  const auto old = found->second.bytes;
  blocks.erase(found);
  void* next = realloc(p, bytes ? bytes : 1);
  assert(next);
  live = live - old + bytes;
  peak = std::max(peak, live);
  ++allocations;
  blocks.emplace(next, Block{next, bytes, true});
  return next;
}
extern "C" uint8_t* xp_native_sd_allocate(size_t bytes, uint8_t) { return static_cast<uint8_t*>(malloc(bytes)); }
extern "C" void xp_native_sd_free(uint8_t* bytes, bool) { free(bytes); }
static HalFile& slot(XpSdFileSlot* p) { return *std::launder(reinterpret_cast<HalFile*>(p)); }
static_assert(sizeof(HalFile) <= sizeof(XpSdFileSlot));
extern "C" void xp_native_sd_handle_init(XpSdFileSlot* p) { new (p) HalFile; }
extern "C" void xp_native_sd_handle_destroy(XpSdFileSlot* p) { slot(p).~HalFile(); }
extern "C" uint8_t xp_native_sd_file_open(XpSdFileSlot* p, const char* path, uint32_t* size) {
  slot(p) = Storage.open(path);
  if (!slot(p)) return 0;
  *size = slot(p).size();
  return 1;
}
extern "C" int32_t xp_native_sd_file_read(XpSdFileSlot* p, uint8_t* out, size_t count) {
  return slot(p).read(out, count);
}
extern "C" uint8_t xp_native_sd_file_seek(XpSdFileSlot* p, uint32_t at) { return slot(p).seek(at); }
extern "C" void xp_native_sd_file_close(XpSdFileSlot* p) { slot(p).close(); }
extern "C" int32_t xp_native_sd_directory_open(XpSdFileSlot* p, const char* path) {
  slot(p) = Storage.open(path);
  return !slot(p) ? 0 : slot(p).isDirectory() ? 1 : -1;
}
extern "C" bool xp_native_sd_directory_next(XpSdFileSlot* p, char* name, bool* directory, bool names) {
  auto f = slot(p).openNextFile();
  if (!f) return false;
  *directory = f.isDirectory();
  if (names || !*directory) f.getName(name, 128);
  f.close();
  return true;
}
extern "C" bool xp_native_sd_path_exists(const char* path) { return Storage.exists(path); }
extern "C" bool xp_native_sd_inspect(const char*, uint16_t* weight, bool* italic) {
  *weight = 400;
  *italic = false;
  return true;
}
extern "C" uint8_t xp_native_sd_font_load(void* raw, int32_t id) {
  auto& p = static_cast<GfxRenderer*>(raw)->provider;
  return p.load && p.load(p.ctx, id);
}
extern "C" void xp_native_sd_diagnostic(const char*, uint32_t, const uint32_t*, uint32_t, uint32_t, bool, const char*) {
}
extern "C" void xp_native_font_diagnostic(void*, uint32_t, uint32_t) {}
extern "C" void xp_native_font_compat_reset(void*, int32_t) {}
extern "C" uint8_t xp_native_font_compat_resolve(void* raw, int32_t id, uint8_t style) {
  (void)raw;
  (void)id;
  return style;
}
extern "C" int32_t xp_native_font_compat_prewarm(void* raw, int32_t id, uint8_t, const char* text, uint8_t, uint8_t,
                                                 uint8_t, uint8_t accumulate) {
  auto* font = static_cast<GfxRenderer*>(raw)->ttf(id);
  assert(font);
  if (accumulate)
    font->addCoverage(text);
  else
    font->build(text);
  return 0;
}
extern "C" void xp_native_font_compat_clear(void* raw, int32_t id, uint8_t, uint8_t resident) {
  auto* font = static_cast<GfxRenderer*>(raw)->ttf(id);
  assert(font);
  if (resident)
    font->releaseResidentCaches();
  else
    font->clearCache();
}
unsigned long SdCardFontRegistry::halFileRead(void* raw, unsigned long at, unsigned char* bytes, unsigned long count) {
  auto& f = *static_cast<HalFile*>(raw);
  if (!count) return 0;
  if (!f || !f.seek(at)) return 0;
  const int got = f.read(bytes, count);
  return got < 0 ? 0 : got;
}
int main(int argc, char** argv) {
  using namespace manager_test;
  if (argc == 2) {
    HalFile file;
    if (!strcmp(argv[1], "--close-moved")) {
      file = HalFile(-1, -2, 0);
      HalFile moved = std::move(file);
      assert(!file.isOpen());
      file.close();
    } else if (!strcmp(argv[1], "--close-default")) {
      assert(!file.isOpen());
      file.close();
    } else {
      file = HalFile(-1, -2, 0);
      file.close();
      assert(!file.isOpen());
      file.close();
    }
    return 0;
  }
  assert(argc == 3);
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::ifstream input(argv[1], std::ios::binary);
  font.assign(std::istreambuf_iterator<char>(input), {});
  assert(!font.empty());
  mode = atoi(argv[2]);
  fileSize = mode == 1 || mode == 9 || (mode >= 3 && mode <= 6) ? 6 * 1024 * 1024 + (mode != 4) : font.size();
  if (mode == 10) fileSize = 0;
  {
    freeink::font::FtFont prime;
    assert(prime.init(font.data(), font.size(), 28));
  }
  const size_t sharedLibrary = live;
  allocations = 0;
  peak = live;
  printf("SHARED_LIBRARY %zu\n", sharedLibrary);
  {
    GfxRenderer graphics;
    renderer = &graphics;
    SdCardFontSystem system;
    strcpy(SETTINGS.sdFontFamilyName, families[0]);
    SETTINGS.fontPointSize = 14;
    system.begin(graphics);
    snapshot("BOOT", system);
    if (mode < 10) {
      int reader = system.resolveFontId(families[0], 14);
      assert(reader);
      draw(reader);
      snapshot("DRAW", system);
      int first = 0;
      for (float scale : {1.5f, 1.8f, 2.2f, 2.6f, 2.8f, 3.0f}) {
        const int id = graphics.resolveFontVariant(reader, scale);
        assert(id);
        if (!first) first = id;
        draw(id);
        snapshot("VARIANT", system);
      }
      assert(!graphics.getFontMap().count(first));
      XpRegisteredFont entry{first, {}, 0};
      assert(xp_font_registry_update(graphics.getFontMap().raw(), XP_FONT_ENSURE_LOADED, &entry));
      snapshot("RELOAD", system);
      xp_font_frame_begin(graphics.getFontMap().raw());
      xp_font_record_text(graphics.getFontMap().raw(), reader, "AV alpha", 0);
      xp_font_frame_finish(graphics.getFontMap().raw());
      draw(reader);
      xp_font_frame_end(graphics.getFontMap().raw());
      snapshot("SCAN", system);
      if (mode == 3) {
        xp_font_cache_release(graphics.getFontMap().raw(), XP_FONT_CLEAR_FRAME);
        auto family = graphics.getFontMap().at(reader);
        for (uint32_t cp = 32; cp != 96; ++cp) assert(family.getGlyph(cp));
        pressureStart = live;
        forcingPressure = true;
        const auto* glyph = family.getGlyph(96);
        assert(glyph && glyph->dataLength);
        forcingPressure = false;
        assert(live < pressureStart);
        snapshot("PRESSURE", system);
        draw(reader);
      }
      SETTINGS.fontPointSize = 18;
      system.ensureLoaded(graphics);
      snapshot("RESIZE", system);
      assert(!graphics.getFontMap().count(reader));
      if (mode == 9) fileSize = font.size();
      strcpy(SETTINGS.sdFontFamilyName, families[1]);
      system.ensureLoaded(graphics);
      snapshot("REPLACE", system);
      draw(system.resolveFontId(families[1], 18));
      if (mode == 9) {
        fileSize = 6 * 1024 * 1024 + 1;
        strcpy(SETTINGS.sdFontFamilyName, families[0]);
        system.ensureLoaded(graphics);
        snapshot("RESTREAM", system);
        draw(system.resolveFontId(families[0], 18));
      }
      SETTINGS.sdFontFamilyName[0] = 0;
      system.ensureLoaded(graphics);
      snapshot("UNLOAD", system);
      assert(graphics.getFontMap().size() == 3);
      for (int id : {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID}) assert(graphics.getFontMap().fallback(id) == 0);
    } else {
      assert(system.resolveFontId(families[0], 14) == 0);
      assert(graphics.getFontMap().size() == 3);
      assert(!strcmp(SETTINGS.sdFontFamilyName, families[0]));
      system.ensureLoaded(graphics);
      snapshot("FAILED_RETRY", system);
      assert(impls == 0);
    }
    assert(opens == closes);
  }
  assert(live == sharedLibrary);
  assert(impls == 0);
  printf("TOTAL %zu %zu %zu %zu %zu %zu\n", allocations, peak, opens, closes, reads, seeks);
}
