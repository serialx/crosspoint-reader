#include <HalStorage.h>
#include <xp_sd_physical.h>

#include <cassert>
#include <cstring>
#include <map>

namespace {
struct Handle {
  bool open = false;
  bool directory = false;
  unsigned next = 0;
};
std::map<const HalFile*, Handle> live;
unsigned names = 0, closes = 0;
void fixed(const HalFile* file) { assert(live.count(file) && "native slot moved"); }
}  // namespace

// Use the production class declaration and placement leaves; replace SDK I/O only.
class HalFile::Impl {};
HalFile::HalFile() { assert(live.emplace(this, Handle{}).second); }
HalFile::HalFile(HalFile&& other) : HalFile() { *this = std::move(other); }
HalFile& HalFile::operator=(HalFile&& other) {
  fixed(this);
  fixed(&other);
  live[this] = live[&other];
  live[&other] = {};
  return *this;
}
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
  live[this].open = false;
  ++closes;
  return true;
}
HalFile::operator bool() const {
  fixed(this);
  return live[this].open;
}
bool HalFile::isDirectory() const {
  fixed(this);
  return live[this].directory;
}
size_t HalFile::getName(char* out, size_t capacity) {
  fixed(this);
  ++names;
  const char* name = live[this].directory ? "nested" : "12.cpfont";
  assert(std::strlen(name) < capacity);
  std::strcpy(out, name);
  return std::strlen(name);
}
HalFile HalFile::openNextFile() {
  fixed(this);
  HalFile entry;
  const unsigned index = live[this].next++;
  live[&entry] = {index < 2, index == 0, 0};
  return entry;
}
size_t HalFile::write(uint8_t) { return 0; }
size_t HalFile::write(const uint8_t*, size_t) { return 0; }
HalStorage::HalStorage() = default;
HalStorage HalStorage::instance;
bool HalStorage::openFileForRead(const char*, const char* path, HalFile& file) {
  fixed(&file);
  live[&file].open = std::strcmp(path, "missing") != 0;
  return live[&file].open;
}
HalFile HalStorage::open(const char* path, oflag_t) {
  HalFile handle;
  live[&handle] = {std::strcmp(path, "missing") != 0, std::strcmp(path, "directory") == 0, 0};
  return handle;
}
bool HalStorage::exists(const char* path) { return std::strcmp(path, "missing") != 0; }

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
  assert(xp_native_sd_directory_open(&slots[0], "missing") == 0);
  assert(xp_native_sd_directory_open(&slots[0], "font") == -1);
  assert(xp_native_sd_directory_open(&slots[0], "directory") == 1);
  char name[128] = {};
  bool directory = false;
  const unsigned startCloses = closes;
  assert(xp_native_sd_directory_next(&slots[0], name, &directory, false));
  assert(directory && names == 0 && name[0] == 0);
  assert(xp_native_sd_directory_next(&slots[0], name, &directory, false));
  assert(!directory && names == 1 && std::strcmp(name, "12.cpfont") == 0);
  assert(!xp_native_sd_directory_next(&slots[0], name, &directory, false));
  assert(closes == startCloses + 2);
  assert(xp_native_sd_directory_open(&slots[0], "directory") == 1);
  assert(xp_native_sd_directory_next(&slots[0], name, &directory, true));
  assert(directory && names == 2 && std::strcmp(name, "nested") == 0);
  assert(xp_native_sd_path_exists("font") && !xp_native_sd_path_exists("missing"));
  uint16_t weight = 123;
  bool italic = true;
  assert(!xp_native_sd_inspect("font", &weight, &italic) && weight == 123 && italic);
  xp_native_sd_handle_destroy(&slots[1]);
  xp_native_sd_handle_destroy(&slots[0]);
  assert(live.empty());
}
