#include <HalStorage.h>
#include <xp_sd_physical.h>

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
