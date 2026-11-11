#include <HalStorage.h>
#include <VectorFontSupport.h>
#include <xp_sd_physical.h>

#if defined(XP_SD_FONT_CATALOG) && CROSSPOINT_VECTOR_FONTS
#include <FtFont.h>
#endif

#include <new>

static_assert(sizeof(HalFile) <= sizeof(XpSdFileSlot));
static_assert(alignof(HalFile) <= alignof(XpSdFileSlot));
static_assert(sizeof(void*) != 4 || sizeof(HalFile) == sizeof(XpSdFileSlot));

namespace {
HalFile& file(XpSdFileSlot* slot) { return *std::launder(reinterpret_cast<HalFile*>(slot)); }
}  // namespace

extern "C" void xp_native_sd_handle_init(XpSdFileSlot* slot) { new (slot) HalFile(); }
extern "C" void xp_native_sd_handle_destroy(XpSdFileSlot* slot) { file(slot).~HalFile(); }
extern "C" uint8_t xp_native_sd_file_open(XpSdFileSlot* slot, const char* path, uint32_t* size) {
  if (!Storage.openFileForRead("SDCF", path, file(slot))) return 0;
  *size = file(slot).fileSize();
  return 1;
}
extern "C" int32_t xp_native_sd_file_read(XpSdFileSlot* slot, uint8_t* bytes, size_t length) {
  return file(slot).read(bytes, length);
}
extern "C" uint8_t xp_native_sd_file_seek(XpSdFileSlot* slot, uint32_t offset) { return file(slot).seekSet(offset); }
extern "C" void xp_native_sd_file_close(XpSdFileSlot* slot) { file(slot).close(); }

#ifdef XP_SD_FONT_CATALOG
extern "C" int32_t xp_native_sd_directory_open(XpSdFileSlot* slot, const char* path) {
  auto& directory = file(slot);
  directory = Storage.open(path);
  return !directory ? 0 : directory.isDirectory() ? 1 : -1;
}
extern "C" bool xp_native_sd_directory_next(XpSdFileSlot* slot, char* name, bool* directory, bool nameDirectories) {
  HalFile entry = file(slot).openNextFile();
  if (!entry) return false;
  *directory = entry.isDirectory();
  if (nameDirectories || !*directory) entry.getName(name, 128);
  entry.close();
  return true;
}
extern "C" bool xp_native_sd_path_exists(const char* path) { return Storage.exists(path); }

#if CROSSPOINT_VECTOR_FONTS
namespace {
unsigned long inspectRead(void* raw, unsigned long offset, unsigned char* bytes, unsigned long count) {
  auto& source = *static_cast<HalFile*>(raw);
  if (!source.seek(offset) || count == 0) return 0;
  const int read = source.read(bytes, count);
  return read < 0 ? 0 : static_cast<unsigned long>(read);
}
}  // namespace
#endif
extern "C" bool xp_native_sd_inspect(const char* path, uint16_t* weight, bool* italic) {
#if CROSSPOINT_VECTOR_FONTS
  HalFile source = Storage.open(path);
  if (!source || source.isDirectory()) return false;
  freeink::font::FtFont::FaceInfo info;
  if (freeink::font::FtFont::inspectStream(inspectRead, &source, source.size(), info) !=
      freeink::font::FtFont::InspectResult::Ok)
    return false;
  *weight = info.weight;
  *italic = info.italic;
  return true;
#else
  return false;
#endif
}
#endif
