#include <ArduinoJson.h>
#include <HalStorage.h>

#include <bit>
#include <iostream>
#include <limits>

#include "src/util/BookmarkFile.h"
#include "src/util/BookmarkUtil.h"

static_assert(ARDUINOJSON_VERSION_MAJOR == 7 && ARDUINOJSON_VERSION_MINOR == 4 && ARDUINOJSON_VERSION_REVISION == 2);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
static_assert(ARDUINOJSON_USE_DOUBLE == 1 && ARDUINOJSON_USE_LONG_LONG == 1);
static_assert(ARDUINOJSON_SLOT_ID_SIZE == 2 && ARDUINOJSON_POOL_CAPACITY == 128);

namespace bookmark_test {
std::string hex(const std::string& bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (unsigned char c : bytes) {
    result += digits[c >> 4];
    result += digits[c & 15];
  }
  return result;
}
}  // namespace bookmark_test

std::string unhex(const char* text) {
  std::string result;
  const std::string s(text);
  for (size_t i = 0; i < s.size(); i += 2) result += static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16));
  return result;
}

int main() {
  using namespace bookmark_test;
  for (std::string line; std::getline(std::cin, line);) {
    JsonDocument input;
    if (deserializeJson(input, line)) return 2;
    files.clear();
    directories = {"/"};
    io.clear();
    calls.clear();
    failures.clear();
    for (JsonPair f : input["failures"].as<JsonObject>()) failures[f.key().c_str()] = f.value().as<unsigned>();
    fail = input["fail"] | "";
    failAt = input["fail_at"] | 1u;
    writeLimit = input["write_limit"] | SIZE_MAX;
    readLimit = input["read_limit"] | SIZE_MAX;
    const std::string bookPath = unhex(input["path"] | "");
    const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
    for (JsonObject f : input["files"].as<JsonArray>()) files[unhex(f["path"])] = unhex(f["bytes"]);
    std::vector<BookmarkEntry> records;
    for (JsonObject r : input["records"].as<JsonArray>()) {
      BookmarkEntry b{};
      b.xpath = unhex(r["xpath"] | "");
      b.summary = unhex(r["summary"] | "");
      b.name = unhex(r["name"] | "");
      b.percentage = std::bit_cast<float>(r["bits"].as<uint32_t>());
      b.computedSpineIndex = r["si"].as<uint16_t>();
      b.computedChapterPageCount = r["pc"].as<uint16_t>();
      b.computedChapterProgress = r["pp"].as<uint16_t>();
      b.hasVisibleTextOffset = r["has_vo"].as<bool>();
      b.visibleTextOffset = r["vo"].as<uint32_t>();
      records.push_back(b);
    }
    const bool load = std::string(input["action"] | "save") == "load";
    const bool ok = load ? BookmarkFile::load(bookPath, records) : BookmarkFile::save(bookPath, records);
    JsonDocument output;
    output["ok"] = ok;
    output["path"] = hex(path);
    auto fileRows = output["files"].to<JsonArray>();
    for (const auto& [p, bytes] : files) {
      auto row = fileRows.add<JsonObject>();
      row["path"] = hex(p);
      row["bytes"] = hex(bytes);
    }
    auto ops = output["io"].to<JsonArray>();
    for (const auto& op : io) ops.add(op);
    auto rows = output["records"].to<JsonArray>();
    for (const auto& b : records) {
      auto r = rows.add<JsonObject>();
      r["xpath"] = hex(b.xpath);
      r["summary"] = hex(b.summary);
      r["name"] = hex(b.name);
      r["bits"] = std::bit_cast<uint32_t>(b.percentage);
      r["si"] = b.computedSpineIndex;
      r["pc"] = b.computedChapterPageCount;
      r["pp"] = b.computedChapterProgress;
      r["has_vo"] = b.hasVisibleTextOffset;
      r["vo"] = b.visibleTextOffset;
    }
    serializeJson(output, std::cout);
    std::cout << '\n';
  }
}
