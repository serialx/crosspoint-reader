#include <Memory.h>
#include <xp_book.h>
#include <xp_epub_session.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "physical_defaults.h"

using Files = std::map<std::string, std::string>;
static Files files;
static std::vector<std::string> events;
static size_t liveFiles, calls, failAt;
static bool operation(std::string event) {
  events.push_back(std::move(event));
  return ++calls != failAt;
}
template <class... Args>
static void record(const char* level, const char* origin, const char* format, Args... args) {
  char buffer[256]{};
  std::snprintf(buffer, sizeof(buffer), format, args...);
  events.push_back(std::string(level) + ":" + origin + ":" + buffer);
}
#define ENABLE_SERIAL_LOG
#define LOG_LEVEL 2
#define LOG_DBG(origin, format, ...) record("DBG", origin, format, ##__VA_ARGS__)
#define LOG_ERR(origin, format, ...) record("ERR", origin, format, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) record("INF", origin, format, ##__VA_ARGS__)
struct HalFile {
  std::string path;
  size_t position = 0;
  HalFile() { ++liveFiles; }
  ~HalFile() {
    close();
    --liveFiles;
  }
  bool close() {
    if (path.empty()) return false;
    events.push_back("close " + path);
    path.clear();
    return true;
  }
  int read(uint8_t* bytes, size_t length) {
    if (!operation("read " + path + " " + std::to_string(length))) return -1;
    const auto& data = files.at(path);
    const size_t count = std::min(length, data.size() - position);
    std::memcpy(bytes, data.data() + position, count);
    position += count;
    return count;
  }
  size_t size() const {
    events.push_back("size " + path);
    return files.at(path).size();
  }
};
struct PhysicalStorage {
  bool exists(const char* path) { return operation("exists " + std::string(path)) && files.contains(path); }
  bool openFileForRead(const char* origin, const std::string& path, HalFile& file) {
    if (!operation("open " + std::string(origin) + " " + path) || !files.contains(path)) return false;
    file.path = path;
    return true;
  }
  bool removeDir(const char* path) {
    if (!operation("remove " + std::string(path))) return false;
    for (auto it = files.begin(); it != files.end();) {
      if (it->first.starts_with(std::string(path) + "/"))
        it = files.erase(it);
      else
        ++it;
    }
    return true;
  }
} Storage;
// ACTUAL_TEXT_HELPERS
// ACTUAL_CSTR_HELPER
static const XpAppResources resources = [] {
  auto value = unexpectedAppResources();
  // ACTUAL_RESOURCES
  return value;
}();
static const XpEpubLeaves leaves = [] {
  auto value = unexpectedEpubLeaves();
  // ACTUAL_LEAVES
  return value;
}();
struct TxtToHtml {
  static const char* cacheVersionTag(std::string_view);
};
struct Txt {
  static void invalidateCache(const std::string&);
  static bool validateCache(const std::string&, const std::string&, size_t);
};
// ACTUAL_TAG
// ACTUAL_INVALIDATE
// ACTUAL_VALIDATE
extern "C" uint8_t xp_epub_test_txt_validate(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText,
                                             uint32_t);
extern "C" size_t xp_test_live_allocations();
int main() {
  size_t pairs = 0;
  for (const auto& suffix : {".txt", ".MD", ".md", ".TXT"}) {
    const std::string path = "/books/\xff" + std::string(suffix), cache = "/cache";
    for (unsigned mode = 0; mode < 8; ++mode) {
      Files initial{{cache + "/sections/0.bin", "section"}};
      if (mode != 6) initial[path] = "raw";
      switch (mode) {
        case 0:
          break;
        case 1:
          initial[cache + "/html/0.html"] = TxtToHtml::cacheVersionTag(path);
          break;
        case 2:
          initial[cache + "/html/0.html"] = "old version";
          break;
        case 3:
          initial[cache + "/html/0.html"] = "";
          break;
        case 4:
          initial[cache + "/html/0.html"] = std::string("x\0", 2) + TxtToHtml::cacheVersionTag(path);
          break;
        default:
          initial[cache + "/html/0.html"] = std::string(65, 'x') + TxtToHtml::cacheVersionTag(path);
          break;
      }
      for (uint32_t cached : {0u, 3u, 4u}) {
        for (size_t fail = 0; fail <= 8; ++fail) {
          files = initial;
          events.clear();
          calls = 0;
          failAt = fail;
          const bool expected = Txt::validateCache(path, cache, cached);
          const auto expectedFiles = files;
          const auto expectedEvents = events;
          files = initial;
          events.clear();
          calls = 0;
          const bool actual = xp_epub_test_txt_validate(&resources, &leaves, text(path), text(cache), cached);
          assert(actual == expected && files == expectedFiles && events == expectedEvents);
          assert(liveFiles == 0 && xp_test_live_allocations() == 0);
          ++pairs;
        }
      }
    }
  }
  std::printf("%zu paired native/Rust TXT validation cases pass\n", pairs);
}
