#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <ReaderFontSizes.h>
#include <SdCardFontSystem.h>
#include <xp_sd_physical.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "../hal_contract/Slots.h"

namespace systemtest {
struct Live {
  void* pointer;
  size_t bytes;
  bool family;
};
Live live[4096]{};
bool recording;
size_t allocations, operations, saves, failAllocation, failOperation, failSave;
GfxRenderer* renderer;
char disk[128];
size_t largest() { return 200 * 1024; }
void state(const char* tag) {
  std::printf("%s %s %u %u %s\n", tag, SETTINGS.sdFontFamilyName, SETTINGS.fontPointSize,
              renderer ? renderer->getFontMap().size() : 0, disk);
}
void* allocate(size_t bytes, bool required) {
  if (recording) {
    ++allocations;
    std::printf("ALLOC %s%zu %d\n", required && family_end ? "FAMILY " : "", bytes, required);
    if (allocations == failAllocation) {
      if (required) {
        state("FATAL");
        std::fflush(stdout);
        std::_Exit(70);
      }
      return nullptr;
    }
  }
  void* result = std::malloc(bytes ? bytes : 1);
  assert(result);
  if (recording) {
    for (auto& slot : live)
      if (!slot.pointer) {
        slot = {result, bytes, required && family_end};
        return result;
      }
    std::abort();
  }
  return result;
}
void release(void* data) {
  if (!data) return;
  for (auto& slot : live)
    if (slot.pointer == data) {
      if (recording) std::printf("FREE %s%zu\n", slot.family ? "FAMILY " : "", slot.bytes);
      slot.pointer = nullptr;
      break;
    }
  std::free(data);
}
bool operation(const char* kind) {
  ++operations;
  std::printf("IO %s\n", kind);
  return operations != failOperation;
}
// The file member carries the impl state; position is the scripted read offset.
struct Slot {
  HalFile file;
  size_t position = 0;
};
static_assert(sizeof(Slot) <= sizeof(XpSdFileSlot));
static_assert(alignof(Slot) <= alignof(XpSdFileSlot));
Slot& slot(XpSdFileSlot* p) { return *std::launder(reinterpret_cast<Slot*>(p)); }
std::array<uint8_t, 152> font() {
  std::array<uint8_t, 152> bytes{};
  std::memcpy(bytes.data(), "CPFONT\0\0", 8);
  bytes[8] = 4;
  bytes[12] = 2;
  for (int style = 0; style < 2; ++style) {
    const int toc = 32 + style * 32;
    bytes[toc] = style == 0 ? 0 : 3;
    bytes[toc + 4] = 1;
    bytes[toc + 8] = 2;
    bytes[toc + 17] = 1;
    bytes[toc + 19] = 1;
    bytes[toc + 24] = 96;
  }
  bytes[97] = bytes[101] = 0xac;
  bytes[100] = 1;
  return bytes;
}
const auto bytes = font();
}  // namespace systemtest
void* operator new(size_t bytes) { return systemtest::allocate(bytes, true); }
void* operator new[](size_t bytes) { return systemtest::allocate(bytes, true); }
void* operator new(size_t bytes, const std::nothrow_t&) noexcept { return systemtest::allocate(bytes, false); }
void* operator new[](size_t bytes, const std::nothrow_t&) noexcept { return systemtest::allocate(bytes, false); }
void operator delete(void* data, const std::nothrow_t&) noexcept { systemtest::release(data); }
void operator delete[](void* data, const std::nothrow_t&) noexcept { systemtest::release(data); }
void operator delete(void* data) noexcept { systemtest::release(data); }
void operator delete[](void* data) noexcept { systemtest::release(data); }
void operator delete(void* data, size_t) noexcept { systemtest::release(data); }
void operator delete[](void* data, size_t) noexcept { systemtest::release(data); }
CrossPointSettings systemSettings;
bool CrossPointSettings::saveToFile() const {
  ++systemtest::saves;
  const bool ok = systemtest::saves != systemtest::failSave;
  if (ok)
    std::snprintf(systemtest::disk, sizeof(systemtest::disk),
                  "{\"fontPointSize\":%u,\"sdFontFamilyName\":\"%s\",\"sentinel\":31}", fontPointSize,
                  sdFontFamilyName);
  systemtest::state(ok ? "SAVE" : "SAVE-FAILED");
  return ok;
}
void CrossPointSettings::clearSdFontFamily() {
  sdFontFamilyName[0] = 0;
  fontPointSize = snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, 4, fontPointSize);
  saveToFile();
}
extern "C" uint8_t* xp_native_sd_allocate(size_t bytes, uint8_t domain) {
  return static_cast<uint8_t*>(systemtest::allocate(bytes, domain == XP_SD_REQUIRED));
}
extern "C" void xp_native_sd_free(uint8_t* data, bool) { systemtest::release(data); }
extern "C" void xp_native_sd_handle_init(XpSdFileSlot* p) { new (p) systemtest::Slot; }
extern "C" void xp_native_sd_handle_destroy(XpSdFileSlot* p) { systemtest::slot(p).~Slot(); }
extern "C" uint8_t xp_native_sd_file_open(XpSdFileSlot* p, const char* path, uint32_t* size) {
  // HalStorage installs an impl for every open attempt, so a failed open stays closeable.
  systemtest::slot(p).file = Storage.open(path);
  if (!systemtest::operation("open")) return 0;
  systemtest::slot(p).position = 0;
  *size = systemtest::bytes.size();
  return 1;
}
extern "C" int32_t xp_native_sd_file_read(XpSdFileSlot* p, uint8_t* out, size_t count) {
  hal_fake::requireImpl(systemtest::slot(p).file.hasImpl, "read");
  if (!systemtest::operation("read")) return 0;
  auto& at = systemtest::slot(p).position;
  assert(at + count <= systemtest::bytes.size());
  std::memcpy(out, systemtest::bytes.data() + at, count);
  at += count;
  return count;
}
extern "C" uint8_t xp_native_sd_file_seek(XpSdFileSlot* p, uint32_t at) {
  hal_fake::requireImpl(systemtest::slot(p).file.hasImpl, "seek");
  if (!systemtest::operation("seek") || at > systemtest::bytes.size()) return 0;
  systemtest::slot(p).position = at;
  return 1;
}
extern "C" void xp_native_sd_file_close(XpSdFileSlot* p) {
  hal_fake::requireImpl(systemtest::slot(p).file.hasImpl, "close");
  systemtest::slot(p).file.close();
}
extern "C" int32_t xp_native_sd_directory_open(XpSdFileSlot* p, const char* path) {
  auto& f = systemtest::slot(p).file;
  f = Storage.open(path);
  return !f ? 0 : f.isDirectory() ? 1 : -1;
}
extern "C" bool xp_native_sd_directory_next(XpSdFileSlot* p, char* name, bool* directory, bool names) {
  auto entry = systemtest::slot(p).file.openNextFile();
  if (!entry) return false;
  *directory = entry.isDirectory();
  if (names || !*directory) entry.getName(name, 128);
  entry.close();
  return true;
}
extern "C" bool xp_native_sd_path_exists(const char* path) { return Storage.exists(path); }
extern "C" bool xp_native_sd_inspect(const char*, uint16_t*, bool*) { return false; }
extern "C" size_t heap_caps_get_largest_free_block(uint32_t) { return systemtest::largest(); }
extern "C" void xp_native_sd_diagnostic(const char*, uint32_t, const uint32_t*, uint32_t, uint32_t, bool, const char*) {
}
extern "C" uint8_t xp_native_sd_font_load(void* raw, int32_t id) {
  auto& p = static_cast<GfxRenderer*>(raw)->provider;
  return p.load && p.load(p.ctx, id);
}
static void select(const char* name, uint8_t points) {
  std::snprintf(SETTINGS.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName), "%s", name);
  SETTINGS.fontPointSize = points;
}
int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--hal-contract") == 0) {
    hal_contract::checkSlots([] { systemtest::failOperation = systemtest::operations + 1; });
    return 0;
  }
  using namespace systemtest;
  std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef XP_SYSTEM_PARENT
  std::printf("FAMILY_WIDTH %zu\n", sizeof(SdCardFontFamilyInfo));
