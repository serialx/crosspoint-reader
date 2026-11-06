#include <HalClock.h>
#include <HalStorage.h>
#include <common/FsApiConstants.h>

#include "src/clippings/ClippingsManager.h"

namespace {
struct Bytes {
  const uint8_t* data;
  size_t length;
};
Bytes bytes(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
struct Calendar {
  int32_t year, month, day, weekday, hour, minute;
};
struct Operations {
  void* (*open)(Bytes);
  uint32_t (*size)(void*);
  size_t (*write)(void*, Bytes);
  uint8_t (*truncate)(void*, uint32_t);
  void (*flush)(void*);
  void (*drop)(void*);
  uint8_t (*calendar)(Calendar*);
};
const Operations operations{
    [](Bytes path) -> void* {
      const std::string name(reinterpret_cast<const char*>(path.data), path.length);
      auto* file = new HalFile(Storage.open(name.c_str(), O_RDWR | O_CREAT | O_AT_END));
      if (*file) return file;
      delete file;
      return nullptr;
    },
    [](void* file) -> uint32_t { return static_cast<HalFile*>(file)->fileSize(); },
    [](void* file, Bytes data) -> size_t { return static_cast<HalFile*>(file)->write(data.data, data.length); },
    [](void* file, uint32_t size) -> uint8_t { return static_cast<HalFile*>(file)->truncate(size); },
    [](void* file) { static_cast<HalFile*>(file)->flush(); },
    [](void* file) { delete static_cast<HalFile*>(file); },
    [](Calendar* out) -> uint8_t {
      struct tm local{};
      if (!halClock.localTime(local)) return 0;
      *out = {local.tm_year, local.tm_mon, local.tm_mday, local.tm_wday, local.tm_hour, local.tm_min};
      return 1;
    },
};
}  // namespace

extern "C" uint8_t xp_bookmarks_test_export(const Operations*, Bytes, Bytes, Bytes, int32_t, Bytes);

bool ClippingsManager::saveClipping(const std::string& title, const std::string& author, const std::string& chapter,
                                    const int page, const std::string& text) {
  return xp_bookmarks_test_export(&operations, bytes(title), bytes(author), bytes(chapter), page, bytes(text));
}
