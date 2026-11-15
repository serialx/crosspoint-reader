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
#include <vector>

#include "../hal_contract/Slots.h"
#include "../hal_fake_strict.h"

namespace {
std::array<uint8_t, 172> font;
std::vector<uint8_t> bidiWork;
bool bidiHeld;
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
  put(36, 3);
  put(40, 3);
  put(44, 18, 1);
  put(45, 16, 2);
  put(56, 64);
  const uint32_t cps[] = {65, 0x0628, 0xfe8f};
  for (size_t i = 0; i < 3; ++i) {
    put(64 + i * 12, cps[i]);
    put(68 + i * 12, cps[i]);
    put(72 + i * 12, i);
    const size_t glyph = 100 + i * 16;
    font[glyph] = font[glyph + 1] = 8;
    put(glyph + 2, 128 + i * 16, 2);
    put(glyph + 6, 8, 2);
    put(glyph + 8, corruptGlyph ? 7 : 8, 2);
    put(glyph + 12, i * 8);
  }
  for (size_t i = 148; i < font.size(); ++i) font[i] = 0xff;
}
bool operation() { return ++operations != failOperation; }
void fixed(XpSdFileSlot* slot) { assert(slot->words[0] == reinterpret_cast<uintptr_t>(slot)); }
// Word 2 stands in for HalFile::impl: an open attempt installs it, destroy clears it.
constexpr size_t IMPL_WORD = 2;
void requireImpl(XpSdFileSlot* slot, const char* operation) {
  hal_fake::requireImpl(slot->words[IMPL_WORD] != 0, operation);
}
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
    const char* arabic = "\xd8\xa8";
    const size_t arabicLength = 2;
    XpSdfontTexts rtl{&arabic, &arabicLength, 1, nullptr, false, false, false};
    xp_sd_font_prepare(&owner, id, &rtl, 1);
    auto rtlProvider = xp_font_registry_provider(&owner);
    XpNativeFamily rtlView{};
    rtlProvider.family(rtlProvider.context, id, &rtlView);
    if (!corruptGlyph && !failAlloc && !failIo) {
      uint16_t advance;
      assert(xp_font_sd_advance(&rtlView.styles[0], 0x0628, &advance) && advance == 144);
      assert(xp_font_sd_advance(&rtlView.styles[0], 0xfe8f, &advance) && advance == 160);
    }
    assert(!bidiHeld);
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
extern "C" uint8_t* xp_native_sd_allocate(size_t bytes, uint8_t) {
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
  slot->words[IMPL_WORD] = 0;
  ++handles;
}
extern "C" void xp_native_sd_handle_destroy(XpSdFileSlot* slot) {
  fixed(slot);
  slot->words[0] = 0;
  slot->words[IMPL_WORD] = 0;
  --handles;
}
extern "C" uint8_t xp_native_sd_file_open(XpSdFileSlot* slot, const char*, uint32_t* size) {
  fixed(slot);
  slot->words[IMPL_WORD] = 1;  // Every open attempt installs an impl, as HalFile does.
  if (!operation()) return 0;
  slot->words[1] = 0;
  *size = font.size();
  return 1;
}
extern "C" int32_t xp_native_sd_file_read(XpSdFileSlot* slot, uint8_t* bytes, size_t length) {
  fixed(slot);
  requireImpl(slot, "read");
  assert(!bidiHeld);  // No SD read may run while token shaping holds its workspace.
  if (!operation()) return 0;
  assert(slot->words[1] + length <= font.size());
  std::memcpy(bytes, font.data() + slot->words[1], length);
  slot->words[1] += length;
  return length;
}
extern "C" uint8_t xp_native_sd_file_seek(XpSdFileSlot* slot, uint32_t offset) {
  fixed(slot);
  requireImpl(slot, "seek");
  if (!operation() || offset > font.size()) return 0;
  slot->words[1] = offset;
  return 1;
}
extern "C" void xp_native_sd_file_close(XpSdFileSlot* slot) {
  fixed(slot);
  requireImpl(slot, "close");
}
extern "C" void xp_native_sd_diagnostic(const char*, uint32_t, const uint32_t*, uint32_t, uint32_t, bool, const char*) {
}
extern "C" void xp_native_font_cache_diagnostic(const void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
extern "C" void xp_native_font_diagnostic(void*, uint32_t, uint32_t) {}
extern "C" const XpLoadedFonts* xp_native_chapter_font_owner(void* context) {
  return static_cast<XpLoadedFonts*>(context);
}
extern "C" void* xp_native_bidi_acquire(size_t, size_t) {
  assert(!bidiHeld);
  if (bidiWork.empty()) {
    bidiWork.resize(xp_bidi_workspace_size());
    assert(reinterpret_cast<uintptr_t>(bidiWork.data()) % xp_bidi_workspace_align() == 0);
    assert(xp_bidi_workspace_init(bidiWork.data(), bidiWork.size()) == 1);
  }
  bidiHeld = true;
  return bidiWork.data();
}
extern "C" void xp_native_bidi_release() {
  assert(bidiHeld);
  bidiHeld = false;
}
int main(int argc, char**) {
  if (argc > 1) {
    hal_contract::checkSlots([] { failOperation = operations + 1; });
    return 0;
  }
  run(0, 0);
  const auto allocationCount = allocations, operationCount = operations;
  for (size_t i = 1; i <= allocationCount; ++i) run(i, 0);
  for (size_t i = 1; i <= operationCount; ++i) run(0, i);
  corruptGlyph = true;
  run(0, 0);
  assert(xp_sd_font_take_corrupt_notice() == 0);
  run(0, 0);  // A freshly loaded corrupt face gets its own one-shot notice.
}
