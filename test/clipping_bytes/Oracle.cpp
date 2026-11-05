// Host driver for the clipping store byte oracle. Reads one case per line from
// stdin, runs the production ClippingStore/ClippingsManager against the fake
// volume in stubs/HalStorage.h, and writes one result row per line.
#include <ArduinoJson.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <esp_random.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "src/ClippingStore.h"
#include "src/clippings/ClippingsManager.h"

namespace clip_test_clock {
time_t now = 1700000000;
}

namespace {

std::string hex(const std::string& bytes) { return clip_test::hex(bytes); }

std::string hexBytes(const uint8_t* data, const size_t size) { return clip_test::hex(data, size); }

std::string unhex(const char* text) {
  std::string result;
  const std::string encoded(text ? text : "");
  assert(encoded.size() % 2 == 0);
  for (size_t i = 0; i + 1 < encoded.size(); i += 2) {
    result += static_cast<char>(std::stoi(encoded.substr(i, 2), nullptr, 16));
  }
  return result;
}

std::string field(JsonObjectConst object, const char* key) { return unhex(object[key] | ""); }

uint32_t number(JsonObjectConst object, const char* key, const uint32_t fallback) { return object[key] | fallback; }

bool flag(JsonObjectConst object, const char* key, const bool fallback) { return object[key] | fallback; }

Clipping makeClipping(JsonObjectConst row) {
  Clipping clipping;
  clipping.spineIndex = static_cast<uint16_t>(number(row, "spine", 0));
  clipping.startPage = static_cast<uint16_t>(number(row, "start", 0));
  clipping.endPage = static_cast<uint16_t>(number(row, "end", 0));
  clipping.pageCount = static_cast<uint16_t>(number(row, "pages", 1));
  clipping.startWordIndex = static_cast<uint16_t>(number(row, "startWord", 0));
  clipping.endWordIndex = static_cast<uint16_t>(number(row, "endWord", 0));
  clipping.wordCount = static_cast<uint16_t>(number(row, "words", 0));
  clipping.paragraphIndex = static_cast<uint16_t>(number(row, "para", UINT16_MAX));
  clipping.timestamp = number(row, "timestamp", 0);
  clipping.layoutSignature = number(row, "sig", 0);
  clipping.startOffset = number(row, "startOffset", UINT32_MAX);
  clipping.endOffset = number(row, "endOffset", UINT32_MAX);
  clipping.syncRevision = row["revision"] | 0ULL;
  clipping.pendingUpload = flag(row, "pending", true);
  const std::string id = field(row, "id");
  std::memcpy(clipping.id, id.data(), std::min<size_t>(id.size(), sizeof(clipping.id) - 1));
  clipping.textOffset = number(row, "textOffset", 0);
  clipping.textLength = static_cast<uint16_t>(number(row, "textLength", 0));
  const std::string chapter = field(row, "chapter");
  std::memcpy(clipping.chapterTitle, chapter.data(),
              std::min<size_t>(chapter.size(), sizeof(clipping.chapterTitle) - 1));
  return clipping;
}

void dumpClipping(JsonObject row, const Clipping& clipping) {
  row["spine"] = clipping.spineIndex;
  row["start"] = clipping.startPage;
  row["end"] = clipping.endPage;
  row["pages"] = clipping.pageCount;
  row["startWord"] = clipping.startWordIndex;
  row["endWord"] = clipping.endWordIndex;
  row["words"] = clipping.wordCount;
  row["para"] = clipping.paragraphIndex;
  row["timestamp"] = clipping.timestamp;
  row["sig"] = clipping.layoutSignature;
  row["startOffset"] = clipping.startOffset;
  row["endOffset"] = clipping.endOffset;
  row["revision"] = clipping.syncRevision;
  row["pending"] = clipping.pendingUpload;
  row["pendingByte"] = *reinterpret_cast<const unsigned char*>(&clipping.pendingUpload);
  row["id"] = hexBytes(reinterpret_cast<const uint8_t*>(clipping.id), sizeof(clipping.id));
  row["textOffset"] = clipping.textOffset;
  row["textLength"] = clipping.textLength;
  row["chapter"] = hexBytes(reinterpret_cast<const uint8_t*>(clipping.chapterTitle), sizeof(clipping.chapterTitle));
}

void setupVolume(JsonObjectConst input) {
  for (JsonVariantConst value : input["dirs"].as<JsonArrayConst>()) clip_test::addDirectory(unhex(value | ""));
  for (JsonObjectConst file : input["files"].as<JsonArrayConst>()) {
    clip_test::addFile(field(file, "path"), field(file, "bytes"));
  }
}

void setupFailures(JsonObjectConst input) {
  for (JsonPairConst failure : input["failures"].as<JsonObjectConst>()) {
    const std::string op = failure.key().c_str();
    if (failure.value().is<JsonArrayConst>()) {
      for (JsonVariantConst attempt : failure.value().as<JsonArrayConst>()) {
        clip_test::failures[op].push_back(attempt.as<unsigned>());
      }
    } else {
      clip_test::failures[op].push_back(failure.value().as<unsigned>());
    }
  }
  for (JsonPairConst entry : input["short"].as<JsonObjectConst>()) {
    const JsonObjectConst plan = entry.value().as<JsonObjectConst>();
    clip_test::shortOps[entry.key().c_str()] = {plan["at"] | 1u, plan["bytes"] | 0u};
  }
}

JsonObjectConst object(JsonVariantConst parent, const char* key) { return parent[key].as<JsonObjectConst>(); }

void runCase(JsonObjectConst input, JsonObject output) {
  clip_test::reset();
  clip_test_random::reset();
  clip_test_clock::now = input["now"] | 1700000000;
  const JsonObjectConst clock = object(input, "clock");
  halClock.available = flag(clock, "available", true);
  halClock.local = {};
  halClock.local.tm_year = static_cast<int>(number(clock, "year", 124));
  halClock.local.tm_mon = static_cast<int>(number(clock, "month", 4));
  halClock.local.tm_mday = static_cast<int>(number(clock, "day", 9));
  halClock.local.tm_hour = static_cast<int>(number(clock, "hour", 17));
  halClock.local.tm_min = static_cast<int>(number(clock, "minute", 32));
  halClock.local.tm_sec = static_cast<int>(number(clock, "second", 0));
  halClock.local.tm_wday = static_cast<int>(number(clock, "weekday", 4));
  setupVolume(input);
  setupFailures(input);
  clip_test::recordIo = flag(input, "io", true);

  output["id"] = input["id"];
  // Production reaches one static instance; a fresh instance per case is the
  // same type with the same state, and keeps cases independent.
  ClippingStore store;
  auto results = output["ops"].to<JsonArray>();
  auto deletions = output["deletions"].to<JsonArray>();
  auto texts = output["texts"].to<JsonArray>();
  auto previews = output["previews"].to<JsonArray>();
  for (JsonObjectConst op : input["ops"].as<JsonArrayConst>()) {
    const std::string name = op["op"] | "";
    auto row = results.add<JsonObject>();
    row["op"] = name;
    bool ok = false;
    if (name == "load") {
      const std::string path = field(op, "path");
      const std::string title = field(op, "title");
      const std::string author = field(op, "author");
      const std::string type = field(op, "type");
      ok = store.loadForBook(path, title, author, type.empty() ? std::string_view("epub") : std::string_view(type));
    } else if (name == "unload") {
      store.unload();
      ok = true;
    } else if (name == "add") {
      clip_test_clock::now = static_cast<time_t>(number(op, "now", static_cast<uint32_t>(clip_test_clock::now)));
      const std::string text = field(op, "text");
      const std::string chapter = field(op, "chapter");
      const auto added = store.addClipping(
          static_cast<uint16_t>(number(op, "spine", 0)), static_cast<uint16_t>(number(op, "start", 0)),
          static_cast<uint16_t>(number(op, "end", 0)), static_cast<uint16_t>(number(op, "pages", 1)),
          static_cast<uint16_t>(number(op, "startWord", 0)), static_cast<uint16_t>(number(op, "endWord", 0)),
          static_cast<uint16_t>(number(op, "words", 1)), chapter.c_str(), static_cast<uint16_t>(number(op, "para", 0)),
          text, number(op, "sig", 0), number(op, "startOffset", UINT32_MAX), number(op, "endOffset", UINT32_MAX));
      ok = added == ClippingStore::AddResult::Added;
      row["result"] = static_cast<unsigned>(added);
    } else if (name == "remove") {
      ok = store.removeClippingAt(number(op, "index", 0));
    } else if (name == "save") {
      ok = store.saveToFile();
    } else if (name == "sync") {
      ok = store.prepareSync();
    } else if (name == "uploads") {
      ok = store.finishUploads();
    } else if (name == "remote") {
      ok = store.applyRemote(makeClipping(object(op, "clipping")), field(op, "text"), flag(op, "deleted", false));
    } else if (name == "next_deletion") {
      uint32_t offset = number(op, "offset", 0);
      // nextDeletion() only writes where it gets to; the rest is the caller's buffer.
      char id[65] = {};
      ok = store.nextDeletion(offset, id);
      auto row2 = deletions.add<JsonObject>();
      row2["offset"] = offset;
      row2["id"] = hexBytes(reinterpret_cast<const uint8_t*>(id), sizeof(id));
    } else if (name == "finish_deletions") {
      ok = store.finishDeletions();
    } else if (name == "read_text") {
      std::string text;
      ok = store.readClippingText(number(op, "index", 0), text);
      auto row2 = texts.add<JsonObject>();
      row2["ok"] = ok;
      row2["text"] = hex(text);
    } else if (name == "preview") {
      const size_t size = number(op, "size", 260);
      std::vector<char> buffer(size + 1, 0);
      size_t length = 0;
      ok = store.readClippingPreview(number(op, "index", 0), buffer.data(), size, length);
      auto row2 = previews.add<JsonObject>();
      row2["ok"] = ok;
      row2["length"] = length;
      row2["text"] = hexBytes(reinterpret_cast<const uint8_t*>(buffer.data()), length);
    } else if (name == "export") {
      ok = ClippingsManager::saveClipping(field(op, "title"), field(op, "author"), field(op, "chapter"),
                                          static_cast<int>(number(op, "page", 0)), field(op, "text"));
    } else if (name == "delete_for_path") {
      ok = ClippingStore::deleteForFilePath(field(op, "path"), field(op, "type"));
    } else if (name == "move_book") {
      ok = ClippingStore::moveBook(field(op, "from"), field(op, "to"));
    } else {
      row["error"] = "unknown op";
    }
    row["ok"] = ok;
    row["count"] = store.clippingCount();
  }

  const size_t dump = input["dump"] | 0u;
  auto records = output["records"].to<JsonArray>();
  const size_t total = store.clippingCount();
  for (size_t i = 0; i < total && (dump == 0 || i < dump); ++i) {
    dumpClipping(records.add<JsonObject>(), *store.clippingAt(i));
  }
  output["recordCount"] = total;
  auto paths = output["paths"].to<JsonArray>();
  for (const auto& path : clip_test::paths) paths.add(hex(path));
  auto files = output["files"].to<JsonArray>();
  for (const auto& [path, node] : clip_test::nodes) {
    if (node->directory) continue;
    auto row = files.add<JsonObject>();
    row["path"] = hex(path);
    row["bytes"] = hexBytes(node->bytes.data(), node->bytes.size());
  }
  if (clip_test::recordIo) {
    auto events = output["io"].to<JsonArray>();
    for (const auto& event : clip_test::io) events.add(event);
  } else {
    auto counts = output["ioCounts"].to<JsonObject>();
    for (const auto& [op, count] : clip_test::attempts) counts[op] = count;
  }
}

}  // namespace

int main() {
  for (std::string line; std::getline(std::cin, line);) {
    JsonDocument input;
    if (deserializeJson(input, line)) return 2;
    JsonDocument result;
    runCase(input.as<JsonObjectConst>(), result.to<JsonObject>());
    serializeJson(result, std::cout);
    std::cout << '\n';
  }
  return 0;
}
