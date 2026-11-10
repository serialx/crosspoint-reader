#ifdef NDEBUG
#undef NDEBUG
#endif
#include <GfxRenderer.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <xp_font_registry.h>
#include <xp_sd_physical.h>

#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {
std::array<uint8_t, 100> font;
std::map<void*, size_t> live;
size_t allocations, failAllocation, operations, failOperation, handles;
XpLoadedFonts* current;
bool corruptGlyph;
constexpr char familyName[] = "FamilyNameLongEnoughForSSOGrowth";
void put(size_t offset, uint32_t value, size_t bytes = 4) {
  for (size_t i = 0; i < bytes; ++i) font[offset + i] = value >> (8 * i);
}
void fixture() {
  font.fill(0);
  std::memcpy(font.data(), "CPFONT\0\0", 8);
  put(8, 4);
  put(12, 1);
  put(36, 1);
  put(40, 1);
  put(44, 18, 1);
  put(45, 16, 2);
  put(56, 64);
  put(64, 65);
  put(68, 65);
  font[76] = font[77] = 8;
  put(78, 128, 2);
  put(82, 8, 2);
  put(84, corruptGlyph ? 7 : 8, 2);
  for (size_t i = 92; i < 100; ++i) font[i] = 0xff;
}
bool operation() { return ++operations != failOperation; }
void fixed(XpSdFileSlot* slot) { assert(slot->words[0] == reinterpret_cast<uintptr_t>(slot)); }
const char* text(const void*, uint32_t) { return "A"; }
void run(size_t failAlloc, size_t failIo) {
  assert(live.empty() && handles == 0);
  allocations = operations = 0;
  failAllocation = failAlloc;
  failOperation = failIo;
  fixture();
  XpLoadedFonts owner;
  std::atomic<uint32_t> generation{1};
  xp_font_registry_init(&owner, &generation, nullptr);
  current = &owner;
  GfxRenderer renderer(&owner);
  SdCardFontManager manager;
  SdCardFontFamilyInfo family;
  family.name = familyName;
  family.root = "/fonts";
  family.files.push_back({nullptr, 14, 0});
  const bool loaded = manager.loadFamily(family, renderer, 14);
  const auto id = manager.getFontId(familyName);
  assert(loaded == (id != 0));
  if (loaded) {
    assert(manager.currentFamilyName() == familyName && manager.currentPointSize() == 14);
    assert(manager.loadFamilyExtraSize(family, renderer, 14, 123) == id);
  }
  if (id) {
    assert(xp_sd_font_id(&owner, familyName, sizeof(familyName) - 1, 0) == id);
    uint8_t size = 0;
    assert(std::strcmp(xp_sd_font_current(&owner, &size), familyName) == 0 && size == 14);
    assert(xp_font_registry_query(&owner, XP_FONT_SD, id, 0, nullptr).value == 1);
    auto slot = xp_font_registry_query(&owner, XP_FONT_SLOT, id, 0, nullptr).slot;
    assert(slot && *slot);
    auto glyph = xp_font_lookup(*slot, 65);
    if (glyph) {
      XpFontGlyphContext context{&owner, slot};
      XpGlyphDraw draw{};
      XpGlyphSource source{};
      auto bitmap = xp_font_glyph_miss(&context, 65, glyph, &draw, &source);
      if (!corruptGlyph && !failAlloc && !failIo) {
        assert(bitmap.data && bitmap.len == 8 && bitmap.data[0] == 0xff);
        assert(source.overflowCount == 1);
      }
    }
    if (corruptGlyph) assert(xp_sd_font_take_corrupt_notice() == 1);
    const char* word = "A";
    const size_t length = 1;
    XpSdfontTexts input{&word, &length, 1, nullptr, false, false, false};
    xp_sd_font_prepare(&owner, id, &input, 1);
    XpParaSpan span{reinterpret_cast<const uint8_t*>(word), 1};
    xp_sd_font_prepare_spans(&owner, id, &span, 1, false, false, 1, false);
    xp_sd_font_prepare_chapter(&owner, id, &span, 1, false, false, 1, false);
    xp_sd_font_prepare_spans(&owner, id, nullptr, 0, false, false, 1, false);
    XpParaSpan empty{nullptr, 0};
    xp_sd_font_prepare_spans(&owner, id, &empty, 1, false, false, 1, false);
    xp_sd_font_prewarm(&owner, id, text, nullptr, 1, 1, false, false, true);
    xp_font_frame_begin(&owner);
    xp_font_record_text(&owner, id, "A", 0);
    xp_font_frame_finish(&owner);
    xp_font_frame_end(&owner);
    auto provider = xp_font_registry_provider(&owner);
    XpNativeFamily view{};
    provider.family(provider.context, id, &view);
    assert(view.sd && view.styles[0].data == *slot);
    xp_font_cache_release(&owner, XP_FONT_CLEAR_RESIDENT);
    assert(xp_font_registry_query(&owner, XP_FONT_EXISTS, id, 0, nullptr).value == 1);
    if (corruptGlyph) assert(xp_sd_font_take_corrupt_notice() == 0);
    manager.unloadFont(renderer, id);
    assert(xp_font_registry_query(&owner, XP_FONT_EXISTS, id, 0, nullptr).value == 0);
  }
  xp_font_registry_drop(&owner);
  assert(live.empty() && handles == 0);
  current = nullptr;
}
}  // namespace
XpFontFiles sdFontFileView(const std::vector<SdCardFontFileInfo>& files) {
  return {&files, files.size(), [](const void* raw, size_t index) -> XpFontFile {
            const auto& file = (*static_cast<const std::vector<SdCardFontFileInfo>*>(raw))[index];
            return {file.name.get(), file.pointSize, file.style};
          }};
}
extern "C" uint8_t* xp_native_sd_allocate(size_t bytes, bool) {
  if (++allocations == failAllocation) return nullptr;
  auto* p = static_cast<uint8_t*>(std::malloc(bytes));
  assert(p && live.emplace(p, bytes).second);
  return p;
}
extern "C" void xp_native_sd_free(uint8_t* data, bool) {
  if (!data) return;
  auto allocation = live.find(data);
  assert(allocation != live.end());
  // Every registered slot must be removed before its style allocation is freed.
  for (uint32_t i = 0; current && i < xp_font_registry_query(current, XP_FONT_COUNT, 0, 0, nullptr).value; ++i) {
    XpRegisteredFont entry{};
    xp_font_registry_query(current, XP_FONT_AT, 0, i, &entry);
    for (auto slot : entry.slots) {
      auto address = reinterpret_cast<uintptr_t>(slot), first = reinterpret_cast<uintptr_t>(data);
      assert(!slot || address < first || address >= first + allocation->second);
    }
  }
  live.erase(allocation);
  std::free(data);
}
extern "C" void xp_native_sd_handle_init(XpSdFileSlot* slot) {
  slot->words[0] = reinterpret_cast<uintptr_t>(slot);
  slot->words[1] = 0;
  ++handles;
}
extern "C" void xp_native_sd_handle_destroy(XpSdFileSlot* slot) {
  fixed(slot);
  slot->words[0] = 0;
  --handles;
}
extern "C" uint8_t xp_native_sd_file_open(XpSdFileSlot* slot, const char*, uint32_t* size) {
  fixed(slot);
  if (!operation()) return 0;
  slot->words[1] = 0;
  *size = font.size();
  return 1;
}
extern "C" int32_t xp_native_sd_file_read(XpSdFileSlot* slot, uint8_t* bytes, size_t length) {
  fixed(slot);
  if (!operation()) return 0;
  assert(slot->words[1] + length <= font.size());
  std::memcpy(bytes, font.data() + slot->words[1], length);
  slot->words[1] += length;
  return length;
}
extern "C" uint8_t xp_native_sd_file_seek(XpSdFileSlot* slot, uint32_t offset) {
  fixed(slot);
  if (!operation() || offset > font.size()) return 0;
  slot->words[1] = offset;
  return 1;
}
extern "C" void xp_native_sd_file_close(XpSdFileSlot* slot) { fixed(slot); }
extern "C" void xp_native_sd_diagnostic(const char*, uint32_t, const uint32_t*, uint32_t, uint32_t, bool) {}
extern "C" void xp_native_font_cache_diagnostic(const void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
extern "C" void xp_native_font_diagnostic(void*, uint32_t, uint32_t) {}
extern "C" const XpLoadedFonts* xp_native_chapter_font_owner(void* context) {
  return static_cast<XpLoadedFonts*>(context);
}
int main() {
  run(0, 0);
  const auto allocationCount = allocations, operationCount = operations;
  for (size_t i = 1; i <= allocationCount; ++i) run(i, 0);
  for (size_t i = 1; i <= operationCount; ++i) run(0, i);
  corruptGlyph = true;
  run(0, 0);
  assert(xp_sd_font_take_corrupt_notice() == 0);
  run(0, 0);  // A freshly loaded corrupt face gets its own one-shot notice.
}