#else
  struct Name {
    uint8_t text[16];
    size_t length, capacity;
  };
  struct Family {
    Name name;
    const void* files;
    size_t length, capacity;
    const char* root;
    bool vector, loose;
  };
  static_assert(sizeof(Family) == 72);
  std::printf("FAMILY_WIDTH %zu\n", sizeof(Family));
#endif
  failAllocation = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 0;
  failOperation = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 0;
  failSave = argc > 3 ? std::strtoul(argv[3], nullptr, 10) : 0;
  {
    GfxRenderer graphics;
    renderer = &graphics;
    {
      SdCardFontSystem system;
      select("F0", 16);
      std::strcpy(disk, "initial-settings");
      recording = true;
      system.begin(graphics);
      state("BOOT");
      select("F0", 16);
      system.ensureLoaded(graphics);
      state("KEEP-SNAP");
      select("F1", 16);
      system.ensureLoaded(graphics);
      state("REPLACE");
      select("F1", 9);
      system.markRegistryDirty();
      system.ensureLoaded(graphics);
      state("DIRTY-SNAP");
      const int reader = system.resolveFontId("F1", SETTINGS.fontPointSize);
      std::printf("READER %d\n", reader);
      for (float scale : {1.5f, 1.8f, 2.2f, 2.6f, 2.8f, 3.0f, 3.3f, 1.8f}) {
        std::printf("VARIANT %d\n", graphics.resolveFontVariant(reader, scale));
      }
      select("Missing", 15);
      system.ensureLoaded(graphics);
      state("MISSING");
      select("F0", 14);
      system.ensureLoaded(graphics);
      present = false;
      system.markRegistryDirty();
      system.ensureLoaded(graphics);
      state("DISAPPEARED");
      select("", 15);
      system.ensureLoaded(graphics);
      state("BUILTIN");
      recording = false;
    }
  }
  renderer = nullptr;
  for (const auto& entry : live) assert(!entry.pointer);
  std::printf("TOTAL %zu %zu %zu\n", allocations, operations, saves);
}
