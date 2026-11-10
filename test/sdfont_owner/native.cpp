#include <HalStorage.h>
#include <xp_sd_physical.h>

#include <cassert>
#include <cstring>
#include <map>

namespace {
std::map<const HalFile*, bool> live;
void fixed(const HalFile* file) { assert(live.count(file) && "native slot moved"); }
}  // namespace

// Use the production class declaration and placement leaves; replace SDK I/O only.
class HalFile::Impl {};
HalFile::HalFile() { assert(live.emplace(this, true).second); }
HalFile::~HalFile() {
  fixed(this);
  live.erase(this);
}
int HalFile::read(void* bytes, size_t count) {
  fixed(this);
  std::memset(bytes, 65, count);
  return static_cast<int>(count);
}
size_t HalFile::fileSize() {
  fixed(this);
  return 123;
}
bool HalFile::seekSet(size_t offset) {
  fixed(this);
  return offset <= 123;
}
bool HalFile::close() {
  fixed(this);
  return true;
}
size_t HalFile::write(uint8_t) { return 0; }
size_t HalFile::write(const uint8_t*, size_t) { return 0; }
HalStorage::HalStorage() = default;
HalStorage HalStorage::instance;
bool HalStorage::openFileForRead(const char*, const char* path, HalFile& file) {
  fixed(&file);
  return std::strcmp(path, "missing") != 0;
}

int main(int argc, char**) {
  XpSdFileSlot slots[2];
  for (auto& slot : slots) xp_native_sd_handle_init(&slot);
  uint32_t size = 0;
  assert(xp_native_sd_file_open(&slots[0], "font", &size) && size == 123);
  uint8_t byte = 0;
  assert(xp_native_sd_file_read(&slots[0], &byte, 1) == 1 && byte == 65);
  assert(xp_native_sd_file_seek(&slots[0], 123));
  assert(!xp_native_sd_file_seek(&slots[0], 124));
  xp_native_sd_file_close(&slots[0]);
  assert(!xp_native_sd_file_open(&slots[1], "missing", &size));
  if (argc > 1) {
    XpSdFileSlot moved;
    std::memcpy(&moved, &slots[0], sizeof(moved));
    xp_native_sd_file_close(&moved);  // Address guard fails before foreign destruction.
    return 1;
  }
  xp_native_sd_handle_destroy(&slots[1]);
  xp_native_sd_handle_destroy(&slots[0]);
  assert(live.empty());
}
